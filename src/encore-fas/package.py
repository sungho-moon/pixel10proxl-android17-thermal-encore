"""Package Encore with LF text and checksums computed from the shipped bytes."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

BASE = Path(__file__).resolve().parent
REPO = BASE.parents[1]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def module_files(module: Path) -> list[Path]:
    return sorted((p for p in module.rglob("*") if p.is_file()),
                  key=lambda p: p.relative_to(module).as_posix())


def normalize_text(module: Path) -> int:
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


def manifest(module: Path, name: str, paths: list[Path]) -> None:
    (module / name).write_text(
        "".join(f"{digest(p)}  {p.relative_to(module).as_posix()}\n" for p in paths),
        encoding="utf-8", newline="\n")


def archive(path: Path, items: list[tuple[Path, str]]) -> None:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for file, name in items:
            info = zipfile.ZipInfo(name)
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            executable = file.suffix == ".sh" or "/bin/" in "/" + name or file.name == "update-binary"
            info.external_attr = (0o100755 if executable else 0o100644) << 16
            z.writestr(info, file.read_bytes())
    with zipfile.ZipFile(path) as z:
        if z.testzip() is not None:
            raise ValueError(f"Corrupt ZIP: {path}")
    path.with_suffix(path.suffix + ".sha256").write_text(
        f"{digest(path)}  {path.name}\n", encoding="utf-8", newline="\n")
    print(f"{path.name} {path.stat().st_size} {digest(path)}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, default=BASE / "module")
    parser.add_argument("--binary", type=Path, help="Optional newly built ARM64 pixel-control")
    parser.add_argument("--output", type=Path, default=REPO / "outputs")
    args = parser.parse_args()
    module = args.module.resolve()
    if args.binary and args.binary.resolve() != (module / "bin/pixel-control").resolve():
        shutil.copy2(args.binary, module / "bin/pixel-control")
    print(f"LF normalization: {normalize_text(module)} files")

    provenance_path = module / "PROVENANCE.json"
    provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    provenance["binaries"]["pixel-control"] = digest(module / "bin/pixel-control")
    provenance_path.write_text(json.dumps(provenance, ensure_ascii=False, indent=2) + "\n",
                               encoding="utf-8", newline="\n")
    for name in ("update-binary", "updater-script"):
        file = module / "META-INF/com/google/android" / name
        file.with_suffix(file.suffix + ".sha256").write_text(
            digest(file) + "\n", encoding="utf-8", newline="\n")

    paths = module_files(module)
    runtime_exclude = {"SHA256SUMS", "INSTALL_SHA256SUMS", "module.prop", "customize.sh", "README.md"}
    manifest(module, "SHA256SUMS", [p for p in paths if p.name not in runtime_exclude
                                   and "META-INF" not in p.relative_to(module).parts])
    manifest(module, "INSTALL_SHA256SUMS", [p for p in paths if p.name not in {"INSTALL_SHA256SUMS", "module.prop"}
                                           and "META-INF" not in p.relative_to(module).parts])
    props = dict(line.split("=", 1) for line in (module / "module.prop").read_text(encoding="utf-8").splitlines()
                 if "=" in line)
    name = f"Encore-Pixel-10-Android17-v{props['version']}"
    args.output.mkdir(parents=True, exist_ok=True)
    archive(args.output / f"{name}.zip", [(p, p.relative_to(module).as_posix()) for p in module_files(module)])
    source = BASE / "pixel-control-fas-v024-android17-generic.cpp"
    archive(args.output / f"{name}-source.zip", [(source, "source/" + source.name),
            (Path(__file__), "source/package.py")] +
            [(p, "source/module/" + p.relative_to(module).as_posix()) for p in module_files(module)])


if __name__ == "__main__":
    main()
