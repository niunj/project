#!/usr/bin/env python3
"""Embed naked-eye HYG v4.0 stars from the archived gzip CSV."""
import csv
import gzip
from pathlib import Path
import sys


with gzip.open(sys.argv[1], "rt", newline="") as catalog:
    stars = [star for star in csv.DictReader(catalog)
             if star["id"] != "0" and float(star["mag"]) <= 6.0]

output = Path(__file__).with_name("BrightStars.inc")
with output.open("w", encoding="ascii") as destination:
    destination.write("// HYG v4.0 subset, CC BY-SA 4.0. See README.md.\n")
    for star in stars:
        # Quantize position to centidegrees, and photometry to centimagnitudes.
        ra = round(float(star["ra"]) * 1500) % 36000
        dec = round(float(star["dec"]) * 100)
        magnitude = round(float(star["mag"]) * 100)
        color = round(float(star["ci"] or 0.65) * 100)
        destination.write(f"{{{ra},{dec},{magnitude},{color}}},\n")
