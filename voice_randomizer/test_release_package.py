"""Validate the public ZIP as an extracted runtime, without starting the game."""
from pathlib import Path
import configparser
import ctypes as C
import hashlib
import json
import zipfile
from package_release import HERE, VERSION, README_NAME, EDITOR_NAME


def main():
    dist = HERE/'dist'/f'v{VERSION}'
    archive = dist/f'Sora1st-Voice-Randomizer-v{VERSION}.zip'
    destination = HERE/'build'/f'release_smoke_v{VERSION}'
    destination.mkdir(exist_ok=False)
    info = json.loads((dist/'release_summary.json').read_text(encoding='utf-8'))
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == info['archive_sha256']
    with zipfile.ZipFile(archive) as packed:
        assert {name.split('/')[0] for name in packed.namelist()} == {'xinput1_4.dll', 'voice_randomizer', README_NAME}
        assert len(packed.namelist()) == 832
        for name in packed.namelist():
            path = (destination/name).resolve()
            assert path.is_relative_to(destination.resolve())
        packed.extractall(destination)
    checksums = (dist/'SHA256SUMS.txt').read_text(encoding='utf-8').splitlines()
    for row in checksums:
        digest, name = row.split('  ', 1)
        assert hashlib.sha256((destination/name).read_bytes()).hexdigest() == digest, name
    ini = destination/'voice_randomizer/voice_randomizer.ini'
    data = ini.read_bytes()
    editor = destination/'voice_randomizer'/EDITOR_NAME
    assert hashlib.sha256(editor.read_bytes()).hexdigest() == info['editor_sha256']
    assert editor.parent == ini.parent and editor.stat().st_size < 1024*1024
    assert data.startswith(b'\xff\xfe')
    text = data.decode('utf-16')
    assert '保底开关' in text and 'Guarantee switch' in text
    source_license = (HERE/'third_party/minhook-1.3.4/LICENSE.txt').read_text(encoding='utf-8')
    readme = (destination/README_NAME).read_text(encoding='utf-8-sig')
    assert readme.endswith(source_license.rstrip()+'\n')
    assert readme.index('【中文】') < readme.index('【English】') < readme.index('【MinHook 许可证 / MinHook License】')
    assert 'Mini_Lom233' in readme and 'MinHook' not in text
    config = configparser.ConfigParser()
    config.read_string(text)
    expected = {'Randomizer': {'Enabled', 'Mode', 'OldPercent', 'GuaranteeTarget', 'GuaranteeEnabled',
                              'CapturingRadianceEnabled', 'ReplacementSet', 'LogCache'},
                'Overlay': {'Enabled', 'LeftPercent', 'CenterYPercent', 'HeightPercent', 'ScrollMs'}}
    assert set(config.sections()) == set(expected)
    k = C.WinDLL('kernel32', use_last_error=True)
    get = k.GetPrivateProfileStringW
    get.argtypes = [C.c_wchar_p, C.c_wchar_p, C.c_wchar_p, C.c_wchar_p, C.c_uint, C.c_wchar_p]
    get.restype = C.c_uint
    for section, keys in expected.items():
        assert set(config[section]) == {key.lower() for key in keys}
        for key in keys:
            value = C.create_unicode_buffer(100)
            assert get(section, key, 'MISSING', value, len(value), str(ini))
            assert value.value == config[section][key], (section, key, value.value)
    proxy = C.WinDLL(str(destination/'xinput1_4.dll'))
    system_path = C.create_unicode_buffer(32768)
    k.GetSystemDirectoryW.argtypes = [C.c_wchar_p, C.c_uint]
    assert k.GetSystemDirectoryW(system_path, len(system_path))
    system = C.WinDLL(str(Path(system_path.value)/'xinput1_4.dll'))
    for name, types, args in [
        ('XInputGetState', [C.c_uint32, C.c_void_p], [999, C.create_string_buffer(64)]),
        ('XInputGetCapabilities', [C.c_uint32, C.c_uint32, C.c_void_p], [999, 0, C.create_string_buffer(128)]),
        ('XInputGetBatteryInformation', [C.c_uint32, C.c_ubyte, C.c_void_p], [999, 0, C.create_string_buffer(32)]),
    ]:
        original, forwarded = getattr(system, name), getattr(proxy, name)
        original.argtypes = forwarded.argtypes = types
        original.restype = forwarded.restype = C.c_uint32
        assert original(*args) == forwarded(*args), name
    result = dict(version=VERSION, extracted_files=len(checksums), zip_roots=['xinput1_4.dll', 'voice_randomizer', README_NAME],
                  all_extracted_hashes_match=True, windows_ini_api_options_checked=13,
                  unicode_comments_and_readme_license=True, extracted_proxy_load_and_forwarding=True,
                  game_launched=False)
    (dist/'package_validation.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
