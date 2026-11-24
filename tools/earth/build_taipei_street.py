#!/usr/bin/env python3
"""Compile a small Taipei street, with terrain construction before voxelization."""
import argparse
from collections import Counter
from hashlib import sha256
import json
import math
from pathlib import Path
import shutil
import tempfile
from time import perf_counter

import numpy as np
from PIL import Image
import rasterio
from rasterio.windows import Window
from scipy.ndimage import distance_transform_edt, gaussian_filter, map_coordinates
from scipy.spatial import ConvexHull
from shapely import make_valid, union_all
from shapely.geometry import LineString, Point, box
from shapely.ops import substring

from build_taipei_urban import (ATTRIBUTION, ECEF, GEOGRAPHIC, ROOT, WORLD, build_structures,
                               burn, data_layers, dump, ground_raster, local_frame,
                               sample_raster, terrain_grid, write_raster)

SPAN = 384
CELL = 2
TEXEL = .1875
SURFACE_SUBDIVISIONS = 2
MAJOR = {'primary', 'secondary', 'tertiary'}
DRIVING = MAJOR | {'unclassified', 'residential', 'living_street', 'service'}


def coords(transform, shape):
    x = transform.c+(np.arange(shape[1])+.5)*transform.a
    y = transform.f+(np.arange(shape[0])+.5)*transform.e
    return np.meshgrid(x, y)


def interpolate(values, transform, x, y):
    col, row = (~transform)*(np.asarray(x), np.asarray(y))
    shape = np.shape(col)
    return map_coordinates(values, [np.ravel(row-.5), np.ravel(col-.5)], order=1, mode='nearest').reshape(shape)


def distance(mask):
    return distance_transform_edt(~mask, sampling=TEXEL)


def ramp(value, start, end):
    t = np.clip((value-start)/(end-start), 0., 1.)
    return t*t*(3.-2.*t)


def raster_at_lonlat(path, lon, lat):
    with rasterio.open(path) as source:
        if not source.crs or source.crs.to_epsg() != 4326:
            raise ValueError('the pinned ellipsoidal DSM must use EPSG:4326')
        col, row = (~source.transform)*(lon, lat)
        x0, y0 = int(col.min())-2, int(row.min())-2
        window = Window(x0, y0, int(col.max())-x0+4, int(row.max())-y0+4)
        values = source.read(1, window=window, masked=True).astype(np.float64).filled(np.nan)
        out = interpolate(values, source.window_transform(window), lon, lat)
        if not np.isfinite(out).all():
            raise ValueError('missing DSM pixels for the terrain context edge')
        return out


def base_edge_heights(path, longitude, latitude, x, y, local, context):
    """Interpolate the same coarse triangle grid used by the Earth context."""
    from pyproj import Transformer
    to_geo = Transformer.from_crs('EPSG:4978', 'EPSG:4979', always_xy=True)
    lon, lat = np.radians([longitude, latitude])
    east = np.array([-np.sin(lon), np.cos(lon), 0.])
    up = np.array([np.cos(lon)*np.cos(lat), np.sin(lon)*np.cos(lat), np.sin(lat)])
    south = np.cross(east, up)
    origin = np.asarray(ECEF.transform(longitude, latitude, 0.))

    def geo(e, h, s):
        components = [origin[i]+east[i]*e+up[i]*h+south[i]*s for i in range(3)]
        return to_geo.transform(*components)

    chart = local(x.ravel(), y.ravel(), np.zeros(x.size)).reshape(*x.shape, 3)
    low = context['coverage']['min_x']
    pitch = (context['coverage']['max_x']-low)/(context['grid_size']-1)
    gx, gz = (chart[..., 0]-low)/pitch, (chart[..., 2]-low)/pitch
    ix, iz = np.floor(gx).astype(int), np.floor(gz).astype(int)
    xmin, zmin = ix.min(), iz.min()
    xx, zz = np.meshgrid(low+np.arange(xmin, ix.max()+2)*pitch, low+np.arange(zmin, iz.max()+2)*pitch)
    hh = np.zeros_like(xx)
    for _ in range(3):
        longitudes, latitudes, _ = geo(xx, hh, zz)
        h = raster_at_lonlat(path, longitudes, latitudes)
        absolute = np.asarray(ECEF.transform(longitudes, latitudes, h)).reshape(3, -1).T
        hh = ((absolute-origin)@up).reshape(xx.shape)
    u, v = gx-ix, gz-iz
    c, r = ix-xmin, iz-zmin
    a, b, d, e = hh[r,c], hh[r,c+1], hh[r+1,c], hh[r+1,c+1]
    target = np.where(u+v<=1., a*(1.-u-v)+b*u+d*v, e*(u+v-1.)+b*(1.-v)+d*(1.-u))
    # The curvature offset is obtained from the zero-height point in this frame.
    return (target-chart[..., 1]).astype(np.float32)


def align_to_context(values, transform, dsm, center, local, context):
    x, y = coords(transform, values.shape)
    px, py = WORLD.transform(*center)
    edge = SPAN/2.-np.maximum(abs(x-px), abs(y-py))
    transition = ramp(edge, 1., 38.)
    needs = transition < 1.
    if np.any(needs):
        heights = base_edge_heights(dsm, *center, x[needs], y[needs], local, context)
        values[needs] = (values[needs]*transition[needs]+heights*(1.-transition[needs]))
    return transition


def street_masks(roads, area, buildings, shape, transform):
    # Clip after buffering so shoulder geometry is continuous where source ways
    # join. Bridges and tunnels were filtered when reading the OSM source.
    ways = [(line, kind, width, source) for line, kind, width, source in roads if line.intersects(area)]
    carriage = union_all([line.buffer(width*.5, quad_segs=8) for line, kind, width, _ in ways if kind in DRIVING])
    if carriage.is_empty:
        raise ValueError('the selected window has no ground-level road')
    main = union_all([line.buffer(width*.5, quad_segs=8) for line, kind, width, _ in ways if kind in MAJOR])
    pavement = burn([carriage.intersection(area)], shape, transform)
    major = burn([main.intersection(area)], shape, transform)
    exclusion = burn([buildings.buffer(.65)], shape, transform)
    grade_distance = distance(pavement)
    paths = burn([line.buffer(width*.5).intersection(area) for line, kind, width, _ in ways
                  if kind in ('footway', 'cycleway', 'path', 'pedestrian')], shape, transform)
    sidewalk = (distance(major)<=2.05) & ~pavement & ~exclusion
    curb = sidewalk & (distance(major)<=.48)
    return ways, carriage, pavement, sidewalk, curb, paths & ~pavement & ~exclusion, grade_distance


def construct_street(base, pavement, sidewalk, dist):
    # Use one continuous grade reference for all connecting roads to avoid
    # elevation disagreements at intersections. Crown and curb are offsets
    # to that smooth grade, not painted highlights.
    grade = gaussian_filter(base, sigma=7./TEXEL)
    depth = distance(~pavement)
    crown = np.minimum(depth*.019, .14)
    elevated_walk = .17*ramp(dist, .12, .65)
    design = grade+np.where(pavement, crown, np.where(sidewalk, elevated_walk, .17))
    mix = 1.-ramp(dist, 2.10, 9.25)
    return (base+(design-base)*mix).astype(np.float32)


def lane_markings(ways, pavement, area, shape, transform):
    dashes = []
    for line, kind, width, _ in ways:
        if kind not in MAJOR or width < 6.:
            continue
        clip = line.intersection(area)
        for run in ([clip] if clip.geom_type=='LineString' else getattr(clip, 'geoms', ())):
            if run.geom_type!='LineString' or run.length<14:
                continue
            # Dash lengths in metres. Leave room at way endpoints/junctions.
            for start in np.arange(6., run.length-7., 9.):
                dashes.append(substring(run, start, min(start+3.2, run.length-5.)).buffer(.16, cap_style='flat'))
    return burn(dashes, shape, transform)&pavement


def materials(image, transform, transition, ways, area, masks, greens, buildings, surfaces, output):
    pavement, sidewalk, curb, paths = masks
    shape = pavement.shape
    base = sample_raster(image, shape, transform, [1, 2, 3]).transpose(1, 2, 0)
    rng = np.random.default_rng(31026)
    grain = rng.normal(0.,1.,shape).astype(np.float32)

    def noise(scale):
        n = np.maximum(2, np.ceil(np.array(shape)*TEXEL/scale).astype(int))
        samples = rng.normal(0.,1.,n).astype(np.float32)
        return np.asarray(Image.fromarray(samples).resize(shape[::-1], Image.Resampling.BILINEAR))

    def fill(mask, color, amount):
        base[mask] = np.asarray(color)+amount[mask,None]

    blotch, macro = noise(2.4), noise(18.)
    ground_green = burn(greens, shape, transform)
    ground_green |= (base[...,1]>base[...,0]*1.04)&(base[...,1]>base[...,2]*1.14)
    x,y = coords(transform, shape)
    def texture(name, mask, tile_width, strength):
        path=surfaces.get(name)
        if path is None or not mask.any():
            return
        with Image.open(path) as source:
            pitch=max(8,round(tile_width/TEXEL))
            pixels=np.asarray(source.convert('RGB').resize((pitch,pitch),Image.Resampling.BOX),dtype=np.float32)
        values=pixels[np.remainder(np.floor(y[mask]/TEXEL).astype(int),pitch),
                      np.remainder(np.floor(x[mask]/TEXEL).astype(int),pitch)]
        base[mask]=(1.-strength)*base[mask]+strength*values

    soil = ~ground_green & ~pavement & ~sidewalk & ~paths
    # Control satellite luminance in the near zone, retaining its variations
    # at district scale. Colour material layers remain deterministic.
    original = base.copy()
    base[soil] = .63*base[soil]+.37*np.array([111,105,91])+((blotch*5+macro*7+grain*1.4)[soil,None])
    fill(ground_green, (76,92,50), blotch*9+macro*10+grain*2.8)
    fill(paths, (122,112,98), blotch*7+macro*3+grain*2.2)
    fill(pavement, (53,54,56), blotch*5.7+macro*4.5+grain*2.6)
    seams = ((np.mod(x+.35*y,2.4)<.23)|(np.mod(y-.35*x,2.75)<.23))
    fill(sidewalk, (133,126,112), blotch*5.0+macro*3.2+grain*1.7-seams*11.)
    texture('asphalt',pavement,30.,.67)
    texture('paving',sidewalk,4.5,.62)
    texture('grass',ground_green & ~sidewalk & ~pavement & ~paths,6.,.65)
    texture('soil',soil,6.,.38)
    fill(curb, (163,158,143), blotch*5.+grain*2.2)
    dashes = lane_markings(ways,pavement,area,shape,transform)
    fill(dashes, (202,196,169), blotch*8.+grain*3.)
    gutter = pavement & (distance(~pavement)<=.65)
    base[gutter] *= .93
    # Small darkening is a baked contact cue from model footprints, independent
    # of sun direction. It fades completely before the scene boundary.
    near_wall = distance(burn([buildings],shape,transform))
    factor = 1.-.19*(1.-ramp(near_wall,.30,3.1))
    base *= np.where(pavement, 1., factor)[...,None]
    # At the boundary, approach the filtered image used by the wider context.
    far = np.stack([gaussian_filter(original[...,i],sigma=9./TEXEL) for i in range(3)],axis=-1)
    base = base*transition[...,None]+far*(1.-transition[...,None])
    write_raster(output,np.clip(np.rint(base),0,255).astype(np.uint8).transpose(2,0,1),transform)


def tree_asset(directory, template):
    rng = np.random.default_rng(701+template)
    obj = directory / f'tree-{template}.obj'
    radii = (2.6, 2.15, 2.9)
    ratio = radii[template]
    points, normals, triangles = [], [], []

    def hull(vertices, material):
        offset=len(points)
        points.extend(vertices)
        nd=vertices-vertices.mean(axis=0)
        if material=='bark':
            # Use a cylinder radial normal, avoiding a spherical trunk.
            axis=vertices[len(vertices)//2:].mean(axis=0)-vertices[:len(vertices)//2].mean(axis=0)
            axis/=np.linalg.norm(axis)
            nd-=np.outer(nd@axis,axis)
        normals.extend(nd/np.linalg.norm(nd,axis=1)[:,None])
        surface=ConvexHull(vertices)
        for row, plane in zip(surface.simplices,surface.equations):
            a,b,c=vertices[row]
            if np.dot(np.cross(b-a,c-a),plane[:3])<0:
                row=row[::-1]
            triangles.append((material, *(int(i)+offset+1 for i in row)))

    def branch(start, tip, low, high):
        direction=np.array(tip)-start
        direction=direction/np.linalg.norm(direction)
        side=np.cross(direction,[0.,0.,1.]);side/=np.linalg.norm(side)
        along=np.cross(direction,side)
        radial=np.array([side*math.cos(a)+along*math.sin(a) for a in np.arange(7)*2.*math.pi/7.])
        hull(np.vstack((start+low*radial,tip+high*radial)),'bark')

    branch(np.array([0.,-.35,0.]),np.array([.14,4.3,0.]),.29,.16)
    for k in range(7):
        angle=k*math.pi*2/7.+rng.uniform(-.18,.18)
        center=np.array([math.cos(angle)*ratio*.62,4.1+rng.uniform(-.45,.85), math.sin(angle)*ratio*.62])
        branch(np.array([0.,2.2,0.]),center,.13,.045)
        height=1.38+rng.uniform(-.3,.45)
        sphere=[]
        for i in range(69):
            yy=1.-2.*(i+.5)/69.
            theta=i*2.39996323
            horizontal=math.sqrt(1.-yy*yy)
            sphere.append([math.cos(theta)*horizontal*1.6,yy*height,math.sin(theta)*horizontal*1.65])
        hull(center+np.array(sphere)*rng.uniform(.87,1.15,(69,1)),f'leaf-{k%3}')
    with obj.open('w') as fout:
        print('mtllib district.mtl\ns off', file=fout)
        for x,y,z in points: print(f'v {x:.6f} {y:.6f} {z:.6f}',file=fout)
        for x,y,z in normals: print(f'vn {x:.6f} {y:.6f} {z:.6f}',file=fout)
        previous=None
        for material,a,b,c in triangles:
            if material!=previous:
                print(f'usemtl {material}',file=fout)
                previous=material
            print(f'f {a}//{a} {b}//{b} {c}//{c}',file=fout)
    return obj


def plant(directory, greens, carriage, footprints, grid, transform, local, active):
    model_dir=directory/'models'
    with (model_dir/'district.mtl').open('a') as f:
        f.write('\nnewmtl bark\nKd .12 .082 .046\nnewmtl leaf-0\nKd .085 .171 .045\n'
                'newmtl leaf-1\nKd .12 .219 .053\nnewmtl leaf-2\nKd .073 .141 .038\n')
    models=[tree_asset(model_dir,v) for v in range(3)]
    exclusion=carriage.buffer(4.4).union(footprints.buffer(4.4))
    planting=union_all(greens).intersection(active.buffer(-12)).difference(exclusion)
    rng=np.random.default_rng(318)
    west,south,east,north=planting.bounds if not planting.is_empty else active.bounds
    records, positions=[],[]
    for index,(x,y) in enumerate(zip(rng.uniform(west,east,4500),rng.uniform(south,north,4500))):
        if len(records)>=27:
            break
        p=Point(x,y)
        if not planting.contains(p) or any(p.distance(q)<7.0 for q in positions):
            continue
        target=local(x,y,float(interpolate(grid,transform,x,y)))
        path=models[index%len(models)]
        positions.append(p)
        records.append({'id':f'tree-{len(records)+1:02d}','role':'structure','position_m':target.tolist(),
                        'rotation_deg':[0,float(rng.uniform(-180,180)),0],
                        'source':{'type':'mesh','path':str(path.relative_to(directory)),
                                  'voxel_size_m':.45,'fill_interior':True,'surface_detail':'source_clipped',
                                  'import_workers':1,'planting_provenance':'generated within mapped green cover'},
                        'physics':{'motion':'static','support':'all'},
                        'material':{'color_rgba':[1,1,1,1],'temperature_k':294.}})
    return records


def inspection(ways, pavement, sidewalk, exclusion, active, grid, transform, local, base):
    def point_height(point, field=grid):
        return float(interpolate(field,transform,point.x,point.y))

    def chart(point, height=None):
        return local(point.x,point.y,point_height(point) if height is None else height).tolist()

    pavement=pavement.astype(np.float32)
    sidewalk=sidewalk.astype(np.float32)
    choices=[]
    for line,kind,width,_ in ways:
        if kind not in MAJOR or width<6.:
            continue
        clipped=line.intersection(active.buffer(-27.))
        if clipped.geom_type!='LineString' or clipped.length<62.:
            continue
        for s in np.arange(23.,clipped.length-27.,5.):
            p=clipped.interpolate(s)
            a,b=clipped.interpolate(s-5),clipped.interpolate(s+5)
            forward=np.array([b.x-a.x,b.y-a.y]);forward/=np.linalg.norm(forward)
            across=np.array([-forward[1],forward[0]])
            for sign in (-1,1):
                edge=Point(np.array(p.coords[0])+across*sign*(width*.5-.8))
                walk=Point(np.array(p.coords[0])+across*sign*(width*.5+1.15))
                if not (interpolate(pavement,transform,edge.x,edge.y)>.8 and
                        interpolate(sidewalk,transform,walk.x,walk.y)>.8):
                    continue
                if exclusion.distance(walk)<1.4:
                    continue
                rise=point_height(walk)-point_height(edge)
                start,end=clipped.interpolate(s-18),clipped.interpolate(s+23)
                slope=abs(point_height(end)-point_height(start))/start.distance(end)
                fit=abs(point_height(start,base)-point_height(start))
                if .08<rise<.32 and slope<.13:
                    choices.append((-abs(slope-.055)*8.-fit*.3-abs(rise-.17),p,edge,walk,start,end,float(slope)))
    if not choices:
        raise ValueError('could not find a robust cross-section across the generated road and sidewalk')
    _,center,edge,walk,start,end,slope=max(choices,key=lambda row:row[0])
    p=np.array(chart(center)); start=np.array(chart(start)); end=np.array(chart(end))
    # View from a driver-height position. Look slightly to one side of the
    # direction of travel so the footway and tree line enter the frame.
    direction=end-start; direction[1]=0.; direction/=np.linalg.norm(direction)
    side=np.asarray(chart(walk))-p;side[1]=0.;side/=np.linalg.norm(side)
    view=p-direction*16.+side*2.1
    view[1]=start[1]+2.5
    target=p+direction*26.+side*3.3
    target[1]=end[1]+2.35
    return {'road_point_m':p.tolist(),'road_edge_m':chart(edge),'sidewalk_m':chart(walk),
            'road_section_endpoints_m':[start.tolist(),end.tolist()],'longitudinal_grade_ratio':slope,
            'cameras':[
                {'name':'street','eye_m':view.tolist(),'target_m':target.tolist(),'fov_y':54.},
                {'name':'low-pass','eye_m':(p-direction*78.+side*62.+[0,54,0]).tolist(),
                 'target_m':(p+direction*34.+[0,6,0]).tolist(),'fov_y':48.},
                {'name':'overview','eye_m':[222.,284.,258.],'target_m':[0.,37.,0.],'fov_y':45.}],
            'crater_camera':{'eye_m':(p-direction*14.+side*7.+[0,13,0]).tolist(),
                             'target_m':p.tolist(),'fov_y':50.}}


def compile_street(args, output):
    dsm=args.data_root/'taipei-dsm30-ellipsoid.tif'
    image=args.data_root/'taipei-sentinel2-20240214-rgb10.tif'
    background=ROOT/'config/earth/taipei-urban-context.map.json'
    earth_map=json.loads(background.read_text())
    local=local_frame(*args.center)
    px,py=WORLD.transform(*args.center)
    half=SPAN/2.
    active=box(px-half-3,py-half-3,px+half+3,py+half+3)
    extent=(px-half-58,py-half-58,px+half+58,py+half+58)
    shape,transform=terrain_grid(extent,TEXEL)
    buildings,parks,roads,skipped,bounds=data_layers(args.osm)
    if bounds is not None:
        for x,y in ((active.bounds[0],active.bounds[1]),(active.bounds[2],active.bounds[3])):
            lon,lat=GEOGRAPHIC.transform(x,y)
            if not (float(bounds.get('minlon'))<=lon<=float(bounds.get('maxlon')) and
                    float(bounds.get('minlat'))<=lat<=float(bounds.get('maxlat'))):
                raise ValueError('OSM extract does not cover the street window')
    footprints=[make_valid(p) for _,p,_ in buildings if p.intersects(active)]
    building_mask=union_all(footprints)
    greens=[make_valid(p).intersection(active) for p in parks if p.intersects(active)]
    _,_,ground_source,correction=ground_raster(dsm,args.dtm_ellipsoidal,extent,active,footprints,output/'ground-reference.tif')
    base,=sample_raster(output/'ground-reference.tif',shape,transform,[1])
    raw,=sample_raster(dsm,shape,transform,[1])
    ways,carriage,pavement,sidewalk,curb,paths,dist=street_masks(roads,active,building_mask,shape,transform)
    ground=construct_street(base,pavement,sidewalk,dist)
    transition=align_to_context(ground,transform,dsm,args.center,local,earth_map['earth'])
    write_raster(output/'ground.tif',ground[None,:,:],transform)
    surfaces={}
    if args.surface_textures:
        for name in ('asphalt','paving','grass','soil'):
            path=args.surface_textures/f'{name}.jpg'
            if path.is_file(): surfaces[name]=path
        if not surfaces:
            raise ValueError('no supported material images in --surface-textures')
    materials(image,transform,transition,ways,active,(pavement,sidewalk,curb,paths),greens,building_mask,surfaces,output/'surface.tif')
    structures, records=build_structures(output,buildings,active.buffer(-22.),ground,transform,ground_source,
                                         local,1.5,30,skipped)
    if not structures:
        raise ValueError('street window has no compiled foreground buildings')
    trees=plant(output,greens,carriage,building_mask,ground,transform,local,active)
    plan=inspection(ways,pavement,sidewalk,building_mask,active,ground,transform,local,raw)
    plan['terrain_id']='district-ground'
    dump(output/'inspection.json',plan)

    earth_map['name']='Taipei street construction sample'
    earth_map['earth']['provenance_assumed']=args.dtm_ellipsoidal is None
    earth_map['earth']['excavation_albedo_srgb']=[116,91,70]
    earth_map['earth']['data_provenance'].update({
        'street_grade': 'authored from the smoothed ground reference, not surveyed',
        'source_ground': ground_source,'cartography':ATTRIBUTION,
        'street_surfaces':'generated asphalt, sidewalk, curb, turf and soil over the pinned Sentinel-2 background',
        'street_surface_texel_m':TEXEL,'source_dsm_native_m':30})
    earth_map['layers']=[{'type':'GDALElevation','uri':'ground.tif'}, {'type':'GDALImage','uri':'surface.tif'}]
    dump(output/'street.map.json',earth_map)
    utc={'year':2026,'month':9,'day':10,'hours_utc':2.}
    config={'format':'voxel-scene-v1','fixed_step_seconds':.02,
            'geo_anchor':{'lat':args.center[1],'lon':args.center[0],'alt':0.},'utc_origin':utc,
            'loading':{'workers':4,'target_jobs':4}}
    area={'min_x':-half,'max_x':half,'min_z':-half,'max_z':half}
    terrain={'id':'district-ground','role':'terrain',
             'source':{'type':'earth','map':'street.map.json','mode':'hd','voxel_surface':'smooth',
                       'cell_size_m':CELL,'hd_subdivisions':SURFACE_SUBDIVISIONS,
                       'bottom_m':CELL*math.floor((float(min(ground.min(),raw.min()))-16.)/CELL),
                       'earth':{'coverage':area,'grid_size':SPAN//CELL*SURFACE_SUBDIVISIONS+1,
                                'imagery_size':round(SPAN/TEXEL),'strict':True}}}
    dump(output/'street.vscene.json',{**config,'objects':[terrain,*structures,*trees]})
    plain={**terrain['source'],'map':str(background),'hd_subdivisions':1}
    plain['earth']={**plain['earth'],'grid_size':SPAN//16+1,'imagery_size':SPAN//8}
    dump(output/'baseline.vscene.json',{**config,'objects':[{**terrain,'source':plain}]})
    camera=plan['cameras'][1]
    dump(output/'street.vtopic.json',{
        'format':'earth-scene-topic-v1','name':'台北南港近地道路样板','scene':'street.vscene.json',
        'earth_map':str(background),'paused':True,'sky':{'show_atmosphere':True,'time_scale':1.},
        'clouds':{'cover':.22},'camera':{'eye_m':camera['eye_m'],'target_m':camera['target_m']}})
    counts=Counter((source for _,kind,_,source in ways if kind in DRIVING))
    return {'span_m':SPAN,'center_lon_lat':args.center,'ground_source':ground_source,
            'source_dsm_m':30,'source_imagery_m':10,'ground_grid_m':CELL,
            'ground_surface_step_m':CELL/SURFACE_SUBDIVISIONS,
            'terrain_texture_texel_m':TEXEL,'ground_reference_max_correction_m':correction,
            'material_images_sha256':{name:sha256(path.read_bytes()).hexdigest() for name,path in surfaces.items()},
            'estimated_curb_design_m':.17,'estimated_sidewalk_width_m':2.05,
            'attribution':ATTRIBUTION,'osm_sha256':sha256(args.osm.read_bytes()).hexdigest(),
            'drivable_way_width_sources':dict(counts),'building_count':len(records),
            'buildings':records,'tree_count':len(trees),'tree_placement':'procedural, within mapped green areas',
            'skipped':dict(skipped),'inspection':plan,
            'assumptions':['Street grades, curb, sidewalk geometry and markings are generated.',
                           'Facade textures and species templates are generated.',
                           'No surveyed ground optical, infrared or radar calibration.']}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-root',type=Path,required=True)
    parser.add_argument('--osm',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--center',type=float,nargs=2,default=(121.6138,25.0558),metavar=('LON','LAT'))
    parser.add_argument('--dtm-ellipsoidal',type=Path)
    parser.add_argument('--surface-textures',type=Path,help='optional local asphalt.jpg, paving.jpg, grass.jpg or soil.jpg')
    args=parser.parse_args()
    args.out=args.out.resolve()
    if args.out.exists():
        parser.error('--out must be a new directory')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='.'+args.out.name+'-',dir=args.out.parent))
    start=perf_counter()
    try:
        report=compile_street(args,stage)
        report['offline_generation_seconds']=round(perf_counter()-start,3)
        dump(stage/'generation.json',report)
        stage.rename(args.out)
    finally:
        if stage.exists(): shutil.rmtree(stage)
    print(f'{args.out}: {report["building_count"]} buildings, {report["tree_count"]} trees')


if __name__=='__main__':
    main()
