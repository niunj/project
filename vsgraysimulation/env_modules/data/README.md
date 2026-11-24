Ocean extent comes from Natural Earth v5.1.2, ne_110m_ocean.geojson.
The data are public domain.
https://www.naturalearthdata.com/about/terms-of-use/
https://github.com/nvkelso/natural-earth-vector/blob/v5.1.2/geojson/ne_110m_ocean.geojson

bake_ocean.py rasterizes polygon centres into 2048 × 1024 north-first row spans.
The checked-in OceanSpans.inc is embedded at compilation. Runtime loading is
offline and does not require Python or GDAL.

This 1:110 million extent serves the far-field ocean. Local coastline and depth
come from the pinned voxel snapshot. Cloud weather is a deterministic procedural
field advected on the WGS84 surface. Atmospheric patterns are synthetic.

Moon visualization albedo is NASA SVS CGI Moon Kit 4720, 2019 LROC map.
Credit NASA's Scientific Visualization Studio / LRO / LROC.
https://svs.gsfc.nasa.gov/4720/
https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004720/lroc_color_poles_1k.jpg
NASA describes this as a visualization map with exposure/white-balance adjustments.
It is suitable for appearance, and is not calibrated spectral reflectance.

bake_moon.py generates the checked-in 512 × 256 embedded grayscale MoonAlbedo.inc.
The same tool embeds LRO/LOLA elevation from NASA SVS CGI Moon Kit's ldem_4.tif.
https://svs.gsfc.nasa.gov/vis/a000000/a004700/a004720/ldem_4.tif

`python3 bake_moon.py --relief ldem_4.tif` resamples the floating-point kilometer
heights to 512 × 256 and quantizes −10 to +11 km into MoonRelief.inc. The renderer
uses local slopes and a short terrain shadow estimate near the terminator.
Offline regeneration requires Pillow. Compilation and runtime require no image decoder.
The renderer projects the mean IAU lunar pole, assumes tidal locking for the
sub-observer longitude and uses a Lommel–Seeliger/diffuse approximation.
Lunar libration is beyond this approximation.

BrightStars.inc is an adapted subset of David Nash's HYG Database v4.0, licensed
under Creative Commons Attribution-ShareAlike 4.0 International.
https://github.com/astronexus/HYG-Database/tree/main/hyg/CURRENT
https://creativecommons.org/licenses/by-sa/4.0/

`python3 bake_stars.py hygdata_v40.csv.gz` retains stars with apparent visual
magnitude at most 6.0 and quantizes the J2000 positions, magnitude and B−V index.
The embedded subset works offline. The diffuse Milky Way, airglow and the night
exposure are visual approximations. The star positions use J2000 without
proper-motion or precession corrections.
Stellar tint uses a B−V temperature estimate and restrained blackbody colour.
https://arxiv.org/abs/1201.1809
