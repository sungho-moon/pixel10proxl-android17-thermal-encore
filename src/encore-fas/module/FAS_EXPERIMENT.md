# Encore Pixel Tensor Android 17 0.2.3 generic adviser

This is a userspace adaptation of the frame-deficit concept in
https://github.com/rem01project/encore_fas, pinned at commit
46d5af8e8756467b636a1da0c2227ee74abd1225. It does not include a
kernel module. Only the existing Pixel controller writes frequency requests.
The tracefs uprobe observes Surface::hook_queueBuffer only for a separately
validated libgui hash and offset. Unknown Android 17 QPR builds keep the
baseline Encore controller and refuse to guess a trace offset.

The proportional adviser independently reimplements the frame-error equation
from fas-rs v4.9.1: `control_khz = 300000 * (frame_seconds * target_fps - 1)`.
It uses the 90th-percentile interval among 30–60 recent frames after a sustained
deficit is detected. Advice levels 1–2 add one Encore OPP step; advice level 3
can add two steps. The combined result remains bounded to levels 0–3, and the
raised tiered temperature cap is applied afterward. Encore alone applies and journals
the seven minimum-frequency votes. The original fas-rs executable is neither
included nor started by this module. Source:
https://github.com/shadow3aaa/fas-rs/tree/v4.9.1 .

FAS is enabled by default for every game registered in Encore Pixel's game
list while that game is foreground. It is independent of the separate
high-frame-rate policy switch. To disable FAS immediately as root:

    touch /data/adb/.config/encore_pixel_cp41/disable-fas

Remove that file to enable FAS again. The controller reacts in its normal
2-second loop and releases any short boost when disabled. The WebUI does not
yet expose this switch.

To disable only the fas-rs adviser while leaving Encore's original FAS detector
active, create `/data/adb/.config/encore_pixel_cp41/disable-fas-rs-advisor`.
Remove it to re-enable the adviser; the controller reads it each loop.

Targets are configured in
The WebUI game page stores `target_fps` in
`/data/adb/.config/encore_pixel_cp41/gamelist.json` for each enabled game:

    com.tencent.tmgp.pubgmhd 120
    com.tencent.tmgp.dfm 120

The installer creates these two entries only when the file does not exist.
Choose 30, 60, or 120 FPS in WebUI. New games are assigned 120 FPS only when
their package is in the maintained competitive FPS/MOBA preset table;
otherwise they receive 60 FPS. FAS never infers a target from a temporary
drop in measured frame rate; it only feeds back against the configured target.

The detector follows the game's dominant queueBuffer thread and uses a
clipped, one-sided frame-deficit CUSUM with 5% tolerance. After acquisition,
a sustained deficit can request a bounded boost when the current rate is
roughly 40–98% of the configured target.
The aggressive fas-rs adviser adds one bounded level for any proportional
error and can add two for a severe error. Requests return to the
normal floor as soon as the deficit clears; there is no fixed hold or cooldown.
Quiet scenes disarm the detector.

The highest extra level requests up to CPU policy0 1036 MHz,
policy2/5 1785 MHz, policy7 1766 MHz, and GPU 691 MHz, with intervening
levels chosen from each policy's actual OPP table. System and thermal maximum
limits continue to control actual clocks. Additional FAS levels are capped at
battery temperatures 40, 41 and 42 C, then released; at 42 C the base request
switches to Lite. The whole controller remains subject to the existing 43 C
protection with recovery at 41 C. The probe stops when the game
leaves foreground, the display sleeps, battery saver is on, Lite mode is
active, or battery capacity falls below 20%.

The probe event and tracefs instance are removed when FAS stops. A durable
same-boot marker lets the controller remove a stale event after a crash.
The frequency journal preserves the original requests and restores them on
exit; a conflicting external vote is not overwritten.

The adviser and adaptive controller pass ARM64 compilation and synthetic node
tests. In a 90-second foreground PUBG Mobile test on this CP41 device, two
advice changes raised Encore's level from 1 to 2; the seven votes and Android
frame policy restored when the test ended. This establishes a real control
contribution, **not** an FPS or stability benefit. If a game behaves poorly,
create `disable-fas-rs-advisor`, then `disable-fas` if needed, or pause the
module from KernelSU and inspect the controller log.

