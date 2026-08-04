# UltraGrid R12L Streaming Handoff

Last updated: 2026-08-03 (America/Los_Angeles)

## 2026-08-03 production validation checkpoint

- UltraGrid recovery commit `03bd47a32` is pushed to
  `instinctual/master`. SRT Server release commit `b912015` is pushed to
  `origin/main`; both local branches matched their remotes before shutdown.
- `/home/administrator/srtserver` is at release 0.3.6. Production relay
  `172.25.5.100` runs that package and passes its health check.
- Relay route `encoder04` and destination `receiver06` are enabled.
  `preserveSourceTime` is disabled on both, preventing stale source timestamps
  from poisoning a newly connected output socket.
- Encoder `172.25.5.74` currently reaches the relay through the transient
  `colorconnect-relay-encoder04-unrestricted.service`. Its SRT URI has no
  `maxbw`, `inputbw`, or `oheadbw` cap, and sustained production traffic showed
  zero sender loss and retransmissions.
- After the clean ColorConnect install, `/opt/instinctual/colorconnect/encoder`
  is absent on `172.25.5.74`. The enabled but inactive
  `colorconnect-encoder.service` and `colorconnect-srttransmit.service` still
  reference scripts below that missing directory. The working relay
  transmitter is therefore not persistent across reboot yet.
- The receiver at `172.25.5.6` runs the QSV recovery build at
  `/usr/local/bin/uv`. Its SHA-256 is
  `1bf6c3f1df8634ba4e1d1cef36d949ee23bd3e18d7ddb6a672b8a683f4f6810d`;
  the previous binary is `/usr/local/bin/uv.pre-qsv-recovery`.
- Hardware decoding now drops corrupt/incomplete RTP frames before QSV and
  recreates the decoder in-process after a QSV input/output error. HEVC waits
  for VPS plus an IRAP access unit, and H.264 waits for SPS plus IDR, before
  resuming. This avoids exiting UltraGrid, an external decoder watchdog, or
  permanently increasing SRT latency.
- The relay fix plus the uncapped encoder path restored stable video. Receiver
  sampling showed complete RTP windows at approximately 24 fps and no further
  QSV GPU hangs.
- The standalone UDP diagnostic now lives in
  `/home/administrator/srtserver/tools/srt_udp_probe.cpp`; it has been removed
  from this repository.
- `/home/administrator/colorconnect` branch `nextgen` matches
  `origin/nextgen` at `cf2ac7b`; cap removal is commit `e293c36`. Its only
  untracked items are a nested 184 MB UltraGrid build checkout and three
  downloaded InfluxData archive-key files, which are installation artifacts
  and were intentionally not committed.
- The ColorConnect Git remote contains an embedded personal access token.
  Rotate that credential and replace the remote with a credential-managed URL;
  the token value is intentionally not recorded here.

The detailed relay release hashes, package checksums, deployed paths, tests,
and version workflow are in `/home/administrator/srtserver/HANDOFF.md`.

## Repository state

- Current checkout: `/home/administrator/UltraGrid`
- Current branch: `master`
- Current validated recovery commit: `03bd47a32` — Recover QSV decoding after
  corrupted input
- Latest deployed implementation commit: `609cd4b5c` — Harden Cage kiosk
  restart behavior
- Organization remote: `instinctual` (`https://github.com/instinctual/UltraGrid.git`)
- Upstream remote: `origin` (`https://github.com/CESNET/UltraGrid.git`)
- Local `master` tracks `instinctual/master`.
- The completed `decklinkscheduling` and `r12l-identity-hevc` branches were
  fully merged and deleted locally and from the `instinctual` remote.
- Current repository build (`bin/uv`) SHA-256:
  `15b9f496ae3c7f506ff36217b9a53c08dc3a4caf2d940682a750072c6a26267c`

Recent commits:

1. `03bd47a32` — Recover QSV decoding after corrupted input
2. `609cd4b5c` — Harden Cage kiosk restart behavior
3. `febb67675` — Add automatic 10-bit Cage kiosk output
4. `00224702b` — Eliminate the Vulkan receiver frame copy
5. `33e8f6efb` — Update streaming handoff after watchdog deployment

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

## End-of-session operational state

Leave both active machines running unless the operator requests a test:

- `10.55.118.88`: `ultragrid-sender.service` is active, has zero service
  restarts, captures approximately 24 fps, and sends to `10.55.118.91`.
- `10.55.118.91`: `ultragrid-cage-receiver.service` is active and configured
  with `Restart=always`. Its single recorded restart is the successful `q`
  hotkey recovery test.
- The current physical HDMI mode is 1920x1080p24, selected automatically from
  the HD incoming stream.
- `Broadcast RGB=Full` verifies on `/dev/dri/card1 HDMI-A-1`.
- PipeWire routes the active UltraGrid stereo stream to the Sony HDMI sink;
  both stream and sink gains are `1.00`.
- The encoder's sampled RTCP reports show 0.00% packet loss.

Quick read-only checks for the next session:

```bash
systemctl --no-pager --full status ultragrid-sender.service
ssh administrator@10.55.118.91 \
  'systemctl --no-pager --full status ultragrid-cage-receiver.service'
ssh administrator@10.55.118.91 \
  'XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 wlr-randr'
ssh administrator@10.55.118.91 \
  '/usr/local/bin/ug-drm-connector-config --verify-only'
```

Remaining Cage receiver validation:

1. Exercise more SDI formats without restarting UltraGrid: fractional and
   integer HD rates, 720p, true interlace, 2K DCI fallback, and exact 4K DCI.
2. For every transition, confirm picture and synchronized audio recover,
   `wlr-randr` selects the expected EDID timing, Full RGB remains set, and the
   HDMI scope still reports 10-bit.
3. Fix detailed Cage/UltraGrid log capture under the PAM-backed kiosk so
   repeat/drop/late counters can be inspected remotely.
4. Do not start the deferred intra-refresh experiment until explicitly
   requested; the known-good production encoder remains `hevc_vaapi` with
   `disable_intra_refresh`.

## Cage/HDMI receiver

The new receiver at `10.55.118.91` uses Ubuntu 26.04 Desktop, Intel DRM,
patched Cage 0.2.1, SDL3 Wayland, and the UltraGrid Vulkan display. The
deployment files and complete build, installation, verification, format-test,
and rollback instructions are in
`contrib/cage-kiosk/README.md`.

The Cage changes are maintained in the organization fork
`https://github.com/instinctual/cage`:

- branch: `ultragrid-kiosk-0.2.1`
- release tag: `v0.2.1-ultragrid.1`
- strict 10-bit commit: `1e64458415db5e188a9165b2c6342af8195ae182`
- primary-client shutdown commit:
  `f34e1be8eb7e7ad9f7b98d12e3da34bc06366ad1`

The fork's upstream-tracking `master` remains unchanged. Build kiosk releases
from the pinned UltraGrid tag, not from the moving default branch. The patch
files remain in this repository as an offline fallback.

The kiosk unit must use `Restart=always`, not `Restart=on-failure`. Pressing
UltraGrid's `q` hotkey cleanly exits UltraGrid, Cage then exits successfully,
and systemd otherwise leaves the kiosk stopped. With `Restart=always`, the
complete Cage/UltraGrid session respawns after two seconds. Keyboard input
remains available for intentional UltraGrid hotkeys.

The production `ultragrid-cage-receiver.service` is installed, enabled, and
starts successfully after an unattended reboot; `gdm.service` is disabled.
Deployed SHA-256 values:

- `/usr/local/bin/uv-vulkan-sdl3`:
  `5506664e08d98f296889c776354af19b7db42fd676ab87f16f492a381c5e9662`
- `/usr/local/bin/cage-ultragrid-10bit`:
  `db503f34afe68e43d27120f9f492ef9611ce8bddaf7a566d2207edd7f8f75a9f`
- `/usr/local/libexec/ultragrid-cage-client`:
  `8c7e40ea53640bb864d8b8d79b199e390ddbb714a6e25fbfaa463970d3aa71b1`
- `/etc/systemd/system/ultragrid-cage-receiver.service`:
  `16f63d6c69f0809604dea705992d5f27e759481ee822091fe7148b790c6df704`

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
and sink.

At the end of the 2026-08-01 session, the incoming stream had changed to HD
and the physical output correctly followed it at 1920x1080p24. Full RGB still
verified, the HDMI sink and UltraGrid stream were active at unity gain, and
the kiosk service was active. Its restart counter was 1, corresponding to the
successful test in which UltraGrid was exited with the `q` hotkey and the
complete kiosk respawned after two seconds.

The kiosk needs `PAMName=login` to create an active logind seat at unattended
boot. PAM moves Cage and UltraGrid into a login-session cgroup, so unmodified
Cage can wait indefinitely for UltraGrid during shutdown. The second included
Cage patch signals its primary client with `SIGINT` before waiting. The unit
also enables linger for `administrator`, starts `user@1000.service` for
PipeWire, and uses `SIGINT` for clean shutdown. The unit requests journal
output, but detailed Cage and UltraGrid runtime lines are currently not
appearing in `journalctl`; only systemd and PAM lifecycle lines are present.
Do not interpret the absence of repeated/dropped/late messages there as zero
events. Fix receiver log capture or add a separate stats path in a future
session.

The attached Sony EDID advertises 4096x2160 DCI but not 2048x1080 DCI. Test
UHD, HD, 720p, true interlace, fractional rates, 2K DCI fallback, and exact 4K
DCI as described in the kiosk README. Validate every case on the HDMI scope
for bit depth and range, not only from the Vulkan swapchain format.

## Signal and codec path

The active Cage/HDMI path is:

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
7. VA DMA-BUF is imported directly into the Vulkan SDL3 display path.
8. Patched Cage composites into an `XR30` or `XB30` 10-bit KMS framebuffer
   and emits Full-range RGB over HDMI at the EDID mode selected from the
   incoming UltraGrid description.

The legacy DeckLink receiver at `10.55.118.89` remains available. Its final
stages instead convert identity Y410 through Vulkan directly into the
page-aligned R10k DeckLink output buffer, then emit single-link 12G SDI RGB
4:4:4 with full-range R10k handling.

The repository uses native DeckLink SDK 16.0 headers.

## Known-good runtime commands

Active encoder to Cage/HDMI receiver:

```text
/usr/local/bin/uv-r12l-identity -4 -V \
  -t decklink:codec=R12L:nosig-send:passthrough \
  -s embedded -a channels=8 \
  -A Opus:bitrate=192k \
  --param audioenc-frame-duration=5 \
  -c libavcodec:encoder=hevc_vaapi:rgb:depth=10:subsampling=444:bitrate=60000000:low_power=1:async_depth=1:slices=1:gop=24:disable_intra_refresh \
  10.55.118.91
```

The active Cage receiver command is maintained in
`contrib/cage-kiosk/ultragrid-cage-client` and currently resolves to:

```text
/usr/local/bin/uv-vulkan-sdl3 -4 -V \
  -d vulkan:fs:modeset:gpu=integrated:display=0 \
  -r pipewire \
  --param decoder-use-codec=R10k \
  --param decoder-drop-policy=blocking \
  --param force-lavd-decoder=hevc_qsv \
  --param low-latency-video
```

The wrapper first verifies wlroots output management and sets the default
PipeWire sink to `1.0`. The `-4` option forces IPv4; `-V` enables video.

Legacy DeckLink receiver:

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
To send to the legacy DeckLink receiver instead, change only the encoder's
destination from `10.55.118.91` to `10.55.118.89`.

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

Current encoder measurements while sending 1920x1080p24 to the Cage receiver
at the end of the 2026-08-01 session:

- DeckLink capture: approximately 24 fps.
- Encode path: approximately 6.0 ms total.
  - R12L conversion: approximately 2.8 ms.
  - Surface upload: 0.0 ms (direct path).
  - VA encoder submit/output: approximately 3.2 ms.
- RTCP receiver reports showed 0.00% packet loss in the sampled intervals.

Earlier measurements after deploying the watchdog build to the UHD
DeckLink-to-DeckLink path:

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

## Deferred intra-refresh work

The current sender intentionally uses
`hevc_vaapi:...:gop=24:disable_intra_refresh`. At 24 fps this provides a full
intra/key frame approximately once per second; it does not use cyclic or
rolling intra-refresh.

The encoder's Intel Raptor Lake-P VA-API driver reports rolling-column and
rolling-row intra-refresh support for the exact
`VAProfileHEVCMain444_10/VAEntrypointEncSliceLP` profile used by the 10-bit
4:4:4 path. The installed FFmpeg `hevc_vaapi` wrapper does not expose a
corresponding intra-refresh option, however. Merely removing
`disable_intra_refresh` from the UltraGrid command would not configure the
VA-API rolling-refresh controls.

FFmpeg's `hevc_qsv` encoder does expose `int_ref_type` values for vertical,
horizontal, and slice refresh, along with cycle-size, cycle-distance, and QP
delta controls. It accepts `xv30le`, and UltraGrid's QSV configuration already
requests vertical intra-refresh with a 20-frame cycle when periodic refresh is
enabled.

Do not switch the production sender to QSV as a one-line command change. The
current optimized R12L-to-identity XV30/Y410 conversion and direct VA surface
path were built around `hevc_vaapi`; QSV must first be shown to preserve the
identity color mapping and 10-bit 4:4:4 profile without adding an expensive
copy or matrix conversion.

If this work is resumed, test QSV side-by-side while leaving the known-good
VA-API sender available for immediate rollback. Validate:

- R12L identity color and range through the HDMI/SDI scopes;
- HEVC Main 4:4:4 10-bit profile and decoder compatibility;
- direct-surface behavior and R12L conversion/upload/encode timings;
- bitrate peaks compared with the current 24-frame GOP;
- recovery after packet loss and when a receiver joins mid-cycle;
- recovery-point signaling and parameter/header availability; and
- absence of rolling-refresh artifacts over repeated cycles.

Rolling refresh should smooth the once-per-second full-intra bitrate peak, but
its recovery occurs across the configured cycle rather than in one frame. At
24 fps, the existing UltraGrid QSV default of 20 frames would take about
0.83 seconds per refresh cycle, so measure both bitrate smoothness and actual
error-recovery time before adopting it.

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
