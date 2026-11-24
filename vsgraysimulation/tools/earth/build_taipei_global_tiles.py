#!/usr/bin/env python3
"""Expose the prepared Taipei rasters through the existing global TMS adapter.

No engine format changes: regional tiles override the existing coarse global
parents. Only tiles fully covered by the real source rasters are published.
Requires numpy, rasterio and pyproj. Coarse sources are linked, never modified.
"""
import argparse
from contextlib import ExitStack
import json
import hashlib
import math
from pathlib import Path

import numpy as np
import rasterio
from rasterio.enums import Resampling
from rasterio.transform import from_origin
from rasterio.vrt import WarpedVRT
from rasterio.windows import Window


def build(root, coarse):
    target=root/"global-tiles"
    def identity(path):
        digest=hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda:stream.read(1024*1024),b""):digest.update(block)
        return digest.hexdigest()
    inputs={name:identity(root/name) for name in ("taipei-dsm30-ellipsoid.tif","taipei-sentinel2-20240214-rgb10.tif","provenance.json")}
    previous=json.loads((target/"provenance.json").read_text()) if (target/"provenance.json").exists() else {}
    reuse=previous.get("inputs_sha256")==inputs and previous.get("coarse_root")==str(coarse)
    manifest={"format":"taipei-hybrid-tms-v1", "profile":"global-geodetic",
              "inputs_sha256":inputs,
              "source_provenance":"../provenance.json", "coarse_root":str(coarse),
              "elevation_grid":"257 nodes, first and last nodes on tile boundaries",
              "imagery_grid":"256 area pixels, RGB", "tiles":{}}
    with ExitStack() as stack:
        for kind,filename,old_directory,max_level,nodes in [
            ("dem","taipei-dsm30-ellipsoid.tif","worldHeight",12,True),
            ("rgb","taipei-sentinel2-20240214-rgb10.tif","worldTexture_tiff",13,False)]:
            destination=target/kind;destination.mkdir(parents=True,exist_ok=True)
            # Preserve all original global parent data through filesystem links.
            for level in (coarse/old_directory).iterdir():
                if level.is_dir() and level.name.isdigit():
                    link=destination/level.name
                    if not link.exists():link.symlink_to(level.resolve(),target_is_directory=True)
            source=stack.enter_context(rasterio.open(root/filename))
            from rasterio.warp import transform_bounds
            bounds=transform_bounds(source.crs,"EPSG:4326",*source.bounds,densify_pts=21)
            # Use the common rectangular coverage, inside the prepared rasters.
            west,south,east,north=121.1005,24.5005,122.449,25.649
            assert bounds[0]<=west and bounds[1]<=south and bounds[2]>=east and bounds[3]>=north
            count=0
            for level in range(6,max_level+1):
                if (destination/str(level)).is_symlink():
                    raise RuntimeError("Refusing to write regional data through a coarse-source link")
                span=180/(1<<level);step=span/256
                x0=math.ceil((west+180)/span);x1=math.floor((east+180)/span)-1
                y0=math.ceil((90-north)/span);y1=math.floor((90-south)/span)-1
                if x1<x0 or y1<y0:continue
                # One VRT per level shares GDAL's source cache across all tiles.
                # For elevation, centers are offset by half a pixel so nodes
                # land exactly at both TMS edges, matching the existing reader.
                left=-180+x0*span;top=90-y0*span
                transform=from_origin(left-(step/2 if nodes else 0),top+(step/2 if nodes else 0),step,step)
                width=(x1-x0+1)*256+(1 if nodes else 0);height=(y1-y0+1)*256+(1 if nodes else 0)
                with WarpedVRT(source,crs="EPSG:4326",transform=transform,width=width,height=height,
                               resampling=Resampling.bilinear) as view:
                    for y in range(y0,y1+1):
                        for x in range(x0,x1+1):
                            path=destination/str(level)/str(x)/(str((1<<level)-1-y)+".tif")
                            if reuse and path.exists():count+=1;continue
                            size=257 if nodes else 256
                            window=Window((x-x0)*256,(y-y0)*256,size,size)
                            channels=(1,) if nodes else (1,2,3)
                            pixels=view.read(channels,window=window)
                            if not np.all(np.isfinite(pixels)):raise RuntimeError("Invalid regional tile")
                            if not nodes and np.any(view.read(4,window=window)!=255):
                                raise RuntimeError("Transparent regional tile; do not publish fabricated coverage")
                            path.parent.mkdir(parents=True,exist_ok=True)
                            temporary=path.with_suffix(".part")
                            with rasterio.open(temporary,"w",driver="GTiff",width=size,height=size,count=len(channels),
                                               dtype=pixels.dtype,crs="EPSG:4326",transform=view.window_transform(window),
                                               compress="deflate",predictor=3 if nodes else 2) as output:
                                output.write(pixels)
                                output.update_tags(provenance="../../../../provenance.json",source=filename,
                                                   vertical_datum="ellipsoidal" if nodes else "n/a")
                            temporary.replace(path);count+=1
                print(kind,"level",level,"tiles",(x1-x0+1)*(y1-y0+1),"total",count,flush=True)
            manifest["tiles"][kind]={"regional_count":count,"max_level":max_level,"size":257 if nodes else 256}
    (target/"provenance.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print("Prepared regional overrides with unchanged global fallback:",target,flush=True)


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("taipei_root",type=Path);parser.add_argument("coarse_root",type=Path)
    args=parser.parse_args();build(args.taipei_root.resolve(),args.coarse_root.resolve())
