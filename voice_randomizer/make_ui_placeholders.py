"""Create four solid 64x64 RGBA PNGs. Existing user images are never overwritten."""
from pathlib import Path
import struct
import zlib
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parent / 'assets/ui')
root = parser.parse_args().output
root.mkdir(parents=True, exist_ok=True)
colors = {'avatar_new.png': (57, 167, 255, 255),
          'avatar_old.png': (249, 115, 22, 255),
          'badge_radiance.png': (245, 213, 71, 255),
          'badge_wai.png': (180, 92, 255, 255)}
def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
for name, rgba in colors.items():
    path = root / name
    if path.exists():
        print('Kept existing:', name)
        continue
    pixels = (b'\0' + bytes(rgba)*64)*64
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 64, 64, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))
    print('Created:', name, rgba)
