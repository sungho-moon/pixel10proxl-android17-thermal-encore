"""Verify package.py against a CRLF checkout and a minimal module fixture."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch


BASE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("encore_package", BASE / "package.py")
assert SPEC and SPEC.loader
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class PackageTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="encore-package-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.module = self.root / "module"
        self.output = self.root / "output"
        self.binary = b"\x7fELF\0\r\n\xff"
        files = {
            "module.prop": b"id=encore_pixel_cp41\r\nversion=0.7.5-test\r\nversionCode=750\r\n",
            "service.sh": b"#!/system/bin/sh\r\necho ready\r\n",
            "customize.sh": b"#!/system/bin/sh\r\necho install\r\n",
            "README.md": "说明\r\n".encode(),
            "bin/pixel-control": self.binary,
            "thread/bin/thread-sampler": b"\x7fELF\0sampler",
            # Thermal executables share the module root; discovery changes its
            # cached profiles, state and vendor overlay after installation.
            "bin/thermal-profile-builder": b"\x7fELF\0thermal",
            "tools/discover.sh": b"#!/system/bin/sh\r\necho discover\r\n",
            "profiles/stock.json": b"{\"profile\":\"stock\"}\r\n",
            "profiles/game.json": b"{\"profile\":\"game\"}\r\n",
            "state/config.env": b"CONFIG_NAME=thermal_info_config_test.json\r\n",
            "system/vendor/etc/thermal_info_config_test.json": b"{\"profile\":\"game\"}\r\n",
            "requested-mode": b"game\r\n",
            "webroot/index.html": b'<script src="./assets/index.js"></script>\r\n',
            "webroot/assets/index.js": b"console.log('ready');\r\n",
            "PROVENANCE.json": b'{"binaries": {}}\r\n',
            "META-INF/com/google/android/update-binary": b"#!/sbin/sh\r\nexit 0\r\n",
            "META-INF/com/google/android/updater-script": b"#MAGISK\r\n",
        }
        for name, data in files.items():
            path = self.module / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    def run_package(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(BASE / "package.py"), "--module", str(self.module),
             "--output", str(self.output), *args],
            capture_output=True, text=True,
        )

    def test_crlf_payload_permissions_and_checksums(self) -> None:
        result = self.run_package()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("versionCode=750", result.stdout)
        package = self.output / "Encore-Pixel-10-Android17-v0.7.5-test.zip"
        with zipfile.ZipFile(package) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.read("bin/pixel-control"), self.binary)
            for info in archive.infolist():
                name = info.filename
                if name.endswith(".sh") or name in {"module.prop", "META-INF/com/google/android/update-binary"}:
                    self.assertNotIn(b"\r", archive.read(name), name)
                mode = (info.external_attr >> 16) & 0o777
                expected = 0o755 if PACKAGE.executable(Path(name), name) else 0o644
                self.assertEqual(mode, expected, name)
                self.assertEqual((info.external_attr >> 16) & 0o170000, 0o100000, name)

            thermal_immutable = {"bin/thermal-profile-builder", "tools/discover.sh"}
            thermal_mutable = {
                "profiles/stock.json", "profiles/game.json", "state/config.env",
                "system/vendor/etc/thermal_info_config_test.json", "requested-mode",
            }
            for manifest in ("SHA256SUMS", "INSTALL_SHA256SUMS"):
                names = set()
                for line in archive.read(manifest).decode().splitlines():
                    digest, name = line.split("  ", 1)
                    names.add(name)
                    self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(), digest, name)
                    self.assertFalse(name.startswith("META-INF/"))
                self.assertNotIn("module.prop", names)
                self.assertNotIn("INSTALL_SHA256SUMS", names)
                self.assertTrue(thermal_immutable.issubset(names))
                if manifest == "SHA256SUMS":
                    self.assertTrue({"customize.sh", "README.md", "module.prop"}.isdisjoint(names))
                    self.assertTrue(thermal_mutable.isdisjoint(names))
                else:
                    self.assertIn("SHA256SUMS", names)
                    self.assertIn("customize.sh", names)
                    self.assertTrue(thermal_mutable.issubset(names))

            for name in ("update-binary", "updater-script"):
                path = "META-INF/com/google/android/" + name
                self.assertEqual(archive.read(path + ".sha256").decode().strip(),
                                 hashlib.sha256(archive.read(path)).hexdigest())
            provenance = json.loads(archive.read("PROVENANCE.json"))
            self.assertEqual(provenance["binaries"]["pixel-control"], hashlib.sha256(self.binary).hexdigest())

            thermal_binary = archive.getinfo("bin/thermal-profile-builder")
            thermal_script = archive.getinfo("tools/discover.sh")
            thermal_profile = archive.getinfo("profiles/game.json")
            self.assertEqual((thermal_binary.external_attr >> 16) & 0o777, 0o755)
            self.assertEqual((thermal_script.external_attr >> 16) & 0o777, 0o755)
            self.assertEqual((thermal_profile.external_attr >> 16) & 0o777, 0o644)

        digest = package.with_suffix(".zip.sha256").read_text().split()[0]
        self.assertEqual(hashlib.sha256(package.read_bytes()).hexdigest(), digest)
        # Rebuilding with the same inputs is deterministic at the payload level.
        result = self.run_package()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(hashlib.sha256(package.read_bytes()).hexdigest(), digest)

    def test_missing_optional_archives_are_skipped(self) -> None:
        self.output.mkdir()
        source_package = self.output / "Encore-Pixel-10-Android17-v0.7.5-test-source.zip"
        source_package.write_bytes(b"stale")
        source_package.with_suffix(".zip.sha256").write_text("stale")
        with patch.object(PACKAGE, "BASELINE", self.root / "missing-baseline.zip"), \
             patch.object(PACKAGE, "FAS_SOURCE", self.root / "missing-fas-rs.zip"):
            self.assertIsNone(PACKAGE.build_source_bundle("0.7.5-test", self.module, self.output))
        self.assertFalse(source_package.exists())
        self.assertFalse(source_package.with_suffix(".zip.sha256").exists())

    def test_existing_baseline_is_optional_source_input(self) -> None:
        self.output.mkdir()
        baseline = self.root / "baseline.zip"
        baseline.write_bytes(b"baseline")
        with patch.object(PACKAGE, "BASELINE", baseline), \
             patch.object(PACKAGE, "FAS_SOURCE", self.root / "missing-fas-rs.zip"):
            path = PACKAGE.build_source_bundle("0.7.5-test", self.module, self.output)
        with zipfile.ZipFile(path) as archive:
            self.assertEqual(archive.read("baseline/baseline.zip"), b"baseline")
            self.assertIn("source/package.py", archive.namelist())

    def test_invalid_properties_fail_without_zip(self) -> None:
        for properties in ("version=../unsafe\nversionCode=750\n", "version=0.7.5\nversionCode=invalid\n"):
            with self.subTest(properties=properties):
                (self.module / "module.prop").write_text(properties)
                result = self.run_package()
                self.assertEqual(result.returncode, 2)
                self.assertFalse(list(self.output.glob("*.zip")))

    def test_broken_webui_build_is_rejected(self) -> None:
        (self.module / "webroot/assets/index.js").unlink()
        result = self.run_package()
        self.assertEqual(result.returncode, 2)
        self.assertIn("WebUI resource missing", result.stderr)

    def test_bare_carriage_return_in_script_is_rejected(self) -> None:
        (self.module / "service.sh").write_bytes(b"#!/system/bin/sh\recho broken\n")
        result = self.run_package()
        self.assertEqual(result.returncode, 2)
        self.assertIn("Invalid carriage return", result.stderr)


if __name__ == "__main__":
    unittest.main()
