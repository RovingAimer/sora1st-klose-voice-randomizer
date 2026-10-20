"""Offline native build. Does not load the output DLL or run the game.
Third-party reconstructed module: provenance in src/VoiceRandomizer.h.
Detours fixed upstream 9764cebcb1a75940e68fa83d6730ffaf0f669401; licenses preserved.
MSVC/SDK discovery adapted from the existing local MIT loader build helper.
Copyright (c) 2025 Hinkiii; build additions Copyright (c) 2026 DialogueOutline contributors.
The MIT notice for this helper adaptation is in src/PROVENANCE.md.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
ROOT = Path(__file__).resolve().parent
OUTPUT = ROOT / 'build'
DETOURS = '9764cebcb1a75940e68fa83d6730ffaf0f669401'
DETOURS_OBJECTS = ('detours', 'modules', 'disasm', 'image', 'creatwth', 'disolx86',
                   'disolx64', 'disolia64', 'disolarm', 'disolarm64')
DETOURS_SCOPE = (
    b'The DETOURS_LICENSE resource contains the complete license for Microsoft Detours only.\r\n'
    b'It does not license the third-party reconstructed module source or original assets.\r\n')


def notice_resource(notice):
    """Standard Win32 .res input for LINK/cvtres; no resource compiler required."""
    # Required empty resource header, followed by named RT_RCDATA (10) entries.
    result = struct.pack('<IIHHHHIHHII', 0, 32, 0xffff, 0, 0xffff, 0, 0, 0, 0, 0, 0)
    for name, data in (('DETOURS_LICENSE', notice), ('DETOURS_LICENSE_SCOPE', DETOURS_SCOPE)):
        identifier = struct.pack('<HH', 0xffff, 10) + (name + '\0').encode('utf-16le')
        identifier += b'\0' * (-len(identifier) % 4)
        header = struct.pack('<II', len(data), 8 + len(identifier) + 16)
        header += identifier + struct.pack('<IHHII', 0, 0x30, 0, 0, 0)
        result += header + data + b'\0' * (-len(data) % 4)
    return result


def extract_notice(dll, name, resource_type=10, language=0):
    """Read a named RCDATA resource directly from PE bytes, without loading it."""
    pe = struct.unpack_from('<I', dll, 0x3c)[0]
    optional = pe + 24
    if dll[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', dll, optional)[0] != 0x20b:
        raise ValueError('Expected a PE32+ DLL')
    section_count = struct.unpack_from('<H', dll, pe + 6)[0]
    section_table = optional + struct.unpack_from('<H', dll, pe + 20)[0]
    def file_offset(rva):
        for index in range(section_count):
            virtual_size, address, raw_size, raw = struct.unpack_from('<IIII', dll, section_table + index * 40 + 8)
            if address <= rva < address + max(virtual_size, raw_size):
                return raw + rva - address
        raise ValueError('Resource RVA has no section')
    resource_rva = struct.unpack_from('<I', dll, optional + 112 + 2 * 8)[0]
    if not resource_rva:
        raise ValueError('DLL has no resource directory')
    base = file_offset(resource_rva)
    def entries(relative):
        named, numbered = struct.unpack_from('<HH', dll, base + relative + 12)
        for index in range(named + numbered):
            key, target = struct.unpack_from('<II', dll, base + relative + 16 + index * 8)
            if key & 0x80000000:
                offset = base + (key & 0x7fffffff)
                length = struct.unpack_from('<H', dll, offset)[0]
                key = dll[offset+2:offset+2+length*2].decode('utf-16le')
            yield key, target
    def directory(relative, key):
        target = dict(entries(relative))[key]
        if not target & 0x80000000:
            raise ValueError('Expected a resource directory')
        return target & 0x7fffffff
    languages = dict(entries(directory(directory(0, resource_type), name)))
    data_entry = languages[language] if language is not None else next(iter(languages.values()))
    if data_entry & 0x80000000:
        raise ValueError('Expected a resource data entry')
    rva, size = struct.unpack_from('<II', dll, base + data_entry)
    offset = file_offset(rva)
    return dll[offset:offset+size]

def compiler_environment(sdk_root=None):
    finder = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / \
        'Microsoft Visual Studio/Installer/vswhere.exe'
    install = subprocess.check_output([str(finder), '-latest', '-products', '*', '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'],
        text=True).strip()
    if not install:
        raise RuntimeError('MSVC x64 Build Tools and Windows SDK are required')
    environment = os.environ.copy()
    environment['PATH'] = environment.get('PATH', '').replace('"', '')
    environment.update(TEMP=str(OUTPUT), TMP=str(OUTPUT), VSLANG='1033', VSCMD_SKIP_SENDTELEMETRY='1')
    setup = Path(install) / 'VC/Auxiliary/Build/vcvars64.bat'
    command = OUTPUT / 'toolchain_env.cmd'
    command.write_text(f'@echo off\ncall "{setup}" >nul\nif errorlevel 1 exit /b 1\nset\n', encoding='ascii')
    result = subprocess.run(['cmd.exe', '/d', '/c', str(command)], cwd=OUTPUT, env=environment,
        capture_output=True, text=True, encoding='mbcs', check=True)
    for line in result.stdout.splitlines():
        key, separator, value = line.partition('=')
        if separator and key:
            environment[key] = value
    environment.update(TEMP=str(OUTPUT), TMP=str(OUTPUT), VSLANG='1033')
    if sdk_root:
        sdk = sdk_root.resolve()
        versions = sorted((sdk / 'Include').glob('10.*'),
                          key=lambda path: tuple(int(part) for part in path.name.split('.')))
        if not versions:
            raise RuntimeError('SDK root must contain Include/10.x')
        include = versions[-1]
        libraries = sdk / 'Lib' / include.name
        if not libraries.is_dir():
            libraries = sdk  # Workspace SDK stores um/x64 and ucrt/x64 here.
        for path in (include / 'um/windows.h', include / 'ucrt/stdio.h',
                     libraries / 'um/x64/kernel32.lib', libraries / 'ucrt/x64/libucrt.lib'):
            if not path.is_file():
                raise RuntimeError('Incomplete SDK: ' + str(path))
        environment['INCLUDE'] = ';'.join(str(include / part) for part in
            ('ucrt', 'shared', 'um', 'winrt')) + ';' + environment.get('INCLUDE', '')
        environment['LIB'] = ';'.join(str(libraries / part / 'x64') for part in
            ('um', 'ucrt')) + ';' + environment.get('LIB', '')
        environment['WindowsSDKVersion'] = include.name + chr(92)
    return environment


def source_git_info():
    """Optional provenance; an extracted source archive needs no Git checkout."""
    unknown = dict(commit=None, dirty=None)
    repository = ROOT.parent.parent if ROOT.parent.name == 'compat' else ROOT.parent
    if not (repository / '.git').exists():
        return unknown
    try:
        command = ['git', '-c', 'safe.directory=' + repository.as_posix(), '-C', str(ROOT)]
        top = subprocess.check_output(command + ['rev-parse', '--show-toplevel'],
            text=True, stderr=subprocess.DEVNULL).strip()
        if Path(top).resolve() != repository.resolve():
            return unknown
        commit = subprocess.check_output(command + ['rev-parse', 'HEAD'],
            text=True, stderr=subprocess.DEVNULL).strip()
        dirty = bool(subprocess.check_output(command + ['status', '--porcelain', '--', '.'],
            text=True, stderr=subprocess.DEVNULL).strip())
        return dict(commit=commit, dirty=dirty)
    except (OSError, subprocess.CalledProcessError):
        return unknown


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-root', type=Path)
    parser.add_argument('--editor', action='store_true', help='Also compile the settings editor; never run it or upstream tests')
    args = parser.parse_args()
    OUTPUT.mkdir(exist_ok=True)
    (OUTPUT/'report.json').unlink(missing_ok=True)
    environment = compiler_environment(args.sdk_root)
    compiler, librarian, linker, dumper = [shutil.which(name, path=environment['PATH'])
        for name in ('cl.exe','lib.exe','link.exe','dumpbin.exe')]
    if not all((compiler,librarian,linker,dumper)):
        raise RuntimeError('Incomplete MSVC toolchain')
    commands = []
    def run(arguments, log):
        arguments = [str(value) for value in arguments]
        commands.append(arguments)
        (OUTPUT/'commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
        result = subprocess.run(arguments,cwd=OUTPUT,env=environment,capture_output=True,
            text=True,encoding='utf-8',errors='replace')
        (OUTPUT/log).write_text(result.stdout+result.stderr,encoding='utf-8')
        if result.returncode:
            raise RuntimeError(f'Command failed ({result.returncode}); see {OUTPUT/log}')
    detours = ROOT/'external/detours/src'
    common = ['/nologo','/c','/O2','/MT','/W4','/WX','/EHsc','/Brepro']
    objects=[]
    for name in DETOURS_OBJECTS:
        obj=OUTPUT/(name+'.obj')
        run([compiler,*common,'/DWIN32_LEAN_AND_MEAN','/DDETOUR_DEBUG=0',
            '/Fo'+str(obj),detours/(name+'.cpp')],name+'.log')
        objects.append(obj)
    library=OUTPUT/'detours.lib'
    run([librarian,'/NOLOGO','/Brepro','/OUT:'+str(library),*objects],'library.log')
    objects=[]
    for name in ('module','rules','overlay'):
        obj=OUTPUT/(name+'.obj')
        run([compiler,*common,'/std:c++17','/utf-8','/I'+str(detours),'/Fo'+str(obj),
            ROOT/'src'/(name+'.cpp')],name+'.log')
        objects.append(obj)
    notice = (ROOT/'external/detours/LICENSE').read_bytes()
    resource = OUTPUT/'notices.res'
    resource.write_bytes(notice_resource(notice))
    artifact=OUTPUT/'VoiceRandomizer.dll'
    run([linker,'/NOLOGO','/DLL','/MACHINE:X64','/INCREMENTAL:NO','/DYNAMICBASE','/Brepro',
        '/NXCOMPAT','/OPT:REF','/OUT:'+str(artifact),*objects,library,resource,
        'kernel32.lib','bcrypt.lib','ole32.lib','windowscodecs.lib','d3d11.lib','dxgi.lib','d3dcompiler.lib'], 'link.log')
    run([dumper,'/NOLOGO','/HEADERS','/EXPORTS','/IMPORTS','/DEPENDENTS',artifact],'pe_inspection.txt')
    inspection=(OUTPUT/'pe_inspection.txt').read_text(encoding='utf-8')
    exports=re.findall(r'^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\w+)\s*$',inspection,re.MULTILINE)
    if exports!=['SoraVoiceInitialize']:
        raise RuntimeError('Unexpected exports: '+repr(exports))
    dependencies=sorted(set(re.findall(r'^\s+([A-Za-z0-9_.-]+\.dll)\s*$',inspection,re.MULTILINE|re.IGNORECASE)))
    allowed={'kernel32.dll','bcrypt.dll','ole32.dll','windowscodecs.dll','d3d11.dll','dxgi.dll','d3dcompiler_47.dll'}
    if not dependencies or set(x.lower() for x in dependencies)-allowed:
        raise RuntimeError('Unexpected dependencies: '+repr(dependencies))
    dll = artifact.read_bytes()
    embedded_notices = {}
    for name, expected in (('DETOURS_LICENSE', notice), ('DETOURS_LICENSE_SCOPE', DETOURS_SCOPE)):
        extracted = extract_notice(dll, name)
        if extracted != expected:
            raise RuntimeError('PE RCDATA notice differs: '+name)
        (OUTPUT/(name+'.txt')).write_bytes(extracted)
        embedded_notices[name] = dict(type='RCDATA', language=0, bytes=len(extracted),
            sha256=hashlib.sha256(extracted).hexdigest(), exact_input_match=True)
    inputs=[ROOT/'build.py',ROOT/'sora_mod.ini',*sorted((ROOT/'src').glob('*')),
            *sorted((ROOT/'external/detours').rglob('*'))]
    inputs=[path for path in inputs if path.is_file()]
    hashes={path.relative_to(ROOT).as_posix():hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    banner=subprocess.run([compiler],env=environment,capture_output=True,text=True,
        encoding='utf-8',errors='replace')
    report=dict(artifact=str(artifact),dll_sha256=hashlib.sha256(dll).hexdigest(),
        dll_bytes=artifact.stat().st_size,compiler=compiler,compiler_version=banner.stderr.strip(),
        sdk_version=environment.get('WindowsSDKVersion'),sdk_root=str(args.sdk_root),
        include_paths=environment.get('INCLUDE'),library_paths=environment.get('LIB'),
        detours_commit=DETOURS,source_git=source_git_info(),inputs=hashes,commands=commands,
        build_flags=common,exports=exports,dependencies=dependencies,embedded_notices=embedded_notices,
        generated_inputs={'notices.res': hashlib.sha256(resource.read_bytes()).hexdigest()},
        dll_executed=False,game_runtime_verified=False,game_directory_written=False)
    if args.editor:
        fork = ROOT.parent.parent
        author = fork/'voice_randomizer'
        editor_inputs = [author/'src'/name for name in
            ('config_editor.cpp', 'editor_config.h', 'config_editor.rc', 'config_editor.manifest')]
        template = author/'release/config_template.ini'
        editor_inputs += [template, ROOT/'build.py']
        if not all(path.is_file() for path in editor_inputs):
            raise RuntimeError('--editor requires the full fork layout with voice_randomizer sources')
        # SDK headers/libraries may be relocated while native tools remain installed.
        kits = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'))/'Windows Kits/10/bin'
        sdk_version = environment.get('WindowsSDKVersion', '').strip('\\/')
        candidates = [kits/sdk_version/'x64', *sorted(kits.glob('10.*/x64'), reverse=True)]
        native = next((path for path in candidates if (path/'rc.exe').is_file() and (path/'mt.exe').is_file()), None)
        rc = str(native/'rc.exe') if native else shutil.which('rc.exe', path=environment['PATH'])
        mt = str(native/'mt.exe') if native else shutil.which('mt.exe', path=environment['PATH'])
        if not rc or not mt:
            raise RuntimeError('--editor requires existing Windows SDK rc.exe and mt.exe')
        environment['PATH'] = str(Path(mt).parent) + os.pathsep + environment['PATH']
        text = template.read_text(encoding='utf-8-sig').replace('\r\n', '\n').rstrip()+'\n'
        template_bytes = b'\xff\xfe'+text.replace('\n', '\r\n').encode('utf-16le')
        generated_template = OUTPUT/'editor_template.ini'
        generated_template.write_bytes(template_bytes)
        generated_rc = OUTPUT/'config_editor.rc'
        resource_text = (author/'src/config_editor.rc').read_text(encoding='utf-8-sig')
        resource_text, replacements = re.subn(r'^101 RCDATA .*$',
            lambda match: '101 RCDATA "'+generated_template.as_posix()+'"', resource_text, count=1, flags=re.MULTILINE)
        if replacements != 1:
            raise RuntimeError('Editor RCDATA template declaration not found')
        generated_rc.write_text(resource_text, encoding='utf-8')
        editor_res = OUTPUT/'config_editor.res'
        run([rc, '/nologo', '/fo', editor_res, generated_rc], 'editor_resource.log')
        editor_artifact = OUTPUT/'KloseVoiceSettings.exe'
        run([compiler, '/nologo', '/std:c++17', '/EHsc', '/O2', '/MT', '/W4', '/utf-8',
             '/DUNICODE', '/D_UNICODE', '/Brepro', '/Fo'+str(OUTPUT/'config_editor.obj'),
             author/'src/config_editor.cpp', editor_res, '/link', '/SUBSYSTEM:WINDOWS',
             '/INCREMENTAL:NO', '/MANIFEST:EMBED', '/MANIFESTINPUT:'+str(author/'src/config_editor.manifest'),
             '/OUT:'+str(editor_artifact)], 'editor_compile.log')
        run([dumper, '/NOLOGO', '/HEADERS', '/IMPORTS', '/DEPENDENTS', editor_artifact], 'editor_pe_inspection.txt')
        run([mt, '-nologo', '-inputresource:'+str(editor_artifact)+';#1',
             '-out:'+str(OUTPUT/'editor_embedded.manifest')], 'editor_manifest.log')
        # Resource extraction reads PE bytes only; no executable is loaded.
        editor_bytes = editor_artifact.read_bytes()
        embedded_template = extract_notice(editor_bytes, 101, language=None)
        if embedded_template != template_bytes:
            raise RuntimeError('Editor embedded configuration differs from the generated template')
        version = extract_notice(editor_bytes, 1, resource_type=16, language=None)
        if '1.2.0'.encode('utf-16le') not in version:
            raise RuntimeError('Editor VERSIONINFO was not preserved')
        pe = struct.unpack_from('<I', editor_bytes, 0x3c)[0]
        if struct.unpack_from('<H', editor_bytes, pe+4)[0] != 0x8664 or struct.unpack_from('<H', editor_bytes, pe+24+68)[0] != 2:
            raise RuntimeError('Editor must be an AMD64 Windows GUI executable')
        manifest = (OUTPUT/'editor_embedded.manifest').read_text(encoding='utf-8-sig')
        if not all(value in manifest for value in ('asInvoker', 'PerMonitorV2', 'Microsoft.Windows.Common-Controls')):
            raise RuntimeError('Editor manifest is missing required settings')
        report['editor'] = dict(artifact=str(editor_artifact), sha256=hashlib.sha256(editor_bytes).hexdigest(),
            bytes=len(editor_bytes), compiler=compiler, compiler_version=banner.stderr.strip(),
            sdk_version=environment.get('WindowsSDKVersion'), sdk_root=str(args.sdk_root),
            resource_compiler=rc, manifest_tool=mt,
            machine='AMD64', subsystem='Windows GUI',
            dependencies=sorted(set(re.findall(r'^\s+([A-Za-z0-9_.-]+\.dll)\s*$',
                (OUTPUT/'editor_pe_inspection.txt').read_text(encoding='utf-8'), re.MULTILINE|re.IGNORECASE))),
            version_resource_sha256=hashlib.sha256(version).hexdigest(),
            inputs={path.relative_to(fork).as_posix():hashlib.sha256(path.read_bytes()).hexdigest() for path in editor_inputs},
            embedded_template_sha256=hashlib.sha256(embedded_template).hexdigest(),
            generated_inputs={path.name:hashlib.sha256(path.read_bytes()).hexdigest()
                              for path in (generated_template, generated_rc, editor_res)},
            executed=False, upstream_tests_run=False)
    (OUTPUT/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({key:report[key] for key in ('artifact','dll_sha256','dll_bytes','exports','dependencies')}))
if __name__=='__main__':
    main()
