#!/usr/bin/env python3
"""Separate the physical antenna mesh for CG-57 display animation."""
import argparse
from pathlib import Path
import shutil


def read_node(lines, start):
    end = start + 1
    while not lines[end].startswith('kids '):
        end += 1
    children = []
    cursor = end + 1
    for _ in range(int(lines[end].split()[1])):
        child, cursor = read_node(lines, cursor)
        children.append(child)
    return (lines[start:end], children), cursor


def partition(node):
    attrs, children = node
    if 'name "SPS-49-Antenna"' in attrs:
        return None, node
    pairs = [partition(child) for child in children]
    hull = (attrs, [body for body, _ in pairs if body is not None])
    isolated = [aerial for _, aerial in pairs if aerial is not None]
    return hull, ((attrs, isolated) if isolated else None)


def write_node(stream, node):
    attrs, children = node
    stream.write('\n'.join(attrs) + f'\nkids {len(children)}\n')
    for child in children:
        write_node(stream, child)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    output = parser.parse_args().output
    source = Path(__file__).resolve().parents[2] / 'models' / 'CG-57' / 'source'
    lines = (source/'CG-57.ac').read_text(encoding='ascii').splitlines()
    start = lines.index('OBJECT world')
    model, _ = read_node(lines, start)
    hull, antenna = partition(model)
    if antenna is None:
        raise ValueError('named CG-57 antenna not present in the bundled asset')
    output.mkdir(parents=True, exist_ok=True)
    for name, node in (('hull', hull), ('antenna', antenna)):
        with (output/f'{name}.ac').open('w', encoding='ascii') as stream:
            stream.write('\n'.join(lines[:start])+'\n')
            write_node(stream, node)
    for texture in source.glob('*.png'):
        shutil.copy2(texture, output/texture.name)


if __name__ == '__main__':
    main()
