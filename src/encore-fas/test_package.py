"""Reproduce a Windows CRLF checkout and verify the Android ZIP payload."""
import hashlib
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


class PackageTest(unittest.TestCase):
    def test_crlf_checkout_produces_lf_payload_and_valid_manifests(self):
        base = Path(__file__).resolve().parent
        with tempfile.TemporaryDirectory(prefix="encore-package-") as tmp:
            root = Path(tmp)
            module = root / "module"
            shutil.copytree(base / "module", module)
            binary = (module / "bin/pixel-control").read_bytes()
            for path in module.rglob("*"):
                if not path.is_file():
                    continue
                data = path.read_bytes()
                if b"\0" in data:
                    continue
                try:
                    data.decode("utf-8")
                except UnicodeDecodeError:
                    continue
                path.write_bytes(data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
            subprocess.run([sys.executable, str(base / "package.py"), "--module", str(module),
                            "--output", str(root / "output")], check=True, capture_output=True, text=True)
            package = next(p for p in (root / "output").glob("*.zip") if not p.name.endswith("-source.zip"))
            with zipfile.ZipFile(package) as z:
                self.assertEqual(z.read("bin/pixel-control"), binary)
                for name in z.namelist():
                    if name.endswith(".sh") or name in {"module.prop", "META-INF/com/google/android/update-binary"}:
                        self.assertNotIn(b"\r", z.read(name), name)
                        if name.endswith(".sh"):
                            self.assertEqual((z.getinfo(name).external_attr >> 16) & 0o777, 0o755)
                for manifest in ("SHA256SUMS", "INSTALL_SHA256SUMS"):
                    for line in z.read(manifest).decode().splitlines():
                        sha, name = line.split("  ", 1)
                        self.assertEqual(hashlib.sha256(z.read(name)).hexdigest(), sha, name)
                        if manifest == "SHA256SUMS":
                            self.assertNotIn(name, {"customize.sh", "README.md", "module.prop"})
                sha = package.with_suffix(".zip.sha256").read_text().split()[0]
                self.assertEqual(hashlib.sha256(package.read_bytes()).hexdigest(), sha)


if __name__ == "__main__":
    unittest.main()
