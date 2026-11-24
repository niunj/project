"""Embed NASA SVS CGI Moon Kit LROC albedo and optional LOLA topography."""
import argparse
from pathlib import Path
from PIL import Image


def write_texture(path, source, pixels):
    with path.open("w", encoding="ascii") as output:
        output.write(f"// NASA SVS CGI Moon Kit 4720, {source}.\n")
        for offset in range(0, len(pixels), 32):
            output.write(",".join(map(str, pixels[offset:offset + 32])) + ",\n")


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--relief", type=Path, help="NASA ldem_4.tif in kilometers")
args = parser.parse_args()
root = Path(__file__).resolve().parent
size = (512, 256)
if args.relief:
    with Image.open(args.relief) as dem:
        if dem.size != (1440, 720) or dem.mode != "F":
            parser.error("--relief expects the 1440x720 floating-point LOLA map")
        heights = dem.resize(size, Image.Resampling.BILINEAR)
        pixels = bytes(round(255 * (min(11., max(-10., height)) + 10.) / 21.)
                       for height in heights.getdata())
    write_texture(root / "MoonRelief.inc", "LRO/LOLA heights, -10 to +11 km", pixels)
else:
    with Image.open(root / "lroc_color_poles_1k.jpg") as albedo:
        pixels = albedo.convert("L").resize(size, Image.Resampling.LANCZOS).tobytes()
    write_texture(root / "MoonAlbedo.inc", "LRO/LROC visualization albedo", pixels)
