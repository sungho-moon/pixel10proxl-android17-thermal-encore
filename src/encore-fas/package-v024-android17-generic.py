from pathlib import Path
import hashlib
import json
import shutil
import zipfile

base = Path(__file__).resolve().parent
module = base / "v024-android17-generic-module"
outputs = base.parent.parent / "outputs"
binary = base / "pixel-control-fas-v024-android17-generic"
source = base / "pixel-control-fas-v024-android17-generic.cpp"
baseline = base.parent / "encore-pixel/artifacts/Encore-Pixel-CP41-v0.1.6-source.zip"
fas_source = outputs / "fas-rs-v4.9.1-source.zip"
assert binary.is_file() and source.is_file() and baseline.is_file() and fas_source.is_file()
shutil.copy2(binary, module / "bin/pixel-control")

provenance_path = module / "PROVENANCE.json"
provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
provenance["binaries"]["pixel-control"] = hashlib.sha256(binary.read_bytes()).hexdigest()
provenance["frame_feedback"] = {
    "type": "Encore single-writer FAS with independently implemented fas-rs proportional adviser",
    "fas_rs_repository": "https://github.com/shadow3aaa/fas-rs",
    "fas_rs_version": "v4.9.1",
    "fas_rs_source_commit": "eb883c534e92107f536ae8ebc343c99243f5ee4d",
    "fas_rs_executable_included": False,
    "pixel_frame_source": "hash-pinned libgui Surface::hook_queueBuffer tracefs uprobe",
    "libgui_sha256": "20163138d23043abf3d755602483b212b6f9ff321e9810814b213e8655b5057a",
    "control": "aggressive profile: 90th percentile of 30-60 recent frame intervals, fas-rs kp equivalent, +1 OPP for advice 1-2 and +2 OPP for advice 3, bounded to Encore level 3",
    "thermal_cap": "battery 40/41/42 C tiered extra-boost limit; whole-controller protection remains 43 C with 41 C recovery",
}
provenance_path.write_text(json.dumps(provenance, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

def files():
    return sorted((p for p in module.rglob("*") if p.is_file()),
                  key=lambda p: p.relative_to(module).as_posix())

runtime = [p for p in files() if p.name not in {
    "SHA256SUMS", "INSTALL_SHA256SUMS", "module.prop", "customize.sh", "README.md"
} and "META-INF" not in p.relative_to(module).parts]
(module / "SHA256SUMS").write_text(
    "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(module).as_posix()}\n" for p in runtime),
    encoding="utf-8", newline="\n")
install = [p for p in files() if p.name not in {"INSTALL_SHA256SUMS", "module.prop"}
           and "META-INF" not in p.relative_to(module).parts]
(module / "INSTALL_SHA256SUMS").write_text(
    "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(module).as_posix()}\n" for p in install),
    encoding="utf-8", newline="\n")

def archive(path, items):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for file, name in items:
            info = zipfile.ZipInfo(name)
            info.compress_type = zipfile.ZIP_DEFLATED
            executable = file.suffix == ".sh" or "/bin/" in "/" + name or file.name == "update-binary"
            info.external_attr = (0o100755 if executable else 0o100644) << 16
            z.writestr(info, file.read_bytes())

outputs.mkdir(exist_ok=True)
package = outputs / "Encore-Pixel-10-Android17-v0.2.3-generic.zip"
archive(package, [(p, p.relative_to(module).as_posix()) for p in files()])
source_items = [(source, source.name), (Path(__file__), Path(__file__).name),
                (baseline, "baseline/Encore-Pixel-CP41-v0.1.6-source.zip"),
                (fas_source, "upstream/fas-rs-v4.9.1-source.zip")]
source_items.extend((p, "module/" + p.relative_to(module).as_posix()) for p in files()
                    if "META-INF" not in p.relative_to(module).parts)
source_zip = outputs / "Encore-Pixel-10-Android17-v0.2.3-generic-source.zip"
archive(source_zip, source_items)
for path in (package, source_zip):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    path.with_suffix(path.suffix + ".sha256").write_text(
        f"{digest}  {path.name}\n", encoding="utf-8")
    print(path.name, path.stat().st_size, digest)
