"""Build and hash-verify the public runtime ZIP from an explicit file allowlist."""
from pathlib import Path
from collections import Counter
import configparser
import hashlib
import io
import json
import re
import shutil
import struct
import wave
import zipfile

HERE = Path(__file__).resolve().parent
VERSION = '1.2.0'
DLL_VERSION = '1.1.0'
EXE_HASH = '9585d3d993874d65156aba633c00b8a25faab3afde8953a8a183c80fb8f657ac'
DATE = '2026-10-10'
README_NAME = '克萝赛随机语音MOD.txt'
EDITOR_NAME = '克萝赛MOD设置.exe'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def release_ini():
    template = (HERE/'release/config_template.ini').read_text(encoding='utf-8')
    text = template.rstrip()+'\n'
    return text.replace('\n', '\r\n').encode('utf-16')


def release_readme():
    template = (HERE/'release/readme_template.txt').read_text(encoding='utf-8')
    license_text = (HERE/'third_party/minhook-1.3.4/LICENSE.txt').read_text(encoding='utf-8')
    text = template.rstrip()+'\n\n【MinHook 许可证 / MinHook License】\n\n'+license_text.rstrip()+'\n'
    return text.replace('\n', '\r\n').encode('utf-8-sig')


def main():
    stage = HERE / 'build' / f'release-v{VERSION}'
    dist = HERE / 'dist' / f'v{VERSION}'
    if stage.exists() and any(stage.iterdir()):
        raise SystemExit(f'Refusing to mix with an existing staging tree: {stage}')
    stage.mkdir(parents=True, exist_ok=True)
    dist.mkdir(parents=True, exist_ok=True)
    files = {}

    def put(name, data):
        assert name not in files and '..' not in Path(name).parts and not Path(name).is_absolute()
        path = stage / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        files[name] = sha(data)

    def copy(name, source):
        put(name, (HERE/source).read_bytes())

    dll = (HERE/'bin/xinput1_4.dll').read_bytes()
    assert f'INIT Voice Randomizer {DLL_VERSION};'.encode() in dll, 'Wrong binary version'
    editor = (HERE/'bin/KloseVoiceSettings.exe').read_bytes()
    assert editor[:2] == b'MZ' and len(editor) < 1024*1024, 'Missing or unexpectedly large native settings editor'
    manifest = json.loads((HERE/'assets/manifest.json').read_text(encoding='utf-8'))
    assert len(manifest) == len({x['name'] for x in manifest}) == 824
    original_ui = {}
    for filename in ('avatar_new.png', 'avatar_old.png', 'badge_radiance.png', 'badge_wai.png'):
        data = (HERE/'assets/ui'/filename).read_bytes()
        assert data[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack('>II', data[16:24]) == (64, 64)
        copy(f'voice_randomizer/assets/ui/{filename}', f'assets/ui/{filename}')
        original_ui[filename] = sha(data)
    for item in manifest:
        name = item['name']
        assert re.fullmatch(r'v\d{3}_[a-z0-9_]+', name)
        data = (HERE/'assets/old'/f'{name}.wav').read_bytes()
        assert sha(data) == item['old_sha256'], name
        with wave.open(io.BytesIO(data), 'rb') as wav:
            assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getcomptype()) == (1, 2, 48000, 'NONE')
            assert len(wav.readframes(wav.getnframes())) == wav.getnframes()*2
        put(f'voice_randomizer/assets/old/{name}.wav', data)
    put('xinput1_4.dll', dll)
    put('voice_randomizer/'+EDITOR_NAME, editor)
    put(README_NAME, release_readme())
    ini_data = release_ini()
    assert (HERE/'release/voice_randomizer.ini').read_bytes() == ini_data, 'Regenerate release INI before packaging'
    put('voice_randomizer/voice_randomizer.ini', ini_data)
    config = configparser.ConfigParser()
    config.read(stage/'voice_randomizer/voice_randomizer.ini', encoding='utf-16')
    assert config['Randomizer']['ReplacementSet'] == 'legacy' and config['Randomizer']['Mode'] == 'random'
    assert config['Randomizer']['OldPercent'] == '50' and config['Overlay']['CenterYPercent'] == '40'
    assert config['Randomizer']['GuaranteeTarget'] == 'new'
    assert config['Randomizer']['GuaranteeEnabled'] == config['Randomizer']['CapturingRadianceEnabled'] == '1'
    assert config['Randomizer']['LogCache'] == '0'
    info = dict(version=VERSION, dll_version=DLL_VERSION, release_date=DATE, exe_sha256=EXE_HASH, dll_sha256=sha(dll), editor_sha256=sha(editor), editor_bytes=len(editor),
                replacement_set='legacy', voice_count=824, history_voice_count=827,
                guarantee_target='new', guarantee_enabled=True, capturing_radiance_enabled=True,
                categories=dict(Counter(x['category'] for x in manifest)), ui_sha256=original_ui)
    checksums = ''.join(f'{digest}  {name}\n' for name, digest in sorted(files.items()))
    (dist/'SHA256SUMS.txt').write_text(checksums, encoding='utf-8')
    archive = dist / f'Sora1st-Voice-Randomizer-v{VERSION}.zip'
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for i, name in enumerate(sorted(files), 1):
            entry = zipfile.ZipInfo(name, (2026, 10, 10, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o644 << 16
            output.writestr(entry, (stage/name).read_bytes(), compresslevel=6)
            if i % 200 == 0:
                print(f'Packed {i}/{len(files)} files', flush=True)
    with zipfile.ZipFile(archive) as packed:
        assert set(packed.namelist()) == set(files)
        assert len([n for n in files if n.endswith('.wav')]) == 824
        assert len(files) == 832
        assert {name.split('/')[0] for name in files} == {'xinput1_4.dll', 'voice_randomizer', README_NAME}
        assert all(name in ('xinput1_4.dll', README_NAME, 'voice_randomizer/voice_randomizer.ini', 'voice_randomizer/'+EDITOR_NAME)
                   or name.startswith(('voice_randomizer/assets/old/', 'voice_randomizer/assets/ui/')) for name in files)
        assert not any(part in {'placeholder', 'tts_source', 'backups', 'logs', '__pycache__'} for name in files for part in Path(name).parts)
        assert not any((n.endswith('.exe') and n!='voice_randomizer/'+EDITOR_NAME) or n.endswith(('.log', '.pdb', '.obj', 'install_receipt.json')) for n in files)
        for name, digest in files.items():
            assert sha(packed.read(name)) == digest, name
    digest = sha(archive.read_bytes())
    (dist/(archive.name+'.sha256')).write_text(f'{digest}  {archive.name}\n', encoding='utf-8')
    summary = dict(info, archive=archive.name, archive_sha256=digest, archive_bytes=archive.stat().st_size,
                   files=len(files), zip_contents_verified=True, only_legacy_audio=True,
                   source_ui_unchanged=all(sha((HERE/'assets/ui'/name).read_bytes())==digest for name, digest in original_ui.items()))
    (dist/'release_summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    for name in ('README.md', 'CHANGELOG.md', '发布文案.md'):
        shutil.copyfile(HERE/'release'/name, dist/name)
    print(json.dumps(summary, ensure_ascii=False, indent=2), flush=True)


if __name__ == '__main__':
    main()
