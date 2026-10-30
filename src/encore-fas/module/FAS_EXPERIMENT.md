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
Preview2 adds Android 17 hook symbol resolution and keeps thread discovery
active during janked startup. Empty thread observations use the upstream
all-core fallback. All changes to upstream are recorded in UPSTREAM.json.

Nine Rust tests and the existing IPC/helper-lifecycle tests pass. On the
connected Pixel 10 Pro XL the eBPF probe attaches and isolated PUBG sampling
receives real frame events, matches Auto to 120 FPS and exports changing CPU
requests. The isolated tests do not write frequencies. Full-loop installation,
long-match performance, power and audio remain unverified for preview2.

A 120-only candidate retains the upstream minimum matching threshold: below
110 observed FPS the target can clear, restarting warm-up. Auto may instead
match a lower candidate. Configuration write durability and automatic
corrupt-list recovery are separate outstanding work.

Preview3 corrects proc stat CPU-time parsing for thread names containing spaces
and parentheses, with malformed-data tests and saturating tick subtraction.
Probe and isolated device capture pass; complete active-loop validation of
this change is pending. Auto target switches still reset the upstream buffer
and can interrupt requests during warm-up. No smoothing or target-lock policy
is added in this revision.

Preview4 retains the last selected target through brief unmatched FPS dips,
uses a 3 FPS hysteresis band and a two-second confirmation before switching,
and keeps the rolling frame history and usable controller state across target
changes. This is intended to prevent repeated warm-up gaps while Auto moves
between candidate rates. It does not remove CPU thermal caps or boost GPU.
Twelve Rust tests pass. Preview4 needs a device flash and comparable
SurfaceFlinger match measurements before its effect can be established.
