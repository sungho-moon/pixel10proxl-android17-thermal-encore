# Thread guardian

The guardian resolves `Surface::hook_queueBuffer` from the current
`/system/lib64/libgui.so` at every session start. Executable `PT_LOAD` segments
translate the symbol's virtual address into the file offset required by uprobes.
An unavailable, malformed, or ambiguous symbol returns exit code 17; the thread
service then stops this backend instead of falling back to an old offset.

## Build

With Android NDK r27c on Linux or WSL:

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk-r27c
sh work/guardian-build/build.sh /path/to/guardian
```

The build targets arm64 Android API 35 and supports 16 KiB pages. The module's
Android SDK 37 gate is unchanged. `NDK_HOST_TAG` defaults to `linux-x86_64`.
For the source ZIP, use `source/guardian-build/build.sh` with an explicit output.

## Validation

- Build `test_elf_offset.c` with the same compiler and run it on the device.
  Its eleven fixtures cover symbol lookup, address translation, malformed ELF,
  non-executable targets, undefined symbols, alignment, and ambiguity.
- Build and run `test_policy.c` and `test_targets.c`; run the latter from a
  writable directory. It uses its own `dynamic-tids-test` fixture.
- `guardian --resolve-offset /system/lib64/libgui.so` is read-only.
- `guardian --selftest /data/local/tmp/TEST_STATE` requires an existing writable
  state directory. It verifies inherited uclamp cleanup and journal recovery
  using its own child processes.
- Check live `PROBE_READY`, `FRAME_STATS valid=1`, and `APPLIED` records, followed
  by `RESTORED` and `GUARDIAN_END code=0` after the game leaves the foreground.
