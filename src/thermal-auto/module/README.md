# Pixel Game Thermal 1.6.1 — automatic configuration discovery

This module targets Pixel Tensor devices on Android 17. At installation it scans the available `thermal_info_config*.json` files and selects the single file whose Thermal schema matches the supported virtual-skin sensors and cooling-device graph. It does not rely on a fixed filename or a hard-coded SHA-256 of the stock file.

The selected stock file is cached in `profiles/stock.json` and keyed by the complete system build fingerprint. A native RapidJSON builder creates `profiles/game.json` by changing only the approved game fields: polling intervals, virtual-skin thresholds, and the CPU/GPU/uclamp ceiling slots used by the tested profile. Stock and overlay files are promoted atomically with retries. Duplicate sensors, missing cooling devices, malformed JSON, unsupported target frequencies, or more than one matching file cause installation or boot validation to fail closed.

The module still uses the existing Game/Stock selection for the next reboot. The selected file name and profile hashes are stored in `state/config.env`, and runtime checks verify that the mounted vendor file matches the generated profile and that the Thermal HAL is ready.

This is schema-adaptive configuration discovery, not a general-purpose thermal tuner. A future firmware with a materially different Thermal schema is rejected until its targets are reviewed.
