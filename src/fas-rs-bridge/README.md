# Experimental Pixel fas-rs bridge

This integrates pinned fas-rs v4.9.1. It is not the unmodified upstream daemon
and has not yet been validated as a complete feedback loop in an Android 17 match.

The bridge compiles upstream frame-time buffers, target matching,
proportional control, utilization tracking and CPU frequency calculation.
Documented CPU-controller patches keep thread discovery active during
janked startup and use the upstream all-core fallback before observations.
UPSTREAM.json records source revisions and hashes. Frame capture uses the
upstream frame-analyzer 0.3.4 eBPF implementation. Its build script and inherited
Cargo metadata are adapted for an explicit BPF build. Android 17 also requires
a fallback to the exported Surface::hook_queueBuffer symbol.

The original sysfs FileHandler is replaced with an in-memory request sink.
The bridge cannot open/write/chmod/unmount CPU nodes. Output records contain
PID/start identity, monotonic timestamp, sequence, FPS, readiness and per-policy
minimum kHz. The Apache Pixel controller communicates with this separate GPL
executable over a pipe.

Pixel remains the only frequency writer. It validates identity and freshness,
clamps requests to live OPPs, the existing bounded boost envelope and thermal
ceilings, and journals/restores its votes. This restricted output backend
differs from upstream min/max locking. GPU/DSU/IRM retain baseline floors;
CPU recommendations do not drive custom GPU boosts.

Stale data, helper termination, foreground departure and thermal ineligibility
release upstream requests. No old custom FPS-guessing fallback is used. The old
guardian/sampler loop is disabled in this preview because it contains another
target inference and uclamp controller. Its source remains for comparison.

Explicit FPS settings are preserved. Auto (0) uses pinned per-game candidate
rates; unknown games use [30,45,60,90,120]. Package refresh no longer rewrites
Auto. This preview supports rates up to 120.

Build using build.py with ANDROID_NDK_HOME, Rust stable with the Android target,
nightly-2026-02-01 with rust-src, and bpf-linker 0.9.15 (rust-llvm-21).
Tests use cargo test --no-default-features and the separate C++ protocol test.
No auto deployment or public release is performed.

Device validation (2026-10-28): preview2 loads/attaches the eBPF probe on the
connected Pixel 10 Pro XL and receives PUBG frame events. Isolated Auto sampling
selects 120 FPS and produces changing CPU requests. These requests were not
applied to sysfs during this test; this is not a performance comparison.

Upstream target matching treats even a single fixed candidate as conditional:
with a 120-only list it may clear the target below 110 observed FPS, resetting
the warm-up buffer. Auto [60,90,120] avoids that single-candidate threshold, but
may choose a lower target. No custom recovery inference is added here.

Preview3 fixes process_monitor parsing for comm names containing spaces or
parentheses in /proc/PID/task/TID/stat. A Pixel PUBG thread named TQM free m(dwt)
was observed with nonzero CPU time but zero in the old parser. Nine Rust tests
pass; the patched helper passes the device probe and isolated frame capture.
It has not been installed as the active helper or shown to improve stutter.
The upstream Auto target switching/reset policy is unchanged.
