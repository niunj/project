#!/usr/bin/env python3
"""Build a local voxel district from an offline OpenStreetMap extract.

Requires numpy, Pillow, pyproj, rasterio, scipy and shapely >= 2.1.
The runtime uses the existing Earth patch, OBJ importer and scene package.
"""
import argparse
from collections import Counter
from hashlib import sha256
import json
import math
from pathlib import Path
import re
import shutil
import tempfile
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image, ImageDraw
from pyproj import Transformer
import rasterio
from rasterio.enums import Resampling
from rasterio.features import rasterize
from rasterio.transform import from_origin
from rasterio.warp import reproject
from scipy.ndimage import distance_transform_edt, gaussian_filter, map_coordinates
from shapely import constrained_delaunay_triangles, make_valid, orient_polygons, union_all
from shapely.geometry import LineString, Polygon, box
from shapely.ops import polygonize


ROOT = Path(__file__).resolve().parents[2]
ATTRIBUTION = '© OpenStreetMap contributors, ODbL 1.0, https://www.openstreetmap.org/copyright'
UTM = 'EPSG:32651'
WORLD = Transformer.from_crs('EPSG:4326', UTM, always_xy=True)
GEOGRAPHIC = Transformer.from_crs(UTM, 'EPSG:4326', always_xy=True)
ECEF = Transformer.from_crs('EPSG:4979', 'EPSG:4978', always_xy=True)
ROADS = {'trunk': 15., 'primary': 13., 'secondary': 11., 'tertiary': 8.,
         'unclassified': 6., 'residential': 5.5, 'living_street': 4.5, 'service': 4.,
         'pedestrian': 4., 'cycleway': 2.6, 'footway': 1.8, 'path': 1.5}
WALLS = {'glass': (131, 148, 155), 'masonry': (177, 151, 135),
         'stone': (181, 177, 163), 'concrete': (160, 164, 162)}


def dump(path, document):
    path.write_text(json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + '\n')


def tags(element):
    return {item.attrib['k']: item.attrib['v'] for item in element.findall('tag')}


def polygons(geometry):
    if geometry.geom_type == 'Polygon' and not geometry.is_empty:
        yield geometry
    elif hasattr(geometry, 'geoms'):
        for part in geometry.geoms:
            yield from polygons(part)


def positive(value):
    if not value or not re.fullmatch(r'\s*\d+(?:\.\d+)?\s*m?\s*', value):
        return None
    number = float(value.strip().removesuffix('m'))
    return number if number > 0 and math.isfinite(number) else None


def ground_level(tag):
    return (tag.get('layer', '0') == '0' and tag.get('bridge', 'no') == 'no'
            and tag.get('tunnel', 'no') == 'no' and tag.get('location') not in ('underground', 'roof'))


def data_layers(path):
    document = ET.parse(path).getroot()
    if document.tag != 'osm':
        raise ValueError('expected an OpenStreetMap XML extract')
    node_ids, lon, lat = [], [], []
    for node in document.findall('node'):
        node_ids.append(node.attrib['id'])
        lon.append(float(node.attrib['lon']))
        lat.append(float(node.attrib['lat']))
    x, y = WORLD.transform(lon, lat)
    nodes = dict(zip(node_ids, zip(x, y)))
    ways = {}
    for way in document.findall('way'):
        refs = [node.attrib['ref'] for node in way.findall('nd')]
        if len(refs) >= 2 and all(ref in nodes for ref in refs):
            coordinates = [nodes[ref] for ref in refs]
            if len(set(coordinates)) >= 2:
                ways[way.attrib['id']] = (LineString(coordinates), tags(way))

    buildings, greens, roads, relation_ways = [], [], [], set()
    for relation in document.findall('relation'):
        tag = tags(relation)
        if tag.get('type') != 'multipolygon' or not (tag.get('building') or green(tag)):
            continue
        rings = {'outer': [], 'inner': []}
        refs = set()
        for member in relation.findall('member'):
            role, ref = member.get('role') or 'outer', member.get('ref')
            if member.get('type') == 'way' and role in rings:
                if ref not in ways:
                    break
                rings[role].append(ways[ref][0])
                refs.add(ref)
        else:
            outer = union_all(list(polygonize(rings['outer'])))
            if not outer.is_empty:
                footprint = outer.difference(union_all(list(polygonize(rings['inner']))))
                identity = 'relation-' + relation.attrib['id']
                if tag.get('building'):
                    buildings.append((identity, footprint, tag))
                    relation_ways.update(refs)
                elif green(tag):
                    greens.append(footprint)
    skipped = Counter()
    for way_id, (line, tag) in ways.items():
        kind = tag.get('highway', '').removesuffix('_link')
        if kind in ROADS and line.length > 0:
            if ground_level(tag) and tag.get('area') != 'yes':
                width = positive(tag.get('width'))
                lanes = positive(tag.get('lanes'))
                roads.append((line, kind, min(width or (3.15 * lanes if lanes else ROADS[kind]), 34.),
                              'osm_width' if width else 'estimated_from_lanes' if lanes else 'estimated_from_class'))
            else:
                skipped['roads_with_layer_or_area'] += 1
        if not line.is_ring and not line.is_closed:
            continue
        if tag.get('building') and way_id not in relation_ways:
            buildings.append(('way-' + way_id, Polygon(line.coords), tag))
        elif green(tag):
            greens.append(Polygon(line.coords))
    return buildings, greens, roads, skipped, document.find('bounds')


def green(tag):
    return (tag.get('leisure') in ('park', 'garden', 'recreation_ground')
            or tag.get('landuse') in ('grass', 'forest', 'meadow', 'village_green')
            or tag.get('natural') in ('wood', 'scrub', 'grassland'))


def terrain_grid(extent, spacing):
    west, south, east, north = extent
    width, height = math.ceil((east - west) / spacing), math.ceil((north - south) / spacing)
    return (height, width), from_origin(west, north, spacing, spacing)


def sample_raster(path, shape, transform, bands):
    values = np.full((len(bands), *shape), np.nan, dtype=np.float32)
    with rasterio.open(path) as source:
        if source.crs is None or max(bands) > source.count:
            raise ValueError(f'invalid raster CRS/bands in {path}')
        for band, out in zip(bands, values):
            reproject(rasterio.band(source, band), out, dst_crs=UTM, dst_transform=transform,
                      dst_nodata=np.nan, resampling=Resampling.bilinear)
    if not np.isfinite(values).all():
        raise ValueError(f'{path} has missing pixels in the requested district')
    return values


def write_raster(path, values, transform):
    with rasterio.open(path, 'w', driver='GTiff', crs=UTM, transform=transform,
                       width=values.shape[2], height=values.shape[1], count=len(values),
                       dtype=values.dtype, tiled=True, compress='deflate', predictor=2) as output:
        output.write(values)


def burn(geometries, shape, transform):
    items = [g for g in geometries if not g.is_empty]
    if not items:
        return np.zeros(shape, dtype=bool)
    return rasterize(items, out_shape=shape, transform=transform, dtype='uint8').astype(bool)


def ground_raster(dsm, dtm, extent, inner, footprints, output):
    shape, transform = terrain_grid(extent, 25.)
    raw, = sample_raster(dsm, shape, transform, [1])
    if dtm:
        source = 'user_supplied_ellipsoidal_dtm'
        with rasterio.open(dtm) as dataset:
            if not dataset.crs or dataset.crs.is_geographic or max(dataset.res) > 30:
                raise ValueError('DTM must be projected in metres and sampled at 30 m or finer')
            spacing = max(dataset.res)
        shape, transform = terrain_grid(extent, spacing)
        raw, = sample_raster(dsm, shape, transform, [1])
        preferred, = sample_raster(dtm, shape, transform, [1])
        weight = np.ones(shape, dtype=np.float32)
    else:
        source = 'inferred_ground_from_building_masked_GLO30_DSM'
        # Only correct likely rooftop contamination. Keep unmasked relief and
        # fill using neighbouring unmasked ground candidates on the source scale.
        mask = burn((shape.buffer(11.) for shape in footprints), shape, transform)
        if mask.all():
            raise ValueError('no unmasked DSM samples remain for ground reconstruction')
        blurred = gaussian_filter(np.where(mask, 0., raw), sigma=1.55)
        support = gaussian_filter((~mask).astype(np.float32), sigma=1.55)
        nearest = distance_transform_edt(mask, return_distances=False, return_indices=True)
        preferred = np.divide(blurred, support, out=raw[tuple(nearest)].copy(), where=support > .10)
        preferred = np.minimum(preferred, raw)
        weight = gaussian_filter(mask.astype(np.float32), sigma=.72)
        weight[mask] = 1.
    bounds_w, _, _, bounds_n = extent
    spacing = transform.a
    x = bounds_w + (np.arange(shape[1])+.5)*spacing
    y = bounds_n - (np.arange(shape[0])+.5)*spacing
    west, south, east, north = inner.bounds
    border = np.minimum(np.minimum(x-west, east-x)[None, :], np.minimum(y-south, north-y)[:, None])
    blend = np.clip((border-30.)/90., 0., 1.)
    result = (raw + (preferred-raw)*weight*(blend*blend*(3.-2.*blend))).astype(np.float32)
    write_raster(output, result[None, :, :], transform)
    return result, transform, source, float(np.max(np.abs(result-raw)))


def paint_ground(imagery, extent, inner, road_features, green_areas, output):
    shape, transform = terrain_grid(extent, .5)
    colors = sample_raster(imagery, shape, transform, [1, 2, 3]).transpose(1, 2, 0)
    rng = np.random.default_rng(84212)
    grain = rng.normal(0, 1, shape).astype(np.float32)
    grasses = burn((make_valid(area).intersection(inner) for area in green_areas
                    if area.intersects(inner)), shape, transform)
    colors[grasses] = colors[grasses]*.57 + np.array([79, 100, 59])*.43
    # Mark only ground-level roads, using the same patch imagery as the terrain
    # contact mesh. Widths may be tagged or estimated from lanes/road class.
    roads, borders, walkway, center_lines = [], [], [], []
    counts = Counter()
    for line, kind, width, evidence in road_features:
        if not line.intersects(inner):
            continue
        center = line.intersection(inner)
        counts[evidence] += 1
        if kind in ('footway', 'cycleway', 'pedestrian', 'path'):
            walkway.append(center.buffer(width*.5).intersection(inner))
            continue
        roads.append(center.buffer(width*.5).intersection(inner))
        borders.append(center.buffer(width*.5 + .55).intersection(inner))
        if width >= 7. and kind in ('primary', 'secondary', 'tertiary'):
            center_lines.append(center.buffer(.20).intersection(inner))
    for sets, rgb, mix in ((walkway, (121, 114, 103), .72), (borders, (151, 146, 134), .84),
                           (roads, (69, 72, 74), .92), (center_lines, (160, 145, 104), .70)):
        mask = burn(sets, shape, transform)
        colors[mask] = colors[mask]*(1.-mix) + np.array(rgb)*mix
    street = burn([*roads, *walkway, *borders], shape, transform)
    colors += grain[..., None] * np.where(street, 2.1, np.where(grasses, 3.4, 1.2))[..., None]
    write_raster(output, np.clip(np.round(colors), 0, 255).astype(np.uint8).transpose(2, 0, 1), transform)
    return counts


def style(tag, index):
    if tag.get('building') in ('commercial', 'office', 'hotel', 'transportation', 'train_station'):
        return 'glass'
    return ('masonry', 'stone', 'concrete')[index % 3]


def appearance(directory):
    materials = []
    rng = np.random.default_rng(417)
    for name, color in {**WALLS, 'roof': (118, 120, 116), 'foundation': (112, 107, 100)}.items():
        pixels = np.clip(np.array(color) + rng.normal(0, 2.8, (64, 64, 1)), 0, 255).astype(np.uint8)
        image = Image.fromarray(pixels)
        paint = ImageDraw.Draw(image)
        if name in WALLS:
            glass = (82, 108, 123) if name == 'glass' else (62, 82, 89)
            left, right, top, bottom = (6, 58, 11, 51) if name == 'glass' else (13, 50, 13, 44)
            paint.rectangle((left-2, top-2, right+2, bottom+3), fill=(86, 88, 85))
            for y in range(top, bottom+1):
                shade = 1.12-.28*(y-top)/(bottom-top)
                paint.line((left, y, right, y), fill=tuple(int(c*shade) for c in glass))
            paint.line((32, top, 32, bottom), fill=(131, 136, 132), width=2)
            paint.line((left-2, bottom+4, right+2, bottom+4), fill=(194, 191, 176), width=2)
        elif name == 'roof':
            paint.line((0, 0, 63, 0), fill=(104, 107, 105))
            paint.line((0, 0, 0, 63), fill=(104, 107, 105))
        image.save(directory / f'{name}.png')
        materials.append(f'newmtl {name}\nKd 1 1 1\nNs {95 if name == "glass" else 11}\nmap_Kd {name}.png\n')
    (directory / 'district.mtl').write_text('\n'.join(materials))


def building_height(tag):
    height, levels = positive(tag.get('height')), positive(tag.get('building:levels'))
    if height:
        return height, 'osm_height'
    if levels:
        return levels*3.25 + .7, 'estimated_from_osm_levels'
    kind = tag.get('building')
    height = {'office': 19., 'commercial': 13., 'apartments': 14., 'industrial': 8.,
              'school': 13., 'residential': 9., 'train_station': 12., 'temple': 6.}.get(kind, 8.)
    return height, 'estimated_from_class'


def write_building(path, footprint, y_bottom, y_roof, facade, floor_pitch):
    footprint = orient_polygons(footprint)
    vertices, coords, faces = {}, [], []

    def face(points, uv, material):
        indices = []
        for point, tex in zip(points, uv):
            index = vertices.setdefault(point, len(vertices)+1)
            coords.append(tex)
            indices.append(f'{index}/{len(coords)}')
        faces.append((material, 'f ' + ' '.join(indices)))

    for cap in constrained_delaunay_triangles(footprint).geoms:
        outline = list(orient_polygons(cap).exterior.coords)[:3]
        for height, points, material in ((y_roof, outline[::-1], 'roof'), (y_bottom, outline, 'foundation')):
            face([(x, height, z) for x, z in points], [(x/7., z/7.) for x, z in points], material)
    for ring in (footprint.exterior, *footprint.interiors):
        for (x1, z1), (x2, z2) in zip(ring.coords, list(ring.coords)[1:]):
            width = math.hypot(x2-x1, z2-z1)
            if width == 0:
                continue
            # Rings are CCW outside and CW inside on the X/Z plane. This
            # winding gives outward normals for exterior and courtyard walls.
            repeats = max(1, round(width/(2.8 if facade == 'glass' else 3.2)))
            v = (y_roof-y_bottom)/floor_pitch
            p = [(x1, y_bottom, z1), (x2, y_bottom, z2), (x2, y_roof, z2), (x1, y_roof, z1)]
            t = [(0., 0.), (repeats, 0.), (repeats, v), (0., v)]
            face([p[0], p[2], p[1]], [t[0], t[2], t[1]], facade)
            face([p[0], p[3], p[2]], [t[0], t[3], t[2]], facade)
    with path.open('w') as out:
        print('mtllib district.mtl\ns off', file=out)
        for x, y, z in vertices:
            print(f'v {x:.7f} {y:.7f} {z:.7f}', file=out)
        for u, v in coords:
            print(f'vt {u:.7f} {v:.7f}', file=out)
        previous = None
        for material, definition in faces:
            if material != previous:
                print(f'usemtl {material}', file=out)
                previous = material
            print(definition, file=out)


def local_frame(lon, lat):
    origin = np.array(ECEF.transform(lon, lat, 0.))
    longitude, latitude = np.radians([lon, lat])
    east = np.array([-np.sin(longitude), np.cos(longitude), 0.])
    up = np.array([np.cos(latitude)*np.cos(longitude), np.cos(latitude)*np.sin(longitude), np.sin(latitude)])
    axes = np.array([east, up, np.cross(east, up)])

    def local(x, y, height):
        longitude, latitude = GEOGRAPHIC.transform(x, y)
        absolute = np.array(ECEF.transform(longitude, latitude, height)).T
        return (absolute-origin) @ axes.T

    return local


def floor_samples(footprint, ground, transform, local):
    # Include corners and midpoints, so a base can follow the lowest contact
    # along any edge rather than a possibly elevated centre pixel.
    edge = footprint.exterior
    samples = [edge.interpolate(i, normalized=True).coords[0] for i in np.linspace(0, 1, 24, endpoint=False)]
    samples.extend(list(edge.coords)[:-1])
    samples.append(footprint.representative_point().coords[0])
    x, y = np.array(samples).T
    col, row = (~transform) * (x, y)
    height = map_coordinates(ground, [row-.5, col-.5], order=1, mode='nearest')
    return local(x, y, height)[:, 1]


def build_structures(directory, candidates, area, ground, transform, source, local, cell,
                     max_buildings, skipped):
    objects, metadata = [], []
    mesh_dir = directory / 'models'
    mesh_dir.mkdir()
    appearance(mesh_dir)
    accepted = []
    for identity, footprint, tag in candidates:
        if not footprint.intersects(area):
            continue
        if (tag.get('building') in ('no', 'roof', 'construction') or not ground_level(tag)
                or positive(tag.get('min_height')) or positive(tag.get('building:min_level'))):
            skipped['building_non_ground_or_incomplete'] += 1
            continue
        for index, part in enumerate(polygons(make_valid(footprint))):
            if not area.covers(part) or part.area < 25.:
                skipped['building_small_or_at_boundary'] += 1
                continue
            accepted.append((identity if index == 0 else f'{identity}-{index}', part, tag))
    # Keep large, coherent footprints first when an extract exceeds the active
    # object budget. Stable source IDs are independent of the ranking.
    accepted.sort(key=lambda row: (-row[1].area, row[0]))
    coverage = None
    for identity, shape, tag in accepted:
        if len(objects) >= max_buildings:
            skipped['building_count_budget'] += 1
            continue
        if coverage is not None and shape.intersection(coverage).area > shape.area*.70:
            skipped['duplicate_or_contained_footprint'] += 1
            continue
        height, evidence = building_height(tag)
        if not 1.5 <= height <= 260.:
            skipped['building_implausible_height'] += 1
            continue
        samples = floor_samples(shape, ground, transform, local)
        relief = float(np.ptp(samples))
        if relief > 12.:
            skipped['building_excessive_foundation_relief'] += 1
            continue
        center = shape.centroid.coords[0]
        center_x, _, center_z = local(*center, 0.)

        def ring_points(ring):
            x, y = np.array(ring.coords).T
            chart = local(x, y, np.zeros_like(x))
            return np.stack((chart[:, 0]-center_x, chart[:, 2]-center_z), axis=-1)

        footprint = Polygon(ring_points(shape.exterior), [ring_points(ring) for ring in shape.interiors])
        y_bottom = float(samples.min())-.45
        y_roof = float(np.median(samples))+height
        bounds = footprint.bounds
        dimensions = (bounds[2]-bounds[0], y_roof-y_bottom, bounds[3]-bounds[1])
        if math.prod(math.ceil((v+.04)/cell) for v in dimensions) > 300_000:
            skipped['building_individual_budget'] += 1
            continue
        identifier = 'osm-' + identity
        path = mesh_dir / f'{identifier}.obj'
        facade = style(tag, int(sha256(identity.encode()).hexdigest()[:8], 16))
        levels = positive(tag.get('building:levels'))
        floor_pitch = height/levels if levels else 3.25
        write_building(path, footprint, 0., y_roof-y_bottom, facade, max(2.3, min(4.6, floor_pitch)))
        location = [float(center_x), y_bottom, float(center_z)]
        roof_x, roof_z = footprint.representative_point().coords[0]
        record = {'id': identifier, 'osm_identity': identity, 'area_m2': round(shape.area, 2),
                  'height_m': round(height, 3), 'height_source': evidence, 'foundation_source': source,
                  'foundation_relief_m': round(relief, 2), 'facade': facade,
                  'roof_probe_m': [float(center_x+roof_x), y_roof, float(center_z+roof_z)]}
        objects.append({'id': identifier, 'role': 'structure', 'position_m': location,
                        'source': {'type': 'mesh', 'path': str(path.relative_to(directory)),
                                   'fill_interior': True, 'surface_detail': 'source_clipped',
                                   'voxel_size_m': cell, 'import_workers': 1,
                                   'district_provenance': {**record, 'attribution': ATTRIBUTION,
                                                           'surface': 'generated roof and facade'}},
                        'physics': {'motion': 'static', 'fracture': True, 'support': 'base', 'friction': .7},
                        'material': {'density_kg_m3': 850., 'temperature_k': 294., 'color_rgba': [1, 1, 1, 1]}})
        metadata.append(record)
        coverage = shape if coverage is None else coverage.union(shape)
    return objects, metadata


def prepare(args, directory):
    longitude, latitude = args.center
    local = local_frame(longitude, latitude)
    x, y = WORLD.transform(longitude, latitude)
    half = args.span/2.
    # Reserve a narrow edge outside the buildings/roads for a clean map seam.
    extent = (x-half-160., y-half-160., x+half+160., y+half+160.)
    active = box(x-half+30., y-half+30., x+half-30., y+half-30.)
    buildings, greens, roads, skipped, bounds = data_layers(args.osm)
    if bounds is not None:
        for px, py in ((active.bounds[0], active.bounds[1]), (active.bounds[2], active.bounds[3])):
            lng, lat = GEOGRAPHIC.transform(px, py)
            if not (float(bounds.attrib['minlon']) <= lng <= float(bounds.attrib['maxlon'])
                    and float(bounds.attrib['minlat']) <= lat <= float(bounds.attrib['maxlat'])):
                raise ValueError('OSM extract bounds do not cover the requested district')
    dsm = args.data_root / 'taipei-dsm30-ellipsoid.tif'
    image = args.data_root / 'taipei-sentinel2-20240214-rgb10.tif'
    background = ROOT / 'config/earth/taipei-urban-context.map.json'
    intersecting = [make_valid(poly) for _, poly, _ in buildings if poly.intersects(active)]
    ground, transform, source, correction = ground_raster(dsm, args.dtm_ellipsoidal, extent, active,
                                                          intersecting, directory / 'ground.tif')
    road_counts = paint_ground(image, extent, active, roads, greens, directory / 'street-surface.tif')
    structures, records = build_structures(directory, buildings, active.buffer(-8.), ground, transform,
                                           source, local, args.building_cell, args.max_buildings, skipped)
    if not structures:
        raise ValueError('district has no usable building footprints')
    ground_map = json.loads(background.read_text())
    ground_map['name'] = 'Taipei district terrain and generated ground-level surfaces'
    ground_map['earth']['data_provenance'].update({
        'district_ground': source, 'base_DSM_native_metres': 30,
        'district_imagery': '10 m Sentinel-2B with generated road/green cover; 0.5 m compositing raster',
        'district_vector_attribution': ATTRIBUTION,
        'district_roads': 'OpenStreetMap centre lines, inferred widths where absent, generated surfaces and markings',
    })
    ground_map['layers'] = [
        {'type': 'GDALElevation', 'name': 'district ground', 'uri': 'ground.tif'},
        {'type': 'GDALImage', 'name': 'district road and ground surface', 'uri': 'street-surface.tif'},
    ]
    dump(directory / 'district.map.json', ground_map)
    coverage = {'min_x': -half, 'max_x': half, 'min_z': -half, 'max_z': half}
    configuration = {'format': 'voxel-scene-v1', 'fixed_step_seconds': .02,
                     'geo_anchor': {'lat': latitude, 'lon': longitude, 'alt': 0.},
                     'utc_origin': {'year': 2026, 'month': 9, 'day': 10, 'hours_utc': 2.},
                     'loading': {'workers': 4, 'target_jobs': 4}}
    ground_source = {'type': 'earth', 'map': 'district.map.json', 'mode': 'hd',
                     'earth': {'coverage': coverage, 'grid_size': args.span//4+1,
                               'imagery_size': args.span*2, 'strict': True},
                     'voxel_surface': 'smooth', 'cell_size_m': 4.,
                     'bottom_m': 4.*math.floor((float(ground.min())-16.)/4.), 'hd_subdivisions': 2}
    terrain = {'id': 'district-ground', 'role': 'terrain', 'source': ground_source}
    dump(directory / 'district.vscene.json', {**configuration, 'objects': [terrain, *structures]})
    baseline = {**ground_source, 'map': str(background)}
    baseline['earth'] = {**baseline['earth'], 'grid_size': args.span//32+1, 'imagery_size': args.span//8}
    dump(directory / 'baseline.vscene.json', {**configuration, 'objects': [{**terrain, 'source': baseline}]})
    focus_x, focus_y, focus_z = records[0]['roof_probe_m']
    dump(directory / 'district.vtopic.json', {
        'format': 'earth-scene-topic-v1', 'name': '台北南港体素街区',
        'scene': 'district.vscene.json', 'earth_map': str(background), 'paused': True,
        'sky': {'show_atmosphere': True, 'time_scale': 1.}, 'clouds': {'cover': .24},
        'camera': {'eye_m': [focus_x+230., focus_y+160., focus_z+270.],
                   'target_m': [focus_x, focus_y, focus_z]},
    })
    report = {'center_lon_lat': args.center, 'working_span_m': args.span,
              'attribution': ATTRIBUTION, 'vector_file_sha256': sha256(args.osm.read_bytes()).hexdigest(),
              'building_count': len(structures), 'building_voxel_size_m': args.building_cell,
              'buildings': records, 'height_sources': dict(Counter(item['height_source'] for item in records)),
              'road_width_sources': dict(road_counts), 'skipped': dict(skipped), 'ground_source': source,
              'maximum_DSM_correction_m': correction, 'ground_voxel_size_m': 4.,
              'background_imagery_native_m': 10., 'ground_surface_composite_m': .5,
              'assumptions': ['Solid exterior mass models. Facades and roof materials are generated.',
                              'Foundations and thermal/radar materials are uncalibrated.',
                              'Roads are generated surface materials. Bridge/tunnel and raised layers require elevation data.']}
    dump(directory / 'generation.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--osm', type=Path, required=True, help='offline OSM XML extract with buildings and roads')
    parser.add_argument('--data-root', type=Path, required=True, help='folder containing the Taipei DSM30 and Sentinel-2B rasters')
    parser.add_argument('--out', type=Path, required=True, help='new output directory')
    parser.add_argument('--center', nargs=2, type=float, default=(121.6138, 25.0558), metavar=('LON', 'LAT'))
    parser.add_argument('--span', type=int, default=768, help='working span in metres (256..1024, multiple of 32)')
    parser.add_argument('--building-cell', type=float, default=2., help='fixed building occupancy cell size in metres')
    parser.add_argument('--max-buildings', type=int, default=125)
    parser.add_argument('--dtm-ellipsoidal', type=Path, help='optional bare-earth GeoTIFF, metre CRS and WGS84 ellipsoidal heights')
    args = parser.parse_args()
    if not 256 <= args.span <= 1024 or args.span % 32:
        parser.error('--span must be in [256, 1024] and a multiple of 32')
    if not .5 <= args.building_cell <= 8. or not 1 <= args.max_buildings <= 250:
        parser.error('building cell or object budget is outside the supported range')
    if not 121.10 <= args.center[0] <= 122.44 or not 24.51 <= args.center[1] <= 25.64:
        parser.error('--center must be within the Taipei raster region')
    args.out = args.out.resolve()
    if args.out.exists():
        parser.error('--out must be a new directory')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.'+args.out.name+'-', dir=args.out.parent))
    try:
        report = prepare(args, stage)
        stage.rename(args.out)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    print(f'{report["building_count"]} buildings; {sum(report["road_width_sources"].values())} road/path ways; {args.out}')


if __name__ == '__main__':
    main()
