#!/usr/bin/env python3
"""Download public, pinned Taipei rasters and prepare an offline Earth package.

Requires: numpy, requests, rasterio, pyproj (Python packages, not engine deps).
The result is actual satellite RGB + DSM, with no invented terrain/texture.
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
from pathlib import Path

import numpy as np
import requests
import rasterio
from rasterio.enums import Resampling
from rasterio.merge import merge
from rasterio.transform import from_origin
from rasterio.vrt import WarpedVRT
from rasterio.warp import reproject, transform_bounds
from rasterio.windows import Window

BOUNDS = (121.1, 24.5, 122.45, 25.65)  # contains the 120 km display context
SCENES = [
    ("UH", 1), ("UH", 0), ("UJ", 0), ("UJ", 1), ("VH", 0), ("VH", 1), ("VJ", 0),
]
DEM_ATTRIBUTION = ("produced using Copernicus WorldDEM-30 © DLR e.V. 2010-2014 and "
                   "© Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS "
                   "by the European Union and ESA; all rights reserved")


def download(pair):
    url, path = pair
    if path.is_file():
        return
    temporary = path.with_suffix(".part")
    with requests.get(url, stream=True, timeout=(20, 90)) as response:
        response.raise_for_status()
        with temporary.open("wb") as output:
            for chunk in response.iter_content(1024 * 1024):
                output.write(chunk)
    temporary.replace(path)
    print("Downloaded", path.name, flush=True)


def fingerprint(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return {"file": path.name, "bytes": path.stat().st_size, "sha256": digest.hexdigest()}


def prepare(root, skip_download):
    raw = root / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    sources, dem_paths, image_paths = [], [], []
    for latitude in (24, 25):
        for longitude in (121, 122):
            name = f"Copernicus_DSM_COG_10_N{latitude:02}_00_E{longitude:03}_00_DEM"
            path = raw / (name + ".tif")
            url = f"https://copernicus-dem-30m.s3.amazonaws.com/{name}/{name}.tif"
            sources.append((url, path))
            dem_paths.append(path)
    for square, sequence in SCENES:
        name = f"S2B_51R{square}_20240214_{sequence}_L2A"
        url = f"https://sentinel-cogs.s3.us-west-2.amazonaws.com/sentinel-s2-l2a-cogs/51/R/{square}/2024/2/{name}/TCI.tif"
        path = raw / (name + "-TCI.tif")
        sources.append((url, path))
        image_paths.append(path)
    geoid = raw / "us_nga_egm08_25.tif"
    sources.append(("https://cdn.proj.org/us_nga_egm08_25.tif", geoid))
    if not skip_download:
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            list(pool.map(download, sources))
    for _, path in sources:
        if not path.is_file():
            raise FileNotFoundError(path)

    print("Preparing 1 arc-second DSM and EGM2008 correction", flush=True)
    datasets = [rasterio.open(path) for path in dem_paths]
    try:
        # Preserve the original grid phase (pixel centers at integral arc
        # seconds); a half-pixel shifted merge can leave a false edge gap.
        step=1/3600
        west,south,east,north=BOUNDS
        aligned=(math.floor(west/step)*step-step/2, math.floor(south/step)*step+step/2,
                 math.ceil(east/step)*step-step/2, math.ceil(north/step)*step+step/2)
        dem, transform = merge(datasets, bounds=aligned, res=step, nodata=-9999)
    finally:
        for dataset in datasets:
            dataset.close()
    if np.any(dem == -9999) or not np.all(np.isfinite(dem)):
        raise RuntimeError("DSM has missing data; refusing to fabricate ground")
    undulation = np.empty(dem.shape[1:], dtype=np.float32)
    with rasterio.open(geoid) as dataset:
        reproject(rasterio.band(dataset, 1), undulation,
                  src_transform=dataset.transform, src_crs=dataset.crs,
                  dst_transform=transform, dst_crs="EPSG:4326",
                  resampling=Resampling.bilinear, num_threads=2)
    # Copernicus GLO-30 is EGM2008 orthometric: h_ellipsoid = H + N.
    # Zero ocean heights become the geoid surface, never synthetic bathymetry.
    dem[0] += undulation
    elevation = root / "taipei-dsm30-ellipsoid.tif"
    with rasterio.open(elevation, "w", driver="GTiff", width=dem.shape[2],
                       height=dem.shape[1], count=1, dtype="float32", crs="EPSG:4326",
                       transform=transform, tiled=True, blockxsize=256, blockysize=256,
                       compress="deflate", predictor=3) as output:
        output.write(dem)
        output.update_tags(source="Copernicus GLO-30 2021 release (DSM, not bare-earth DTM)",
                           vertical_datum="WGS84 ellipsoidal; H_EGM2008 + N_EGM2008",
                           geoid="PROJ us_nga_egm08_25.tif", source_spacing="1 arc second", attribution=DEM_ATTRIBUTION)
    correction = {"min_m": float(undulation.min()), "max_m": float(undulation.max())}
    del dem, undulation

    print("Mosaicking same-day Sentinel-2 10 m RGB with real validity masks", flush=True)
    west, south, east, north = transform_bounds("EPSG:4326", "EPSG:32651", *BOUNDS, densify_pts=21)
    west, south = math.floor(west / 10) * 10, math.floor(south / 10) * 10
    east, north = math.ceil(east / 10) * 10, math.ceil(north / 10) * 10
    width, height = int((east - west) / 10), int((north - south) / 10)
    rgb_transform = from_origin(west, north, 10, 10)
    imagery = root / "taipei-sentinel2-20240214-rgb10.tif"
    datasets = [rasterio.open(path) for path in image_paths]
    views = [WarpedVRT(dataset, crs="EPSG:32651", transform=rgb_transform,
                       width=width, height=height, resampling=Resampling.nearest)
             for dataset in datasets]
    missing = 0
    try:
        with rasterio.open(imagery, "w", driver="GTiff", width=width, height=height,
                           count=4, dtype="uint8", crs="EPSG:32651", transform=rgb_transform,
                           tiled=True, blockxsize=512, blockysize=512, compress="deflate",
                           predictor=2, photometric="RGB", BIGTIFF="IF_SAFER") as output:
            output.colorinterp = (rasterio.enums.ColorInterp.red, rasterio.enums.ColorInterp.green,
                                  rasterio.enums.ColorInterp.blue, rasterio.enums.ColorInterp.alpha)
            for top in range(0, height, 512):
                for left in range(0, width, 512):
                    window = Window(left, top, min(512, width-left), min(512, height-top))
                    rgba = np.zeros((4, int(window.height), int(window.width)), dtype=np.uint8)
                    for view in views:
                        data = view.read((1, 2, 3), window=window)
                        # In Sentinel TCI, all-black is padding, not valid water.
                        valid = (rgba[3] == 0) & np.any(data != 0, axis=0)
                        rgba[:3, valid] = data[:, valid]
                        rgba[3, valid] = 255
                    missing += int(np.count_nonzero(rgba[3] == 0))
                    output.write(rgba, window=window)
                if top % 2048 == 0:
                    print(f"  imagery rows {top}/{height}", flush=True)
            output.update_tags(source="Contains modified Copernicus Sentinel data (2024), Sentinel-2B L2A TCI",
                               acquisition="2024-02-14", source_spacing_m="10",
                               mosaic="First non-padding pixel from seven same-pass granules; no sharpening or generated detail")
    finally:
        for view in views:
            view.close()
        for dataset in datasets:
            dataset.close()
    if missing>256:
        raise RuntimeError(f"Uncovered RGB region ({missing} pixels): obtain the missing granules instead of inventing imagery")
    # A handful of all-black TCI pixels are isolated acquisition voids. Only
    # repair tiny holes using adjacent observed colours; record the exact count.
    with rasterio.open(imagery, "r+") as output:
        holes=np.argwhere(output.read(4)==0)
        for y,x in holes:
            left,top=max(0,int(x)-1),max(0,int(y)-1)
            block=output.read(window=Window(left,top,min(3,width-left),min(3,height-top)))
            valid=block[3]==255
            if np.count_nonzero(valid)<3:
                raise RuntimeError("RGB void has insufficient observed neighbours")
            sample=np.empty((4,1,1),dtype=np.uint8)
            sample[:3,0,0]=np.rint(np.mean(block[:3,valid],axis=1)).astype(np.uint8)
            sample[3,0,0]=255
            output.write(sample,window=Window(int(x),int(y),1,1))
        output.update_tags(isolated_void_pixels_interpolated=str(missing))
        output.build_overviews([2, 4, 8, 16], Resampling.average)
    manifest = {
        "format": "taipei-public-raster-provenance-v1", "bounds_wgs84": BOUNDS,
        "dem": {"product": "Copernicus GLO-30 DSM, 2021 release", "spacing_arcsec": 1,
                "attribution": DEM_ATTRIBUTION,
                "vertical_conversion": "h = H_EGM2008 + N_EGM2008", "geoid_undulation": correction,
                "ocean": "zero orthometric surface; no bathymetry",
                "source": "https://registry.opendata.aws/copernicus-dem/",
                "license": "https://dataspace.copernicus.eu/explore-data/data-collections/copernicus-contributing-missions/collections-description/COP-DEM"},
        "imagery": {"product": "Sentinel-2B L2A TCI", "date": "2024-02-14", "spacing_m": 10,
                    "source": "https://registry.opendata.aws/sentinel-2-l2a-cogs/",
                    "attribution": "Contains modified Copernicus Sentinel data (2024)",
                    "isolated_void_pixels_interpolated": missing, "missing_pixels_in_bounding_rectangle": 0},
        "sources": [{"url": url, **fingerprint(path)} for url, path in sources],
        "outputs": [fingerprint(elevation), fingerprint(imagery)],
    }
    (root / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("Prepared", root, "uncovered imagery pixels:", missing, flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--skip-download", action="store_true")
    args = parser.parse_args()
    prepare(args.output.resolve(), args.skip_download)
