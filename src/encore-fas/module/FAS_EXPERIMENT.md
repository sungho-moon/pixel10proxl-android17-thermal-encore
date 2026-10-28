# fas-rs upstream bridge preview

This preview replaces the previous custom FrameObserver, nearest-tier target
inference and proportional adviser with a separate GPL Rust program using
fas-rs v4.9.1 source. Frame capture uses frame-analyzer 0.3.4 eBPF.

Per-game Auto matches upstream candidate rates. Existing fixed settings are
preserved as optional advanced overrides. Package refresh no longer changes
Auto to a competitive preset. The old guardian/sampler uclamp controller is
disabled in the preview to avoid running a second frame-feedback algorithm.

The bridge exports minimum CPU requests to Pixel over an anonymous pipe. It
does not write frequency nodes. Pixel validates PID/start-time identity,
sequence and timestamps, applies its live OPP and thermal bounds, journals the
minimum-frequency votes and restores them on exit. GPU/DSU/IRM remain at their
baseline game floors. Upstream min/max frequency locking is not used.

The FAS switch disables the whole bridge. The old independent fas-rs adviser
switch is removed. Missing/failed bridge or stale output yields baseline
scheduling only, with no custom inference fallback.

Source, upstream revisions and build instructions: src/fas-rs-bridge.
Matching, request-sink, IPC and helper-lifecycle tests pass on the host; Android
executables and eBPF compile. No device probe, match test or performance
improvement has yet been established for this preview. Configuration write
durability and automatic corrupt-list recovery are separate outstanding work.
