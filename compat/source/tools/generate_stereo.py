"""Build private stereo assets from the user's original voice PAC and old WAVs.

Uses only Python's standard library. Never extracts or writes into game/input
directories. Audio rights remain separate from the module source license.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import wave
import zipfile

COMPAT = Path(__file__).resolve().parents[2]
OLD_PREFIX = 'voice/wav/voice_randomizer/assets/old/'
ASSET_PREFIX = 'voice/wav/voice_randomizer/assets/'


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def voice_index():
    path = COMPAT / 'source/src/voice_index.inc'
    rows = re.findall(r'\{"([a-z0-9_]+)",\s*(\d+)u\}', path.read_text(encoding='utf-8'))
    if len(rows) != 827 or len({name for name, _ in rows}) != 827:
        raise ValueError('Expected the 827-row voice index (824 assets + 3 UI-only)')
    return [(name, int(ident)) for name, ident in rows[:824]], file_sha256(path)


def pac_index(path, names):
    size = path.stat().st_size
    with path.open('rb') as source:
        magic, count, header_size, _ = struct.unpack('<4sIII', source.read(16))
        if magic != b'FPAC' or not 16 + count * 32 <= header_size <= min(size, 64 * 1024 * 1024):
            raise ValueError('Invalid FPAC index')
        source.seek(0)
        header = source.read(header_size)
    entries = {}
    for i in range(count):
        _, name_at, length, offset = struct.unpack_from('<4Q', header, 16 + i * 32)
        if name_at >= header_size or offset + length > size:
            raise ValueError('FPAC entry outside the archive')
        name = header[name_at:header.index(0, name_at)].decode('utf-8')
        if name in names:
            if name in entries:
                raise ValueError('Duplicate PAC resource: ' + name)
            entries[name] = (offset, length)
    if names != entries.keys():
        raise ValueError('Missing PAC resources: ' + ', '.join(sorted(names - entries.keys())))
    return entries


def pcm_wave(data, channels, label):
    if len(data) < 44 or data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError(label + ': expected RIFF WAVE')
    if struct.unpack_from('<I', data, 4)[0] + 8 != len(data):
        raise ValueError(label + ': RIFF length mismatch')
    offset = 12
    fmt = pcm = None
    while offset + 8 <= len(data):
        kind, length = struct.unpack_from('<4sI', data, offset)
        start = offset + 8
        if start + length > len(data):
            raise ValueError(label + ': truncated WAV chunk')
        if kind == b'fmt ':
            if fmt is not None or length < 16:
                raise ValueError(label + ': invalid/duplicate fmt chunk')
            fmt = struct.unpack_from('<HHIIHH', data, start)
        elif kind == b'data':
            if pcm is not None:
                raise ValueError(label + ': duplicate data chunk')
            pcm = data[start:start + length]
        offset = start + length + (length & 1)
    expected = (1, channels, 48000, 96000 * channels, 2 * channels, 16)
    if offset != len(data) or fmt != expected or not pcm or len(pcm) % (2 * channels):
        raise ValueError(label + ': expected complete PCM %dch 48000Hz 16bit WAV' % channels)
    return pcm, {'encoding': 'PCM', 'channels': channels, 'rate': 48000,
                 'bits': 16, 'frames': len(pcm) // (2 * channels),
                 'bytes': len(data), 'sha256': sha256(data)}


def stereo_wave(left, right):
    length = max(len(left), len(right))
    left, right = left.ljust(length, b'\0'), right.ljust(length, b'\0')
    stereo = bytearray(length * 2)
    stereo[0::4], stereo[1::4] = left[0::2], left[1::2]
    stereo[2::4], stereo[3::4] = right[0::2], right[1::2]
    result = io.BytesIO()
    with wave.open(result, 'wb') as output:
        output.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
        output.writeframes(stereo)
    data = result.getvalue()
    pcm, meta = pcm_wave(data, 2, 'generated stereo')
    if (pcm[0::4] != left[0::2] or pcm[1::4] != left[1::2]
            or pcm[2::4] != right[0::2] or pcm[3::4] != right[1::2]):
        raise ValueError('Generated stereo channels differ from padded inputs')
    return data, meta


def build(args):
    out = args.out.resolve()
    if out == COMPAT or not out.is_relative_to(COMPAT):
        raise ValueError('--out must be a subdirectory inside this compat workspace')
    rows, index_sha = voice_index()
    names = {name for name, _ in rows}
    entries = pac_index(args.voice_pac, {'voice/wav/' + name + '.wav' for name in names})
    reference = None
    if args.reference_index:
        reference = {row['name']: row for row in json.loads(args.reference_index.read_text(encoding='utf-8'))['rows']}
        if not names <= reference.keys():
            raise ValueError('Reference voice index is missing target assets')
    old_zip = zipfile.ZipFile(args.old_archive) if args.old_archive else None
    try:
        if old_zip:
            members = [item.filename for item in old_zip.infolist()
                       if item.filename.startswith(OLD_PREFIX) and item.filename.endswith('.wav')]
            expected = {OLD_PREFIX + name + '.wav' for name in names}
            if len(members) != 824 or set(members) != expected:
                raise ValueError('Old archive must contain exactly the 824 indexed old WAVs')
        elif {item.stem for item in args.old_root.glob('*.wav')} != names:
            raise ValueError('--old-root must directly contain exactly the 824 indexed WAVs')
        out.mkdir(parents=True, exist_ok=True)
        archive = out / 'VoiceRandomizer-stereo-assets.zip'
        partial = out / 'VoiceRandomizer-stereo-assets.zip.partial'
        manifest = {'private_local_assets_only': True,
                    'rights': 'Game and legacy voice assets retain their original rights; do not upload with source.',
                    'voice_pac': str(args.voice_pac.resolve()), 'voice_pac_bytes': args.voice_pac.stat().st_size,
                    'old_input': str((args.old_archive or args.old_root).resolve()),
                    'old_archive_sha256': file_sha256(args.old_archive) if args.old_archive else None,
                    'index_sha256': index_sha, 'asset_pairs': 824, 'members': 1648,
                    'reference_index': str(args.reference_index.resolve()) if reference is not None else None,
                    'reference_match_count': 0, 'rows': []}
        with args.voice_pac.open('rb') as pac, zipfile.ZipFile(partial, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
            for number, (name, ident) in enumerate(rows, 1):
                offset, length = entries['voice/wav/' + name + '.wav']
                pac.seek(offset)
                new, new_meta = pcm_wave(pac.read(length), 1, name + ' new')
                raw_old = old_zip.read(OLD_PREFIX + name + '.wav') if old_zip else (args.old_root / (name + '.wav')).read_bytes()
                old, old_meta = pcm_wave(raw_old, 1, name + ' old')
                del raw_old
                if reference is not None:
                    expected = reference[name]
                    if (expected['sha256'], expected['bytes'], expected['voice_id']) != (old_meta['sha256'], old_meta['bytes'], ident):
                        raise ValueError(name + ': old bytes/hash/voice ID differ from reference index')
                    manifest['reference_match_count'] += 1
                row = {'name': name, 'voice_id': ident, 'new': new_meta, 'old': old_meta, 'outputs': []}
                for direction, left, right in (('stereo_lr', new, old), ('stereo_rl', old, new)):
                    data, meta = stereo_wave(left, right)
                    member = ASSET_PREFIX + direction + '/' + name + '.wav'
                    output.writestr(member, data)
                    row['outputs'].append({'member': member, **meta, 'padded_channels_verified': True})
                    del data
                manifest['rows'].append(row)
                if number % 100 == 0 or number == 824:
                    print('Generated and channel-verified %d/824 pairs' % number, flush=True)
        with zipfile.ZipFile(partial) as check:
            expected = {item['member']: item for row in manifest['rows'] for item in row['outputs']}
            if len(check.infolist()) != 1648 or set(check.namelist()) != expected.keys():
                raise ValueError('Output ZIP member list mismatch')
            for member, meta in expected.items():
                # ZipFile.read also checks the member CRC; verify what will actually ship.
                data = check.read(member)
                _, actual = pcm_wave(data, 2, member)
                if actual != {key: meta[key] for key in actual}:
                    raise ValueError(member + ': ZIP readback differs from validated audio')
        manifest.update(zip_name=archive.name, zip_bytes=partial.stat().st_size,
                        zip_sha256=file_sha256(partial), zip_crc_readback_verified=True,
                        uncompressed_wav_bytes=sum(item['bytes'] for row in manifest['rows'] for item in row['outputs']))
        partial.replace(archive)
        manifest_path = out / 'stereo-assets-manifest.json'
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({key: manifest[key] for key in ('zip_name', 'zip_bytes', 'zip_sha256', 'uncompressed_wav_bytes', 'reference_match_count')}, indent=2))
        print('Manifest:', manifest_path)
    finally:
        if old_zip:
            old_zip.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--voice-pac', type=Path, required=True)
    old = parser.add_mutually_exclusive_group(required=True)
    old.add_argument('--old-archive', type=Path)
    old.add_argument('--old-root', type=Path)
    parser.add_argument('--out', type=Path, default=COMPAT / 'dist/20261020-stereo')
    parser.add_argument('--reference-index', type=Path, help='Optional original voice-index.json; fail if old hashes/IDs differ')
    args = parser.parse_args()
    try:
        build(args)
    except (OSError, ValueError, KeyError, EOFError, struct.error, zipfile.BadZipFile) as error:
        parser.exit(1, 'Stereo preparation failed: %s\n' % error)


if __name__ == '__main__':
    main()
