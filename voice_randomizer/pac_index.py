"""Read an FPAC file index without extracting or modifying game data."""
import struct


def pac_index(path):
    with path.open('rb') as f:
        magic, count, header_size, version = struct.unpack('<4sIII', f.read(16))
        assert magic == b'FPAC' and 16 + count * 32 <= header_size <= path.stat().st_size
        f.seek(0)
        header = f.read(header_size)
    entries = {}
    for i in range(count):
        name_hash, name_offset, size, offset = struct.unpack_from('<QQQQ', header, 16 + i * 32)
        assert name_offset < header_size and offset + size <= path.stat().st_size
        name = header[name_offset:header.index(0, name_offset)].decode('utf-8')
        assert name not in entries
        entries[name] = dict(size=size, offset=offset, hash=name_hash)
    return entries
