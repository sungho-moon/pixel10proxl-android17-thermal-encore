#!/usr/bin/env python3
"""Build Encore with LF text, payload checksums and Unix ZIP permissions."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path


BASE = Path(__file__).resolve().parent
MODULE = BASE / "module"
OUTPUTS = BASE.parent.parent / "outputs"
BINARY = BASE / "pixel-control-fas-v024-android17-generic"
SOURCE = BASE / "pixel-control-fas-v024-android17-generic.cpp"
GUARDIAN_SOURCE = BASE.parent.parent / "work/guardian-build"
BASELINE = BASE.parent / "encore-pixel/artifacts/Encore-Pixel-CP41-v0.1.6-source.zip"
FAS_SOURCE = OUTPUTS / "fas-rs-v4.9.1-source.zip"

CHECKSUM_FILES = {"SHA256SUMS", "INSTALL_SHA256SUMS"}
INSTALL_EXCLUDED = {"INSTALL_SHA256SUMS", "module.prop"}
RUNTIME_EXCLUDED = CHECKSUM_FILES | {"module.prop", "customize.sh", "README.md"}
RUNTIME_EXCLUDED_NAMES = {
    "config-name",
    "expected.env",
    "requested-mode",
    "thermal-disabled",
}
RUNTIME_EXCLUDED_PREFIXES = ("profiles/", "state/", "system/vendor/etc/")


def read_module_properties(path: Path) -> tuple[str, int]:
    properties: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" not in line or line.lstrip().startswith("#"):
            continue
        key, value = line.split("=", 1)
        properties[key.strip()] = value.strip()

    version = properties.get("version", "")
    version_code = properties.get("versionCode", "")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]*", version):
        raise ValueError("module.prop has no safe version value")
    if not re.fullmatch(r"\d+", version_code) or not 0 < int(version_code) < 2**31:
        raise ValueError("module.prop has no valid positive versionCode")
    return version, int(version_code)


def module_files(module: Path) -> list[Path]:
    return sorted(
        (path for path in module.rglob("*") if path.is_file()),
        key=lambda path: path.relative_to(module).as_posix(),
    )


def executable(path: Path, relative: str) -> bool:
    parts = Path(relative).parts
    return path.suffix in {".py", ".sh"} or "bin" in parts or path.name == "update-binary"


def normalize_text(module: Path) -> int:
    """Hash and ship the same LF bytes, including on Windows CRLF checkouts."""
    changed = 0
    for path in module_files(module):
        data = path.read_bytes()
        if b"\0" in data:
            continue
        try:
            data.decode("utf-8")
        except UnicodeDecodeError:
            continue
        normalized = data.replace(b"\r\n", b"\n")
        if normalized != data:
            path.write_bytes(normalized)
            changed += 1
        if (path.suffix == ".sh" or path.name in {"module.prop", "update-binary", "updater-script"}) and b"\r" in normalized:
            raise ValueError(f"Invalid carriage return in {path}")
    return changed


def validate_webui(module: Path) -> None:
    """Reject a missing or partially copied Vite build before packaging it."""
    webroot = module / "webroot"
    index = webroot / "index.html"
    if not index.is_file():
        raise ValueError(f"WebUI entry missing: {index}")
    for match in re.finditer(r'''\b(?:src|href)\s*=\s*(["'])(.*?)\1''', index.read_text(encoding="utf-8")):
        reference = match.group(2).split("?", 1)[0].split("#", 1)[0]
        if not reference or reference.startswith(("http:", "https:", "data:", "//")):
            continue
        resource = (webroot / reference.lstrip("/")).resolve()
        if not resource.is_relative_to(webroot.resolve()) or not resource.is_file():
            raise ValueError(f"WebUI resource missing or outside webroot: {reference}")


def apply_permissions(module: Path, files: list[Path]) -> None:
    for path in files:
        relative = path.relative_to(module).as_posix()
        path.chmod(0o755 if executable(path, relative) else 0o644)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_checksums(module: Path) -> None:
    runtime = [
        path for path in module_files(module)
        if path.name not in RUNTIME_EXCLUDED
        and path.name not in RUNTIME_EXCLUDED_NAMES
        and not any(path.relative_to(module).as_posix().startswith(prefix)
                    for prefix in RUNTIME_EXCLUDED_PREFIXES)
        and "META-INF" not in path.relative_to(module).parts
    ]
    (module / "SHA256SUMS").write_text(
        "".join(f"{sha256(path)}  {path.relative_to(module).as_posix()}\n" for path in runtime),
        encoding="utf-8",
        newline="\n",
    )

    # INSTALL_SHA256SUMS includes SHA256SUMS because the installer verifies it;
    # it excludes META-INF because KernelSU supplies that directory itself.
    files = module_files(module)
    install = [
        path for path in files
        if path.name not in INSTALL_EXCLUDED and "META-INF" not in path.relative_to(module).parts
    ]
    (module / "INSTALL_SHA256SUMS").write_text(
        "".join(f"{sha256(path)}  {path.relative_to(module).as_posix()}\n" for path in install),
        encoding="utf-8",
        newline="\n",
    )


def archive(path: Path, items: list[tuple[Path, str]]) -> None:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for source, name in sorted(items, key=lambda item: item[1]):
            info = zipfile.ZipInfo(name)
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            mode = 0o100755 if executable(source, name) else 0o100644
            info.external_attr = mode << 16
            output.writestr(info, source.read_bytes())
    with zipfile.ZipFile(path) as output:
        if output.testzip() is not None:
            raise ValueError(f"Corrupt ZIP: {path}")


def checksum_file(path: Path) -> Path:
    checksum = path.with_suffix(path.suffix + ".sha256")
    checksum.write_text(f"{sha256(path)}  {path.name}\n", encoding="utf-8", newline="\n")
    return checksum


def remove_previous(path: Path) -> None:
    path.unlink(missing_ok=True)
    path.with_suffix(path.suffix + ".sha256").unlink(missing_ok=True)


def build_source_bundle(version: str, module: Path, outputs: Path) -> Path | None:
    optional = [(BASELINE, "baseline/" + BASELINE.name), (FAS_SOURCE, "upstream/" + FAS_SOURCE.name)]
    present = [(path, name) for path, name in optional if path.is_file()]
    source_package = outputs / f"Encore-Pixel-10-Android17-v{version}-source.zip"
    if not SOURCE.is_file() or not present:
        remove_previous(source_package)
        print("Optional source bundle skipped: source file or optional upstream archives are absent.")
        return None

    items: list[tuple[Path, str]] = [(SOURCE, "source/" + SOURCE.name), (BASE / "package.py", "source/package.py")]
    items.extend(present)
    items.extend(
        (path, "source/guardian-build/" + path.name)
        for path in sorted(GUARDIAN_SOURCE.iterdir())
        if path.is_file() and path.suffix in {".c", ".h", ".sh", ".md"}
    )
    items.extend(
        (path, "source/module/" + path.relative_to(module).as_posix())
        for path in module_files(module)
    )
    # Include new bridge headers and all corresponding GPL helper sources.
    for header in sorted(BASE.glob("fas-rs-*.h")):
        items.append((header, "source/" + header.name))
    bridge = BASE.parent / "fas-rs-bridge"
    if bridge.is_dir():
        for path in sorted(bridge.rglob("*")):
            relative = path.relative_to(bridge)
            if path.is_file() and not any(p in {"target", ".git", "__pycache__"} for p in relative.parts):
                items.append((path, "source/fas-rs-bridge/" + relative.as_posix()))
    archive(source_package, items)
    checksum_file(source_package)
    return source_package


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, default=MODULE)
    parser.add_argument("--binary", type=Path, help="Optional newly built ARM64 pixel-control")
    parser.add_argument("--output", type=Path, default=OUTPUTS)
    args = parser.parse_args()
    module = args.module.resolve()
    outputs = args.output.resolve()

    try:
        version, version_code = read_module_properties(module / "module.prop")
        validate_webui(module)
        destination = module / "bin/pixel-control"
        binary = args.binary
        if binary is None and module == MODULE.resolve() and BINARY.is_file():
            binary = BINARY
        if binary is not None and binary.resolve() != destination.resolve():
            shutil.copy2(binary, destination)
        if not destination.is_file():
            raise ValueError(f"Required binary missing: {destination}")
        print(f"LF normalization: {normalize_text(module)} files")

        provenance = module / "PROVENANCE.json"
        if provenance.is_file():
            data = json.loads(provenance.read_text(encoding="utf-8"))
            binaries = data.setdefault("binaries", {})
            for name, path in {
                "pixel-control": destination,
                "pixel-fas-rs-bridge": module / "bin/pixel-fas-rs-bridge",
                "thermal-profile-builder": module / "bin/thermal-profile-builder",
                "guardian": module / "thread/bin/guardian",
            }.items():
                if path.is_file():
                    binaries[name] = sha256(path)
            provenance.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")

        for name in ("update-binary", "updater-script"):
            path = module / "META-INF/com/google/android" / name
            if path.is_file():
                path.with_suffix(path.suffix + ".sha256").write_text(sha256(path) + "\n", encoding="utf-8", newline="\n")

        write_checksums(module)
        files = module_files(module)
        apply_permissions(module, files)
        outputs.mkdir(parents=True, exist_ok=True)
        package = outputs / f"Encore-Pixel-10-Android17-v{version}.zip"
        archive(package, [(path, path.relative_to(module).as_posix()) for path in files])
        package_checksum = checksum_file(package)
        source_package = build_source_bundle(version, module, outputs)

        print(f"Built {package.name} (versionCode={version_code}, sha256={sha256(package)})")
        print(f"Checksum {package_checksum.name}")
        if source_package is not None:
            print(f"Built {source_package.name} (sha256={sha256(source_package)})")
        return 0
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"package.py: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
