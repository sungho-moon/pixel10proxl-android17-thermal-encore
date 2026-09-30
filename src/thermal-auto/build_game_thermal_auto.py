"""Build the schema-matching automatic Thermal JSON module."""
from __future__ import annotations
import hashlib, json, shutil, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MODULE = ROOT / "module"
OUTPUT = ROOT / "dist"
VERSION = "1.6.1-auto-config"

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def normalize_tree(root: Path) -> None:
    for p in root.rglob("*"):
        if p.is_file() and (p.suffix == ".sh" or p.name in {"module.prop", "README.md"}):
            p.write_bytes(p.read_bytes().replace(b"\r\n", b"\n"))

def archive(path: Path, root: Path, prefix: str = "") -> None:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in sorted(root.rglob("*")):
            if not p.is_file(): continue
            rel = p.relative_to(root)
            if rel.parts[:1] == ("thermal-test-1790661233",) or rel.name == "thermal-test-1790661233-analysis.json": continue
            if rel.as_posix() == "system/vendor/etc/thermal_info_config_throttling.json": continue
            name = (Path(prefix) / p.relative_to(root)).as_posix()
            info = zipfile.ZipInfo(name)
            info.compress_type = zipfile.ZIP_DEFLATED
            executable = p.suffix == ".sh" or "/bin/" in "/" + name
            info.external_attr = (0o100755 if executable else 0o100644) << 16
            z.writestr(info, p.read_bytes())

def main() -> None:
    MODULE.mkdir(parents=True, exist_ok=True)
    binary = MODULE / "bin/thermal-profile-builder"
    assert binary.is_file()
    destination = MODULE / "bin/thermal-profile-builder"
    if binary.resolve() != destination.resolve(): shutil.copy2(binary, destination)
    prop = "\n".join([
        "id=pixel_game_thermal_cp41",
        "name=Pixel 10 Pro XL Game Thermal Auto Config",
        f"version={VERSION}", "versionCode=16100",
        "author=Local device adaptation",
        "description=CP41 schema-matched Thermal JSON discovery and game profile patch; ambiguous configurations fail closed.", "",
    ])
    (MODULE / "module.prop").write_text(prop, encoding="utf-8", newline="\n")
    normalize_tree(MODULE)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    package = OUTPUT / f"pixel10proxl-game-thermal-CP41-v{VERSION}.zip"
    archive(package, MODULE)
    with zipfile.ZipFile(package) as z: assert z.testzip() is None
    source = OUTPUT / f"pixel10proxl-game-thermal-source-v{VERSION}.zip"
    with zipfile.ZipFile(source, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in sorted(MODULE.rglob("*")):
            if p.is_file(): z.writestr("module/" + p.relative_to(MODULE).as_posix(), p.read_bytes())
        z.writestr("src/thermal-profile-builder.cpp", (ROOT / "thermal-profile-builder.cpp").read_bytes())
        z.writestr("build_game_thermal_auto.py", Path(__file__).read_bytes())
    report = {
        "version": VERSION, "package_sha256": sha(package), "source_sha256": sha(source),
        "schema": "VIRTUAL-SKIN-HINT/CPU-LIGHT-ODPM/CPU-MID/CPU-ODPM/CPU-HIGH/SOC",
        "changes": 40, "discovery": "native RapidJSON probe across thermal_info_config*.json",
        "fail_closed": ["malformed JSON", "duplicate target sensors/cdevs", "ambiguous matching files", "unsupported target frequencies"],
        "fingerprint_keyed_cache": True,
        "atomic_stock_and_overlay_promotion": True,
        "installed": False,
    }
    (OUTPUT / "pixel-game-thermal-auto-validation.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    package.with_suffix(package.suffix + ".sha256").write_text(f"{sha(package)}  {package.name}\n", encoding="utf-8")
    source.with_suffix(source.suffix + ".sha256").write_text(f"{sha(source)}  {source.name}\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))

if __name__ == "__main__": main()
