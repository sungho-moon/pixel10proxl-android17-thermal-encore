#!/usr/bin/env python3
"""Build the pinned upstream BPF program, tests and Android bridge."""
import os
from pathlib import Path
import subprocess

base = Path(__file__).resolve().parent
root = base.parent.parent
build = Path(os.environ.get('FASRS_BUILD_DIR', root / 'work/fasrs-build')).resolve()
ndk = Path(os.environ['ANDROID_NDK_HOME']).resolve()
env = dict(os.environ)
env['CARGO_TARGET_DIR'] = str(build)
env['CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER'] = str(ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang')
subprocess.run(['cargo', '+nightly-2026-02-01', 'build', '--locked', '--release', '--target', 'bpfel-unknown-none', '-Z', 'build-std=core'], cwd=base / 'upstream/frame-analyzer/frame-analyzer-ebpf', env=env, check=True)
env['PIXEL_FAS_BPF_OBJECT'] = str(build / 'bpfel-unknown-none/release/frame-analyzer-ebpf')
subprocess.run(['cargo', '+stable', 'test', '--locked', '--no-default-features'], cwd=base, env=env, check=True)
subprocess.run(['cargo', '+stable', 'build', '--locked', '--release', '--target', 'aarch64-linux-android'], cwd=base, env=env, check=True)
print(build / 'aarch64-linux-android/release/pixel-fas-rs-bridge')
