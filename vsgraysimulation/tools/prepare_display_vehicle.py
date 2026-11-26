#!/usr/bin/env python3
"""Convert the vehicle preview texture to a renderer-compatible PNG."""
import argparse
from pathlib import Path
import shutil

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1] / 'models' / 'Himars'
    args.output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / 'Himars.obj', args.output / 'Himars.obj')
    material = (source / 'Himars.mtl').read_text(encoding='ascii', errors='replace')
    (args.output / 'Himars.mtl').write_text(material.replace('Himars.tif', 'Himars.png'), encoding='ascii')
    with Image.open(source / 'Himars.tif') as image:
        image.convert('RGBA').save(args.output / 'Himars.png')


if __name__ == '__main__':
    main()
