#!/usr/bin/env python3
"""Build local installation and source ZIPs from reviewed manifests.

The original package path is required explicitly. This tool only reads it and
never launches the original binaries, the game, or the candidate module.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]
REPOSITORY = ROOT.parent
EXPECTED_GAME_SHA256 = "9585d3d993874d65156aba633c00b8a25faab3afde8953a8a183c80fb8f657ac"
ARCHIVE_STEM = "VoiceRandomizer-options-candidate"
CHUNK_SIZE = 1024 * 1024


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK_SIZE), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path):
    return {"bytes": path.stat().st_size, "sha256": sha256_file(path)}


def read_json(path):
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def safe_relative_path(value):
    if not isinstance(value, str) or "\\" in value:
        raise RuntimeError("Manifest contains a non-portable path: " + repr(value))
    relative = PurePosixPath(value)
    if relative.is_absolute() or not relative.parts or any(part in ("", ".", "..") for part in relative.parts):
        raise RuntimeError("Manifest contains an unsafe path: " + repr(value))
    return relative


def walk_files(root):
    result = set()
    for current, directories, files in os.walk(root, followlinks=False):
        current_path = Path(current)
        for name in directories:
            if (current_path / name).is_symlink():
                raise RuntimeError("Original package contains a symbolic directory: " + name)
        for name in files:
            path = current_path / name
            if path.is_symlink() or not path.is_file():
                raise RuntimeError("Original package contains a non-regular file: " + str(path))
            result.add(path.relative_to(root).as_posix())
    return result


def validate_original(original, input_manifest, tree_manifest, voice_index, assets_archive=None):
    tree_rows = tree_manifest.get("files")
    if not isinstance(tree_rows, list) or not tree_rows:
        raise RuntimeError("Original tree manifest has no file list")
    expected = {}
    for row in tree_rows:
        relative = safe_relative_path(row.get("path"))
        key = relative.as_posix()
        if key in expected:
            raise RuntimeError("Duplicate path in original tree manifest: " + key)
        if not isinstance(row.get("bytes"), int) or row["bytes"] < 0:
            raise RuntimeError("Invalid file size in original tree manifest: " + key)
        if not re.fullmatch(r"[0-9a-f]{64}", str(row.get("sha256", ""))):
            raise RuntimeError("Invalid SHA256 in original tree manifest: " + key)
        expected[key] = {"bytes": row["bytes"], "sha256": row["sha256"]}

    if assets_archive is None:
        actual_paths = walk_files(original)
    else:
        with zipfile.ZipFile(assets_archive) as archive:
            names = [item.filename for item in archive.infolist() if not item.is_dir()]
            asset_names = [name for name in names if name.startswith("voice/wav/voice_randomizer/")]
            known_modules = {"mods/VoiceRandomizer/VoiceRandomizer.dll", "mods/VoiceRandomizer/sora_mod.ini"}
            if len(names) != len(set(names)) or set(names) - set(asset_names) != known_modules:
                raise RuntimeError("Asset archive must be the previous compatible installation package")
            actual_paths = {name.removeprefix("voice/wav/") for name in asset_names}
            actual_paths.update(key for key in expected if not key.startswith("voice_randomizer/")
                                and original.joinpath(*safe_relative_path(key).parts).is_file())
    if actual_paths != set(expected):
        missing = sorted(set(expected) - actual_paths)
        extra = sorted(actual_paths - set(expected))
        raise RuntimeError(f"Original file set differs from reviewed manifest (missing={len(missing)}, extra={len(extra)})")

    for key, record in expected.items():
        path = original.joinpath(*safe_relative_path(key).parts)
        if assets_archive is not None and key.startswith("voice_randomizer/"):
            with zipfile.ZipFile(assets_archive) as archive, archive.open("voice/wav/" + key) as stream:
                digest = hashlib.sha256()
                size = 0
                for chunk in iter(lambda: stream.read(CHUNK_SIZE), b""):
                    digest.update(chunk)
                    size += len(chunk)
            actual = {"bytes": size, "sha256": digest.hexdigest()}
        else:
            actual = file_record(path)
        if actual != record:
            raise RuntimeError("Original file hash/size mismatch: " + key)

    input_rows = input_manifest.get("files")
    if not isinstance(input_rows, list) or not input_rows:
        raise RuntimeError("Input manifest has no file list")
    for row in input_rows:
        key = safe_relative_path(row.get("path")).as_posix()
        if key not in expected:
            raise RuntimeError("Input manifest path is absent from full manifest: " + key)
        if expected[key] != {"bytes": row.get("bytes"), "sha256": row.get("sha256")}:
            raise RuntimeError("Input and full-tree manifests disagree: " + key)

    rows = voice_index.get("rows")
    if not isinstance(rows, list) or len(rows) != 824:
        raise RuntimeError("Expected exactly 824 indexed legacy voices")
    if voice_index.get("origin_dll_sha256") != "c0d344f13742234bb448749baa1eed262e1fee6aad289ed3a9c8c2d7eacb5c92":
        raise RuntimeError("Voice index does not match the reviewed original DLL")
    voice_paths = set()
    for row in rows:
        name = row.get("name")
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_]+", name):
            raise RuntimeError("Invalid voice name in voice index")
        key = "voice_randomizer/assets/old/" + name + ".wav"
        expected_record = {"bytes": row.get("bytes"), "sha256": row.get("sha256")}
        if expected.get(key) != expected_record:
            raise RuntimeError("Voice index and original file manifest disagree: " + key)
        voice_paths.add(key)

    actual_old = {key for key in expected if key.startswith("voice_randomizer/assets/old/") and key.lower().endswith(".wav")}
    actual_ui = {key for key in expected if key.startswith("voice_randomizer/assets/ui/") and key.lower().endswith(".png")}
    if actual_old != voice_paths or len(actual_ui) != 4:
        raise RuntimeError("Original legacy WAV or UI PNG inventory is incomplete")

    required = {
        "voice_randomizer/voice_randomizer.ini",
        "voice_randomizer/克萝赛MOD设置.exe",
        "克萝赛随机语音MOD.txt",
        "xinput1_4.dll",
    }
    if not required.issubset(expected):
        raise RuntimeError("Expected original package files are missing from the manifest")
    return expected


def git_output(*arguments):
    return subprocess.check_output(["git", "-c", "safe.directory=" + REPOSITORY.as_posix(), "-C", str(REPOSITORY), *arguments], text=True, encoding="utf-8").strip()


def validate_committed_source():
    commit = git_output("rev-parse", "HEAD")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise RuntimeError("Unable to identify the committed source revision")
    changes = git_output("status", "--porcelain", "--untracked-files=all")
    if changes:
        raise RuntimeError("Commit package and documentation changes before packaging:\n" + changes)
    return commit


def validate_native_build():
    build = ROOT / "source" / "build"
    report_path = build / "report.json"
    dll_path = build / "VoiceRandomizer.dll"
    pe_path = build / "pe_inspection.txt"
    report = read_json(report_path)
    source_git = report.get("source_git", {})
    native_commit = source_git.get("commit")
    if not isinstance(native_commit, str) or not re.fullmatch(r"[0-9a-f]{40}", native_commit) or type(source_git.get("dirty")) is not bool:
        raise RuntimeError("Native build report does not identify its source revision and workspace state")
    git_output("cat-file", "-e", native_commit + "^{commit}")
    if report.get("detours_commit") != "9764cebcb1a75940e68fa83d6730ffaf0f669401":
        raise RuntimeError("Native build uses an unexpected Detours revision")
    if Path(str(report.get("artifact", ""))).name != dll_path.name:
        raise RuntimeError("Native build report does not describe the expected DLL")
    actual_dll = file_record(dll_path)
    if not re.fullmatch(r"[0-9a-f]{64}", str(report.get("dll_sha256", ""))) or not isinstance(report.get("dll_bytes"), int):
        raise RuntimeError("Native build report has invalid DLL metadata")
    if report.get("dll_bytes") != actual_dll["bytes"] or report.get("dll_sha256") != actual_dll["sha256"]:
        raise RuntimeError("Native report and DLL disagree")
    if report.get("exports") != ["SoraVoiceInitialize"]:
        raise RuntimeError("Unexpected module exports")
    dependencies = {str(name).lower() for name in report.get("dependencies", [])}
    if "xinput1_4.dll" in dependencies or any("xinput" in name for name in dependencies):
        raise RuntimeError("The module has an XInput dependency")
    if report.get("dll_executed") is not False or report.get("game_runtime_verified") is not False or report.get("game_directory_written") is not False:
        raise RuntimeError("Build report has unexpected runtime/install claims")

    notice_path = ROOT / "source" / "external" / "detours" / "LICENSE"
    detours_notice = report.get("embedded_notices", {}).get("DETOURS_LICENSE", {})
    notice_record = file_record(notice_path)
    if (detours_notice.get("type") != "RCDATA" or detours_notice.get("exact_input_match") is not True
            or {"bytes": detours_notice.get("bytes"), "sha256": detours_notice.get("sha256")} != notice_record):
        raise RuntimeError("Native report does not verify the complete Detours license resource")
    generated_inputs = report.get("generated_inputs", {})
    if not isinstance(generated_inputs, dict) or not generated_inputs:
        raise RuntimeError("Native report has no generated resource inputs")
    for key, expected in generated_inputs.items():
        relative = safe_relative_path(key)
        generated = build.joinpath(*relative.parts)
        if not generated.is_file() or sha256_file(generated) != expected:
            raise RuntimeError("Generated native build input hash mismatch: " + key)

    pe_text = pe_path.read_text(encoding="utf-8", errors="replace")
    for marker in ("File Type: DLL", "8664 machine (x64)", "PE32+", "SoraVoiceInitialize"):
        if marker not in pe_text:
            raise RuntimeError("PE inspection is missing evidence: " + marker)
    if re.search(r"\bXINPUT[^\r\n]*\.dll\b", pe_text, re.IGNORECASE):
        raise RuntimeError("PE inspection lists an XInput dependency")

    input_hashes = report.get("inputs")
    if not isinstance(input_hashes, dict) or not input_hashes:
        raise RuntimeError("Native build report has no source input hashes")
    source_root = ROOT / "source"
    for key, expected in input_hashes.items():
        relative = safe_relative_path(key)
        path = source_root.joinpath(*relative.parts)
        if not path.is_file() or sha256_file(path) != expected:
            raise RuntimeError("Native build input hash mismatch: " + key)
        git_path = "compat/source/" + relative.as_posix()
        # The report hashes raw worktree bytes; Git may normalize text line
        # endings in its blob, so compare committed trees and verify the report
        # hash against this clean checkout instead of hashing the raw blob.
        same_committed_content = subprocess.run(
            ["git", "-c", "safe.directory=" + REPOSITORY.as_posix(), "-C", str(REPOSITORY), "diff", "--quiet", native_commit, "HEAD", "--", git_path],
            check=False,
        )
        if same_committed_content.returncode != 0:
            raise RuntimeError("Native report input differs from its source commit: " + key)
    return report, report_path, pe_path, dll_path


def source_package_files():
    # Include the complete fork sources, including the original editor project.
    paths = git_output("ls-files", "-z").split("\0")
    result = []
    forbidden = {".dll", ".exe", ".wav", ".obj", ".lib", ".exp", ".pdb", ".res", ".pyc", ".log", ".zip"}
    for relative in sorted(path for path in paths if path):
        path = REPOSITORY.joinpath(*safe_relative_path(relative).parts)
        if path.suffix.lower() in forbidden or set(PurePosixPath(relative).parts).intersection({"analysis", ".git", "__pycache__"}):
            raise RuntimeError("Private input or binary tracked in source tree: " + relative)
        if not path.is_file():
            raise RuntimeError("Tracked source file is missing: " + relative)
        result.append((path, relative))
    return result


def zip_from_files(output, files):
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
        for path, member in files:
            if isinstance(path, tuple):
                source_archive, source_member = path
                with zipfile.ZipFile(source_archive) as source, source.open(source_member) as reader, archive.open(member, "w", force_zip64=True) as writer:
                    shutil.copyfileobj(reader, writer, CHUNK_SIZE)
            else:
                archive.write(path, member)


def verify_zip(path, expected):
    with zipfile.ZipFile(path, "r") as archive:
        bad_member = archive.testzip()
        if bad_member:
            raise RuntimeError("ZIP CRC failure: " + bad_member)
        names = [entry.filename for entry in archive.infolist() if not entry.is_dir()]
        if len(names) != len(set(names)) or set(names) != set(expected):
            raise RuntimeError("ZIP member list differs from the expected package contents")
        records = []
        for name in sorted(names):
            digest = hashlib.sha256()
            size = 0
            with archive.open(name, "r") as member:
                for chunk in iter(lambda: member.read(CHUNK_SIZE), b""):
                    digest.update(chunk)
                    size += len(chunk)
            record = {"path": name, "bytes": size, "sha256": digest.hexdigest()}
            if expected[name] != {"bytes": size, "sha256": digest.hexdigest()}:
                raise RuntimeError("ZIP member hash mismatch: " + name)
            records.append(record)
    return records


def atomic_json(path, value):
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", required=True, type=Path,
                        help="read-only original package directory containing xinput1_4.dll and voice_randomizer/")
    parser.add_argument("--assets-archive", type=Path,
                        help="previous compatible install ZIP supplying unchanged assets without extraction")
    parser.add_argument("--out", type=Path, default=ROOT / "dist",
                        help="output directory inside this workspace (default: dist/)")
    parser.add_argument("--release-version", help="prepare versioned release filenames, for example 0.1.0")
    parser.add_argument("--no-audio", action="store_true", help="exclude legacy WAVs from the installation archive")
    args = parser.parse_args()
    if args.release_version and not re.fullmatch(r"\d+\.\d+\.\d+", args.release_version):
        raise RuntimeError("--release-version must be MAJOR.MINOR.PATCH")
    stem = "VoiceRandomizer-v" + args.release_version if args.release_version else ARCHIVE_STEM

    original = args.original.expanduser().resolve(strict=True)
    if not original.is_dir():
        raise RuntimeError("--original must name the original package directory")
    output = args.out.expanduser().resolve()
    try:
        output.relative_to(ROOT.resolve())
    except ValueError as exc:
        raise RuntimeError("Output directory must be inside this project workspace") from exc
    output.mkdir(parents=True, exist_ok=True)

    source_commit = validate_committed_source()
    native_report, native_report_path, pe_path, dll_path = validate_native_build()
    editor = native_report.get("editor")
    if not editor or editor.get("executed") is not False:
        raise RuntimeError("Build the current settings editor with --editor before packaging")
    editor_path = ROOT / "source/build/KloseVoiceSettings.exe"
    editor_record = file_record(editor_path)
    if editor_record != {"bytes": editor.get("bytes"), "sha256": editor.get("sha256")}:
        raise RuntimeError("Editor report and binary disagree")
    for key, expected in editor.get("inputs", {}).items():
        path = REPOSITORY.joinpath(*safe_relative_path(key).parts)
        if not path.is_file() or sha256_file(path) != expected:
            raise RuntimeError("Editor source input changed: " + key)
        if git_output("diff", native_report["source_git"]["commit"], "HEAD", "--", key):
            raise RuntimeError("Editor input differs from its build source commit: " + key)
    template_path = ROOT / "source/build/editor_template.ini"
    if sha256_file(template_path) != editor.get("embedded_template_sha256"):
        raise RuntimeError("Generated default INI differs from embedded editor template")
    input_manifest = read_json(ROOT / "analysis" / "input_manifest.json")
    tree_manifest = read_json(ROOT / "analysis" / "original_tree_manifest.json")
    voice_index = read_json(ROOT / "analysis" / "voice-index.json")
    assets_archive = args.assets_archive.resolve(strict=True) if args.assets_archive else None
    original_records = validate_original(original, input_manifest, tree_manifest, voice_index, assets_archive)

    original_notice = original / "克萝赛随机语音MOD.txt"
    notice_record = file_record(original_notice)
    notice_expected = original_records["克萝赛随机语音MOD.txt"]
    if notice_record != notice_expected:
        raise RuntimeError("Preserved original notice differs from the source package text")

    dll_record = file_record(dll_path)
    ini_path = ROOT / "source" / "sora_mod.ini"
    module_ini_record = file_record(ini_path)
    install_name = stem + ("-main.zip" if args.release_version else "-install.zip")
    source_name = stem + "-source.zip"
    manifest_name = stem + "-manifest.json"
    install_path = output / install_name
    source_path = output / source_name

    asset_records = {
        key: value for key, value in original_records.items() if key.startswith("voice_randomizer/")
        and not (args.no_audio and key.lower().endswith(".wav"))
    }
    expected_install = {
        "mods/VoiceRandomizer/VoiceRandomizer.dll": dll_record,
        "mods/VoiceRandomizer/sora_mod.ini": module_ini_record,
    }
    overrides = {
        "voice_randomizer/克萝赛MOD设置.exe": editor_path,
        "voice_randomizer/voice_randomizer.ini": template_path,
    }
    for key, value in asset_records.items():
        expected_install["voice/wav/" + key] = file_record(overrides[key]) if key in overrides else value

    install_files = [(dll_path, "mods/VoiceRandomizer/VoiceRandomizer.dll"),
                     (ini_path, "mods/VoiceRandomizer/sora_mod.ini")]
    install_files.extend((overrides[key] if key in overrides else
                          ((assets_archive, "voice/wav/" + key) if assets_archive else
                           original.joinpath(*safe_relative_path(key).parts)), "voice/wav/" + key)
                         for key in sorted(asset_records))
    with tempfile.NamedTemporaryFile(prefix=install_name + ".", suffix=".tmp", dir=output, delete=False) as handle:
        install_tmp = Path(handle.name)
    try:
        zip_from_files(install_tmp, install_files)
        install_members = verify_zip(install_tmp, expected_install)
        os.replace(install_tmp, install_path)
    finally:
        install_tmp.unlink(missing_ok=True)

    update_path = output / (stem + ("-update.zip" if args.release_version else "-module-update.zip"))
    update_files = install_files[:2] + [(editor_path, "voice/wav/voice_randomizer/克萝赛MOD设置.exe")]
    zip_from_files(update_path, update_files)
    update_members = verify_zip(update_path, {member: expected_install[member] for _, member in update_files})

    source_files = source_package_files()
    source_files.append((original_notice, "compat/source/third_party/original-notice.txt"))
    source_files.sort(key=lambda item: item[1])
    source_expected = {member: file_record(path) for path, member in source_files}
    with tempfile.NamedTemporaryFile(prefix=source_name + ".", suffix=".tmp", dir=output, delete=False) as handle:
        source_tmp = Path(handle.name)
    try:
        zip_from_files(source_tmp, source_files)
        source_members = verify_zip(source_tmp, source_expected)
        os.replace(source_tmp, source_path)
    finally:
        source_tmp.unlink(missing_ok=True)

    docs = [
        (ROOT / "README.md", output / (stem + "-README.md")),
        (ROOT / "验收与回退.md", output / (stem + "-验收与回退.md")),
    ]
    doc_records = []
    for source_doc, destination in docs:
        shutil.copyfile(source_doc, destination)
        doc_records.append({"path": destination.name, **file_record(destination)})

    manifest = {
        "artifact": "prepared release " + args.release_version if args.release_version else "local candidate; not a release",
        "audio_included": not args.no_audio,
        "created_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "game_sha256_required": EXPECTED_GAME_SHA256,
        "package_source_commit": source_commit,
        "settings_editor": editor,
        "native_module": {
            "source_commit": native_report["source_git"]["commit"],
            "build_report_workspace_dirty": native_report["source_git"]["dirty"],
            "all_compile_inputs_match_source_commit": True,
            "dll_sha256": dll_record["sha256"],
            "dll_bytes": dll_record["bytes"],
            "export": native_report["exports"],
            "dependencies": native_report["dependencies"],
            "build_report_sha256": sha256_file(native_report_path),
            "pe_inspection_sha256": sha256_file(pe_path),
            "input_sha256": native_report["inputs"],
            "generated_input_sha256": native_report["generated_inputs"],
            "embedded_notices": native_report["embedded_notices"],
        },
        "original_assets": {
            "source_package": "Sora1st-克萝赛随机语音-v1.2.0 (read-only input)",
            "assets_archive": ({"name": assets_archive.name, **file_record(assets_archive)} if assets_archive else None),
            "full_original_file_count": len(original_records),
            "full_original_bytes": sum(record["bytes"] for record in original_records.values()),
            "copied_file_count": len(asset_records)-len(overrides),
            "copied_bytes": sum(record["bytes"] for key,record in asset_records.items() if key not in overrides),
            "replaced_files": {key: file_record(value) for key,value in overrides.items()},
            "copied_files": [
                {"path": key, **value} for key, value in sorted(asset_records.items()) if key not in overrides
            ],
            "source_notice": {"path": "compat/source/third_party/original-notice.txt", **notice_record},
            "root_legacy_xinput_and_readme_copied": False,
        },
        "installation_archive": {
            "path": install_name,
            "bytes": install_path.stat().st_size,
            "sha256": sha256_file(install_path),
            "member_count": len(install_members),
            "members": install_members,
        },
        "module_update_archive": {
            "path": update_path.name,
            **file_record(update_path),
            "member_count": len(update_members),
            "members": update_members,
        },
        "source_archive": {
            "path": source_name,
            "bytes": source_path.stat().st_size,
            "sha256": sha256_file(source_path),
            "member_count": len(source_members),
            "members": source_members,
        },
        "adjacent_docs": doc_records,
    }
    atomic_json(output / manifest_name, manifest)
    print(json.dumps({
        "module_update_archive": str(update_path),
        "package_source_commit": source_commit,
        "native_module_commit": native_report["source_git"]["commit"],
        "install_archive": str(install_path),
        "install_bytes": manifest["installation_archive"]["bytes"],
        "install_sha256": manifest["installation_archive"]["sha256"],
        "install_members": len(install_members),
        "source_archive": str(source_path),
        "source_bytes": manifest["source_archive"]["bytes"],
        "source_sha256": manifest["source_archive"]["sha256"],
        "source_members": len(source_members),
        "manifest": str(output / manifest_name),
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError, json.JSONDecodeError,
            zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        raise SystemExit("package_candidate: " + str(error))
