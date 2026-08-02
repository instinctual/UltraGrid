# UltraGrid R12L Streaming Handoff

Last updated: 2026-08-01 (America/Los_Angeles)

## Repository state

- Current checkout: `/home/administrator/UltraGrid`
- Current branch: `master`
- Latest implementation commit: `00224702b` — Eliminate the Vulkan receiver
  frame copy
- Organization remote: `instinctual` (`https://github.com/instinctual/UltraGrid.git`)
- Upstream remote: `origin` (`https://github.com/CESNET/UltraGrid.git`)
- Local `master` tracks `instinctual/master`.
- The completed `decklinkscheduling` and `r12l-identity-hevc` branches were
  fully merged and deleted locally and from the `instinctual` remote.
- Current local binary SHA-256:
  `7f9097fbfb9b1ebd6220823f0d8503a377bc6881c22aac225c83e4da0c34604e`

Recent commits:

1. `b155a959b` — Document DeckLink streaming handoff
2. `e907a7ecb` — Recover DeckLink format detection after signal loss
3. `5d3032b2e` — Reduce Vulkan handoff overhead
4. `60a4e955b` — Optimize DeckLink decode scheduling
5. `a188eaace` — Optimize DeckLink buffer handoff

## Machines

| Role | Host | SSH user | Runtime unit |
|---|---|---|---|
| Encoder | `10.55.118.88` | `administrator` | `ultragrid-sender.service` |
| DeckLink receiver | `10.55.118.89` | `administrator` | `ultragrid-receiver.service` |
| Cage/HDMI receiver | `10.55.118.91` | `administrator` | `ultragrid-cage-receiver.service` |

Credentials are intentionally not stored in this repository. Obtain them
through the operator or the normal secret-management mechanism.

The encoder and DeckLink receiver runtime services are transient systemd units
created with `systemd-run`. They use `Restart=on-failure` and `RestartSec=1s`.

Both machines currently run the watchdog build at
`/usr/local/bin/uv-r12l-identity`. Its SHA-256 matches the local binary:
`7f9097fbfb9b1ebd6220823f0d8503a377bc6881c22aac225c83e4da0c34604e`.
The previously installed binary was retained on each machine as
`/tmp/uv-r12l-identity.pre-watchdog` for short-term rollback.

## Cage/HDMI receiver

The new receiver at `10.55.118.91` uses Ubuntu 26.04 Desktop, Intel DRM,
patched Cage 0.2.1, SDL3 Wayland, and the UltraGrid Vulkan display. The
deployment files and complete build, installation, verification, format-test,
and rollback instructions are in
`contrib/cage-kiosk/README.md`.

The production `ultragrid-cage-receiver.service` is installed, enabled, and
starts successfully after an unattended reboot; `gdm.service` is disabled.
Deployed SHA-256 values:

- `/usr/local/bin/uv-vulkan-sdl3`:
  `5506664e08d98f296889c776354af19b7db42fd676ab87f16f492a381c5e9662`
- `/usr/local/bin/cage-ultragrid-10bit`:
  `db503f34afe68e43d27120f9f492ef9611ce8bddaf7a566d2207edd7f8f75a9f`

The path is intentionally strict:

- patched Cage requires an `XR30` or `XB30` 10-bit render/scanout format and
  refuses an 8-bit fallback;
- `ug-drm-connector-config` discovers the connected DRM output and sets
  `Broadcast RGB=Full` by property and enum names;
- VSync remains enabled;
- PipeWire playback follows the default sink instead of a numeric node ID,
  which is not stable across reboots;
- both the UltraGrid playback stream and default HDMI sink run at `1.0` unity
  gain; the kiosk wrapper reapplies sink unity at every start;
- Vulkan receives decoded `R10k` directly through the optimized QSV/VA path;
  and
- `vulkan:...:modeset` uses wlroots output management to select the EDID mode
  matching each incoming UltraGrid description.

Mode selection requires one enabled output, an exact incoming width/height,
and the closest EDID-advertised refresh. True interlaced input uses field rate;
PsF uses frame rate. It avoids renegotiation if the correct mode is already
current. Two explicit fallbacks preserve compatibility with consumer HDMI
sinks: 2048x1080 DCI uses 1920x1080 HD when 2K DCI is absent, and 4096x2160
DCI uses 3840x2160 UHD when 4K DCI is absent. Vulkan centers and letterboxes
the DCI image instead of stretching it.

During live validation, an incoming 3840x2160p24 stream automatically changed
the physical output from its EDID-preferred 1920x1080p60 timing to
3840x2160p24. The active primary-plane framebuffer remained `XB30`, and
`Broadcast RGB=Full` verified after the change. Earlier scope validation
confirmed 10-bit only after restarting the complete Cage unit following an
HDMI/EDID-chain change; restarting UltraGrid alone is insufficient for that
case.

After the final unattended reboot, the receiver again selected
3840x2160p24, verified `Broadcast RGB=Full`, routed the UltraGrid stream to
the current HDMI default sink, and reported `1.00` volume for both the stream
and sink. The service remained active with zero automatic restarts.

The kiosk needs `PAMName=login` to create an active logind seat at unattended
boot. PAM moves Cage and UltraGrid into a login-session cgroup, so unmodified
Cage can wait indefinitely for UltraGrid during shutdown. The second included
Cage patch signals its primary client with `SIGINT` before waiting. The unit
also enables linger for `administrator`, starts `user@1000.service` for
PipeWire, uses `SIGINT` for clean shutdown, and routes output to the journal so
repeated/dropped/late-frame counters are observable remotely.

The attached Sony EDID advertises 4096x2160 DCI but not 2048x1080 DCI. Test
UHD, HD, 720p, true interlace, fractional rates, 2K DCI fallback, and exact 4K
DCI as described in the kiosk README. Validate every case on the HDMI scope
for bit depth and range, not only from the Vulkan swapchain format.

## Signal and codec path

The active path is:

1. DeckLink captures UHD 24 fps, 12-bit RGB 4:4:4 as R12L.
2. The DeckLink 16.x input buffer is pinned with
   `IDeckLinkVideoBuffer::StartAccess()` until compression consumes it.
3. Vulkan converts R12L into identity-mapped Y410/XV30:
   `Y=G`, `U=B`, and `V=R`. This avoids an RGB-to-YUV color matrix.
4. VA-API encodes HEVC 10-bit 4:4:4 RGB identity planes at 60 Mb/s using
   low-power mode, async depth 1, one slice, GOP 24, `max_b_frames=0`, and
   no all-intra mode.
5. UltraGrid transports video and eight-channel Opus audio.
6. Intel QSV decodes HEVC into a VA surface.
7. VA DMA-BUF is imported into Vulkan, which converts identity Y410 directly
   into the page-aligned R10k DeckLink output buffer.
8. DeckLink emits single-link 12G SDI, RGB 4:4:4, with the R10k full-range
   path enabled.

The repository uses native DeckLink SDK 16.0 headers.

## Known-good runtime commands

Encoder:

```text
/usr/local/bin/uv-r12l-identity -4 -V \
  -t decklink:codec=R12L:nosig-send:passthrough \
  -s embedded -a channels=8 \
  -A Opus:bitrate=192k \
  --param audioenc-frame-duration=5 \
  -c libavcodec:encoder=hevc_vaapi:rgb:depth=10:subsampling=444:bitrate=60000000:low_power=1:async_depth=1:slices=1:gop=24:disable_intra_refresh \
  10.55.118.89
```

Receiver:

```text
/usr/local/bin/uv-r12l-identity -4 -V \
  -d decklink:single-link:synchronized=3 \
  -r embedded \
  --param bmd-r10k-full-range \
  --param decoder-use-codec=R10k \
  --param decoder-drop-policy=blocking \
  --param force-lavd-decoder=hevc_qsv \
  --param low-latency-video
```

Do not add `audio-lavc-decoder=libopus`; native Opus decoding is known-good.

## Required DeckLink behavior

- Output link configuration is single-link:
  `bmdLinkConfigurationSingleLink`.
- RGB 4:4:4 SDI output is enabled:
  `bmdDeckLinkConfig444SDIVideoOutput=true`.
- Full-range R10k behavior is selected with `--param bmd-r10k-full-range`.
- Video is the A/V master.
- Receiver scheduled depth must remain `synchronized=3`.
- Audio is gated until video playback has started and is timestamped against
  the video RTP epoch.

Do not reduce the DeckLink scheduled depth to 2. Testing the untouched
known-good scheduler at depth 2 produced sustained late frames, dropped
frames, and audio underflow. Depth 3 is the stable floor on this hardware.

## Current optimizations

- Balanced DeckLink capture buffer access on every success and failure path.
- Direct QSV/VA DMA-BUF Vulkan Y410-to-R10k conversion.
- Direct Vulkan writes into page-aligned DeckLink output buffers through
  `VK_EXT_external_memory_host`, with automatic fallback to the prior copy.
- Cached Vulkan imports for the reusable QSV/VA surface pool.
- Reusable Vulkan fences instead of `vkQueueWaitIdle()` on encoder and
  receiver conversions.
- One Vulkan descriptor-set update per decoded frame instead of two.
- Playback begins from the first real decoded frame instead of blank preroll.
- Audio scheduling remains disabled until video-master playback starts.
- Compact cumulative DeckLink counters report late, dropped, flushed,
  repeated, dismissed, schedule-failure, and audio-underflow events.
- A no-signal watchdog re-arms DeckLink format detection after two seconds of
  continuous signal loss. Recovery runs from the capture thread rather than
  the DeckLink callback.
  - It stops the stream, disables video input, re-enables the last mode with
    `bmdVideoInputEnableFormatDetection`, and starts the stream.
  - Retries back off at 5, 10, 20, then 30 seconds during sustained loss.
  - Any valid frame resets the watchdog and its backoff.
  - State is tracked independently per DeckLink device.

No optimization above adds a frame queue or increases scheduled depth.

## Last measured steady state

After deploying the watchdog build and restarting both runtime units:

- Encoder capture: approximately 24 fps.
- Encoder encode path: approximately 15.9–17.0 ms total.
  - R12L conversion: approximately 6.8–6.9 ms.
  - VA encoder submit/output: approximately 9.0–10.2 ms.
- Receiver output: approximately 24 fps.
- Receiver decode path: approximately 10–11 ms.
- Steady receiver counters: 0 late, 0 dropped, 0 schedule failures.

Counter increases during a deliberate sender restart are expected. Confirm
that counters become flat after the stream recovers.

## Signal-loss and format-change validation

The deployed build was exercised with repeated SDI interruptions, SDI format
changes, and a reboot of the machine producing the signal:

- UHD 2160p24 RGB 12-bit R12L and 525i59.94 YCbCr 10-bit v210 both
  reconfigured end-to-end without restarting UltraGrid.
- During the producer reboot, DeckLink briefly detected 1080i59.94 and then
  switched to the final 2160p24 R12L signal. Normal format callbacks arrived,
  so the watchdog correctly did not cycle the input.
- The original failure mode was a persistent old-mode stream whose frames all
  carried `bmdFrameHasNoInputSource`, with no format-change callback after the
  source returned. The watchdog specifically targets this condition.
- Short or intermittent losses do not trigger the watchdog unless no valid
  frame is received continuously for two seconds.
- Format transitions can temporarily add repeated/dismissed frames and audio
  underflows. In successful tests these counters became flat after recovery,
  while late, dropped, and schedule-failure counters remained zero.

## Important rejected experiments

- `synchronized=2`: unstable on the receiver.
- Custom adaptive 2–3-frame scheduling: entered late/drop/underflow loops.
- Immediate scheduling and clock re-anchoring variants: unstable.
- QSV/DeckLink scheduling changes that removed required synchronization:
  rejected because they risk partial or stale frames.
- Directly importing the DeckLink capture pointer as Vulkan external host
  memory: not implemented because DeckLink does not guarantee the allocation
  size, alignment, or lifetime required by Vulkan.
- Receiver parameters that removed R10k full-range handling caused incorrect
  colors.
- Multiple or forced Opus decoder configurations previously caused static;
  native Opus decode with clean restarts is known-good.

Source clip changes or gaps can temporarily reduce received video packets and
cause repeated frames. Distinguish this from network loss by checking RTP
packet counters and whether DeckLink counters stop increasing after recovery.

## Build and validation

From the repository root:

```bash
make -j"$(nproc)"
make check
```

The test suite prints an existing
`exit() called unexpectedly! Maybe by some library?` line during the FreeBSD
network test, but the test completes as `Ok`. GPUJPEG may be skipped.

Verify a clean tree before deployment:

```bash
git status -sb
git diff --check
sha256sum bin/uv
```

## Deployment outline

1. Build and run `make check`.
2. Copy `bin/uv` to `/tmp` on each target.
3. Install it as `/usr/local/bin/uv-r12l-identity`.
4. Stop the existing transient unit.
5. Recreate the unit with `systemd-run`, using the exact commands above.
6. Compare the SHA-256 of the installed binary on both machines with
   `bin/uv`.
7. Wait through startup, then verify:
   - both services are active;
   - encoder capture is 24 fps;
   - receiver display is 24 fps;
   - late/dropped/schedule-failure counters remain flat;
   - audio underflows do not continue;
   - QSV/VA Vulkan direct conversion is active.

Do not place passwords directly in this document, shell history, commits, or
service command lines.

## Checkout location

The repository move to `/home/administrator/UltraGrid` is complete. A normal
incremental build and the full test suite passed from this location; no
autotools regeneration was required.
