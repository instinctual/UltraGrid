# UltraGrid R12L Streaming Handoff

Last updated: 2026-08-18 (America/Los_Angeles)

## 2026-08-17 CESNET upstream catchup checkpoint

- Work is on branch `catchup`, pushed to `instinctual/catchup` at merge commit
  `97a2c1393` (`Merge CESNET master into catchup`). It was created from our
  production `master` commit `4fe234f8f`. **Do not treat this as merged or
  deployed:** `master` remains unchanged and `catchup` still requires live
  encoder/receiver validation.
- `catchup` merges CESNET `origin/master` through `b037a0c57` (2026-08-14).
  At this checkpoint it is zero commits behind that upstream ref and 25
  commits ahead, counting our 24 pre-existing commits plus the merge commit.
- The sole merge conflict was `src/rxtx/ultragrid_rtp.c`. It was resolved by
  retaining our `--param low-latency-video` zero-frame RTP playout option while
  also taking upstream's atomic exit flag and asynchronous-send race fixes.
- The most relevant upstream correctness fixes now included are:
  - `72ca09588`: lock the UDP receiver exit flag;
  - `3d5a18204`: make the UltraGrid RTP exit flag atomic;
  - `8063ceb50`: remove the asynchronous RTP sender exit race;
  - `e886981fc`: avoid dereferencing a missing decoder output frame;
  - `6d34a1c8f`: initialize the reported incoming-format string; and
  - `cd15100a1`: eliminate the asynchronous RTP sender's per-frame allocation.
- DeckLink and audio timing changes (`acea14db9`, `8f7295eb4`, `6a1eaa198`,
  `9b4e9a1c4`, and `c9ec8bd8d`) use monotonic clocks. They improve diagnostic
  timing and make DeckLink `drift_fix` robust against wall-clock changes, but
  they do not otherwise change audio gain, latency, or synchronization.
- The PipeWire changes around `bba997f77` are defensive RAII/event-structure
  cleanup. They do not fix sink selection, gain, or the earlier missing
  WirePlumber/session-manager failure. Removing compatibility for PipeWire
  older than 0.3.49 is acceptable for the supported Ubuntu 24.04/26.04 hosts.
- CESNET's explicit R12L/Y416 fake-conversion filters and generic Y416
  conversions are included but are not used by our production direct
  QSV/Vulkan identity path. If that experimental postprocess is evaluated,
  first fix `state_vopp_y416_to_r12l_fake::full_range`: upstream allocates the
  state with `malloc()` and does not initialize the field on the default path.
  Also remember that the fake filters default to limited-range scaling;
  `:full-range` is required for bit-identity behavior.
- The merged tree compiled successfully with `make -j8`. `make check` passed
  codec conversions, QSV hardware-recovery logic, capture/display discovery,
  IPv4 loopback/unicast, and the remaining unit tests. The multicast networking
  cases failed because this workstation has no multicast loopback route; this
  is an environment limitation, not an observed catchup regression.
- Preservation checks confirmed that the custom QSV R12L identity encoder,
  cyclic intra-refresh, decoder recovery, Vulkan/Cage modesetting, DeckLink
  signal-loss recovery, and `low-latency-video` code remain present after the
  merge. Physical encoder, DeckLink receiver, Cage/PipeWire receiver, SDI
  interruption, color/range, audio-sync, and soak testing are still required
  before merging `catchup` into `master`.
- Deferred DeckLink latency experiment: compare the known-good
  `--param low-latency-video` plus `synchronized=3` receiver against a receiver
  with the parameter removed plus `synchronized=2`. The latter restores one
  frame of RTP playout delay while removing one frame of minimum DeckLink
  scheduling depth. Measure end-to-end A/V latency and require late, dropped,
  repeated, dismissed, scheduling-failure, and audio-underflow counters to
  remain flat after startup. The earlier rejected `synchronized=2` experiment
  still had `low-latency-video` enabled, so it did not test this tradeoff.

## 2026-08-14 QSV encoder development checkpoint

- Development is on local branch `qsv`, created from `master` commit
  `85ec45f8b`. The production `master` branch and VA-API runtime have not been
  changed.
- The R12L identity encoder can now use `hevc_qsv` with an `AV_PIX_FMT_QSV`
  video-memory input frame. The QSV device is derived from VA-API, the QSV
  frame is mapped to its underlying VA Y410 surface, and the existing Vulkan
  shader writes `Y=G`, `U=B`, and `V=R` directly into that surface.
- If direct QSV/VA/Vulkan mapping is unavailable, the encoder falls back to
  OpenCL identity conversion into a software `XV30` frame and QSV's normal
  upload. The fallback was forced with `--param lavc-use-codec=xv30le` and
  sustained UHD 2160p24.
- The QSV identity path explicitly requests HEVC RExt, low-power encoding,
  async depth one, and Full-range RGB/identity VUI metadata. It avoids the
  generic QSV RGB-to-limited-BT.601 handling.
- QSV vertical intra-refresh is enabled with `intra_refresh`. The tested
  configuration reports `IntRefType=1`, `IntRefCycleSize=20`, and
  `IntRefCycleDist=20`; GOP remains 24 for the same one-second IDR recovery
  boundary as the VA-API configuration.
- Encoder hardware validation on `encoderTest` / `10.55.118.88` used Intel
  media runtime 26.1.2 and oneVPL implementation 2.16. Direct QSV encoding
  sustained UHD 2160p24 with a 13.77 ms average encode path, essentially the
  same as the 13.75 ms VA-API baseline and well inside the 41.67 ms frame
  budget.
- A 32-second local loopback test ran QSV encode and QSV decode concurrently:
  the receiver selected `xv30le`, used direct QSV/VA/Vulkan Y410-to-R10k
  conversion, sustained 24 fps, and received 16,144,768/16,144,768 RTP
  packets with zero loss. No encode, decode, or GPU errors occurred.
- Remaining required validation is visual/scope testing through a physical
  receiver: picture/color identity, Full range, 10-bit output, audio sync,
  SDI format transitions (HD, UHD, 2K DCI, and 4K DCI fallback behavior),
  receiver join, packet-loss recovery, and an extended soak. A new test
  receiver is being prepared; do not declare the QSV branch production-ready
  until those tests pass.

Current QSV sender video configuration:

```text
-c libavcodec:encoder=hevc_qsv:rgb:depth=10:subsampling=444:bitrate=60000000:low_power=1:async_depth=1:slices=1:gop=24:intra_refresh
```

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

## 2026-08-14 pre-reboot checkpoint

This is the current checkpoint and supersedes the older deferred-QSV notes
above where they conflict.

### Repository state

- UltraGrid branch `qsv` is at pushed commit `62b46e9b0` (`Recover QSV
  streams after input disruption`) before this HANDOFF update and the final
  merge to `master`. The QSV encoder retains vertical cyclic intra-refresh
  and forces an IDR at every configured GOP boundary so corrupted pictures
  recover. DeckLink capture timestamps are rebased after input/timeline
  discontinuities. The existing VA-API encoder remains available; QSV was
  added as another encoder path and still uses VA-API for Linux surface/device
  interoperability.
- SRT Server source is clean on `main` at pushed commit `e5c3d4c` (`Release
  0.3.41 isolate relay telemetry`), tracking `origin/main`. The older 0.3.34
  deployment details below are a historical validation checkpoint, not a
  claim about the currently installed relay package.
- ColorConnect `/home/administrator/colorconnect2` is clean for tracked files
  on `main` at pushed commit `f616049` (`Harden SRT startup and NIC
  buffering`). That commit:
  - persists the physical NIC transmit queue length at 10,000 packets through
    `fix_rings.sh`;
  - uses `${SRTUDPRECBUF}` for the local UltraGrid UDP input and appends
    `${SRTUDPSENDBUF}` to the encoder's SRT destination URI; and
  - installs a shared, stability-gated latency measurement for encoder,
    receiver, and legacy relay roles, with deterministic regression coverage.
- The ColorConnect latency helper rejects lossy/malformed ping windows,
  discards a clean warm-up window, requires three consecutive consistent
  zero-loss windows, and selects their median RTT. Live encoder validation
  selected 9 ms from a 2.599 ms median at the configured 3.5 multiplier.
- ColorConnect currently has unrelated generated untracked artifacts under
  `common/install_modules/UltraGrid/` and
  `common/install_modules/telegraf/influxdata-archive.key`; they were not
  included in the requested source commits.

### Encoder service and command state

- This host is `encoderTest`, physical address `10.55.118.88`, ZeroTier
  address `172.25.5.74`.
- `colorconnect-encoder.service` is active but **disabled**. It will not start
  automatically after reboot unless it is enabled or started manually.
- The current UltraGrid command is QSV HEVC 10-bit 4:4:4 at 60 Mb/s with
  vertical intra-refresh, a 20-frame refresh cycle, recovery-point SEI, GOP
  24, eight embedded SDI audio channels, and Opus at 192 kb/s:

  ```text
  uv -m 1344 -l auto -t decklink:codec=R12L:passthrough:nosig-send -c libavcodec:encoder=hevc_qsv:low_power=1:async_depth=1:slices=1:intra_refresh:int_ref_type=vertical:int_ref_cycle_size=20:int_ref_cycle_dist=20:recovery_point_sei=1:gop=24:rgb:depth=10:subsampling=444:bitrate=60M:gop=24 -s embedded --audio-capture-format channels=8 --audio-codec=Opus:bitrate=192k -P 40000
  ```
- After reboot, the transient 30 ms test unit disappeared and the normal
  `colorconnect-srttransmit.service` became active. It was then upgraded to
  the stability-gated latency helper and validated live at 9 ms with
  `payloadsize=1316` and `udpsndbuf=16777216`. The deployed session
  configuration sets `SRTUDPSENDBUF="&udpsndbuf=16777216"` and the deployed
  script consumes it. The repository sample still defaults this variable to
  1 MiB; that sample default was not changed in this checkpoint.

### Persistent physical NIC tuning

- Encoder NIC `enp86s0` is live with RX/TX hardware rings at their 4096
  maximums and Linux transmit queue length 10,000.
- The settings are persistent through the active systemd link drop-in:

  ```text
  /etc/systemd/network/10-netplan-enp86s0.link.d/50-colorconnect-rings.conf
  ```

  containing:

  ```ini
  [Link]
  RxBufferSize=max
  TxBufferSize=max
  TransmitQueueLength=10000
  ```

### ZeroTier socket experiment is deliberately deferred

- Persistent sysctl ceilings on both encoder and relay are:

  ```ini
  net.core.rmem_max = 33554432
  net.core.wmem_max = 33554432
  net.core.netdev_max_backlog = 4096
  ```

- During diagnosis, every existing physical UDP socket owned by
  `zerotier-one` on the encoder and relay was live-resized by requesting
  16 MiB for both `SO_RCVBUF` and `SO_SNDBUF`. Linux reports the doubled
  bookkeeping value of 32 MiB (`rb33554432`, `tb33554432`). The temporary
  pidfd-based helper is `/tmp/set-process-socket-buffer` and is not an
  installed deployment component.
- The user explicitly chose to skip making this ZeroTier socket change
  persistent. There is no ZeroTier systemd override on either machine.
  Restarting ZeroTier or rebooting recreates its sockets with ZeroTier's
  normal requested buffer size; do not silently reinstall the live patch.
- Relay ZeroTier interface `ztkse3dx6t` was also live-set to transmit queue
  length 10,000, but no persistent systemd link configuration or service
  override was found for it. Expect this live-only value to reset on relay
  reboot.
- ZeroTier socket drop counters shown by `ss -uapnme` are cumulative for the
  lifetime of the socket. A large `d` value alone does not prove current loss;
  sample it twice and check whether it increases.

### Transport conclusions to preserve

- Encoder-to-receiver direct SRT over the ZeroTier addresses was stable. The
  earlier severe relay-path loss was strongly affected by relay backpressure,
  burst handling, and an encoder bandwidth cap below the real short-term
  source rate. Production SRT bandwidth caps were removed.
- `udpsndbuf` is local to each SRT endpoint. The encoder's setting cannot be
  inherited from the relay, and a relay-side setting only changes sockets
  owned by the relay.
- Keep UltraGrid `-m 1344` paired with SRT `payloadsize=1316` for this
  ZeroTier path.
- Do not replace proper decoder recovery with an external UltraGrid watcher,
  process exit, or permanently high SRT latency. The QSV forced-IDR and
  discontinuity-handling work is the intended in-process recovery mechanism.

## 2026-08-18 upstream catch-up and paired validation

- Branch `catchup` merged CESNET `master` through upstream commit
  `69dbe5851`. The merge commit is `e5c5d5568`. Seven upstream commits were
  included: the RTP Reed-Solomon data-deleter leak fix, the Windows Tracy
  linker fix, Vulkan cast cleanups, and CineForm/JPEG XS namespace/style
  cleanups.
- The only merge conflict was in
  `src/video_display/vulkan/vulkan_transfer_image.cpp`. The resolution retains
  ColorConnect's external-host-memory fast path and fallback while applying
  upstream's `static_cast` cleanup in the mapped-memory fallback.
- The merged tree builds successfully. `make check` passes the functional
  codec/recovery tests; the three UDP multicast tests continue to report the
  known missing multicast-loopback-route failures on this build host.
- Encoder04 (`10.55.118.88`) and receiver06 (`10.55.118.103`) both ran the
  clean build identifying itself as `catchup rev e5c5d5568`.

### Live encoder/receiver soak

- Encoder04 remained on its normal production command: DeckLink R12L capture,
  QSV HEVC 10-bit 4:4:4 identity encoding at 60 Mb/s, vertical cyclic
  intra-refresh, and eight-channel embedded audio encoded as Opus.
- After the startup format detection, DeckLink capture held 24.0001 fps on
  average (23.9577-24.0356 fps across the sampled windows). QSV encode-path
  time averaged 16.59 ms (14.53-17.04 ms) against a 41.67 ms frame budget.
  No encoder errors, GPU hangs, capture loss, or continuing audio warnings
  occurred. The three `Audio frame too small!` messages were confined to the
  first second after service startup.
- Receiver06 completed a five-minute manual soak with this candidate command,
  without changing its saved production configuration:

  ```text
  /usr/local/bin/uv -V -d decklink:single-link:synchronized=2 \
    -r embedded -P 50000 \
    --param bmd-r10k-full-range,decoder-use-codec=R10k,force-lavd-decoder=hevc_qsv
  ```

- The receiver selected the direct QSV/VA DMA-BUF Vulkan Y410-to-R10k path
  and eliminated the CPU frame copy. QSV decode averaged 9.22 ms
  (8.66-9.66 ms) against the 27.50 ms budget.
- Receiver totals were 7,200 video frames: 7,170 displayed, 30 startup/join
  drops while waiting for synchronization, one startup corrupt input, and
  zero missing frames. DeckLink ended with zero late, dropped, flushed, or
  repeated frames; zero scheduling failures; and one startup-only dismissal
  plus one startup-only audio underflow. Neither startup counter increased.
- UltraGrid received 1,680,128/1,680,128 video RTP packets and
  472,576/472,576 audio RTP packets (100% for both). SRT performed some
  successful retransmission underneath UltraGrid, but its drop counters did
  not increase during the soak. All 59,982 decoded audio frames were played.
- The known QSV/VA cleanup-order warning (`Failed to destroy surface ...
  invalid VADisplay`) appeared only when the timed test shut down. It did not
  occur during playback and did not affect the soak result.

### Resulting DeckLink receiver recommendation

Use `synchronized=2`, retain the normal one-frame RTP playout delay, and omit
both `decoder-drop-policy=blocking` and `low-latency-video`. The saved
receiver06 production configuration was deliberately not edited during this
test, and `colorconnect-receiver.service` was returned to its prior inactive
state after the manual soak. Encoder04 remains active on the merged build.

### Latency-control conclusions

- A fresh search of CESNET `master` at `69dbe5851` found no upstream video
  parameter equivalent to ColorConnect's `--param low-latency-video`.
  Upstream unconditionally sets the RTP video playout buffer to one frame
  (`1/fps`) after an incoming-format change.
- The similarly named upstream controls are not substitutes:
  `low-latency-audio[=ultra]` changes only audio buffering; DeckLink
  `:low-latency` changes DeckLink submission mode; and `--audio-delay` can add
  delay to audio or video but cannot subtract the default video frame.
- Keep the custom `low-latency-video` implementation temporarily because the
  Cage/Vulkan kiosk launcher still uses it. Do not use it on DeckLink
  receivers. A/B test Cage/Vulkan with and without it; remove the custom code
  and documentation if it provides no material benefit there. A generic
  upstream `video-playout-delay=<milliseconds|frames>` option would be the
  preferred long-term replacement.
- DeckLink `:low-latency` is already UltraGrid's implicit default. It submits
  video immediately with `DisplayVideoFrameSync()` and writes continuous
  audio with `WriteAudioSamplesSync()`, without an UltraGrid scheduled-frame
  queue. Explicitly specifying it is redundant and prints a warning.
- DeckLink `:synchronized=N` disables that immediate path and uses
  timestamped audio plus `ScheduleVideoFrame()` with a minimum `N`-frame
  queue. Do not combine `:low-latency` with `:synchronized`. The separately
  configured Blackmagic `bmdDeckLinkConfigLowLatencyVideoOutput` hardware flag
  remains enabled by default even in UltraGrid's synchronized mode.
- The validated DeckLink receiver setting remains
  `decklink:single-link:synchronized=2` with normal RTP playout delay.

### Repository closeout

- The validated catch-up was merged into the fork's default branch `master`
  and pushed as merge commit `2e21c0f28`. There is no fork `main` branch in
  this checkout.
- Local `master` matched `instinctual/master` at closeout. The only worktree
  dirt was pre-existing untracked content inside `ext-deps/libmpegts`; it was
  not modified or committed.

## 2026-08-18 receiver06 scheduled-buffer startup anomaly

- After a full ColorConnect update of encoder04 and receiver06, receiver06 ran
  UltraGrid `master rev 3112f8e5` with the validated
  `decklink:single-link:synchronized=2` command. The UltraGrid executable is
  code-identical to the earlier `e5c5d5568` soak candidate; the commits after
  that candidate changed only documentation. ColorConnect commit `796ee10`
  did change the deployed Pro receiver sample from `synchronized=3` to
  `synchronized=2` and removed `decoder-drop-policy=blocking` and
  `low-latency-video`.
- On the first service start, UltraGrid was already running while the SRT
  receiver spent about 49 seconds connecting. When video joined, the
  UltraStudio 4K Mini front panel showed `1fr Buffer`. UltraGrid then logged
  continuing `Dismissed frame, buffered: 4` events, and the user saw matching
  occasional visual stutters. The decoder drop count remained fixed after
  startup, QSV decode stayed around 9-10 ms against a 27.50 ms budget, RTP
  windows were complete, and there were no GPU hangs or decode errors. SRT
  recovered some packet gaps without continuing drops, so decoder overload or
  unrecovered network loss did not explain the stutters.
- A restart with `synchronized=3` made the front panel show three buffered
  frames. It produced one startup dismissal and one startup audio underflow,
  then held 24 fps with no continuing dismissals or missing frames.
- The user then restored `synchronized=2` and restarted only UltraGrid while
  the SRT input was already established. The front panel correctly showed two
  buffered frames. During the observed interval, the only dismissal and audio
  underflow were startup events; missing frames remained zero, output held
  24 fps, QSV decode measured 8.58-9.70 ms, all RTP windows were complete, and
  all 6,200 reported audio frames were played.
- Current evidence therefore points to an abnormal cold-start/SRT-join
  scheduling state, not proof that `synchronized=2` is inherently unstable.
  Keep `synchronized=2` for now. Reproduce by rebooting receiver06 or starting
  both SRT and UltraGrid from cold, and compare the front-panel depth with
  continuing dismissed/missing/repeated counters. If the one-frame state
  recurs, harden scheduled-playback initialization rather than permanently
  increasing latency.
- DeckLink synchronized bounds use a comma:
  `synchronized=min,max`. Plain `synchronized=2` means a 2-4 frame window;
  `synchronized=2,3` narrows it to 2-3 and is not additional burst protection.
  The code already calls `IDeckLinkOutput::GetBufferedVideoFrameCount()` on
  every scheduled callback. `--verbose=debug` exposes
  `ScheduleNextFrame - N frames buffered` but is too noisy for production.
  A future diagnostic can report compact five-second current/min/max buffer
  depth and warn when it remains below the configured minimum or pinned at the
  maximum.

## 2026-08-20 receiver11 display-absent kiosk startup

- ReceiverLite `receiver11` is reachable at `172.25.5.11`. Its kiosk failed
  before Cage/UltraGrid because both i915 DRM connectors reported disconnected
  and `ug-drm-connector-config` returned `No matching connected DRM output
  found`. The GPU, i915 driver, `/dev/dri/card1`, render node, and kiosk-user
  group permissions were correct. SRT ingest remained active. The old
  `Restart=always` behavior retried the failed pre-start every two seconds and
  accumulated more than 600 restarts.
- `drm_connector_config` now accepts `--wait`. With no matching connected
  output it emits one waiting message and polls once per second until DRM
  exposes exactly one connected connector with at least one advertised mode.
  It then retains the existing behavior: set and verify `Broadcast RGB=Full`
  before allowing Cage to start. Immediate failure remains the default when
  `--wait` is omitted.
- The strict Cage kiosk unit now calls the helper with `--wait` and sets
  `TimeoutStartSec=infinity`. This deliberately leaves the kiosk in
  `activating (start-pre)` while no sink is present; the independent SRT
  service continues receiving. It does not use a fake EDID or headless output,
  so the strict 10-bit, Full-range, and EDID mode-selection guarantees remain
  intact.
- The updated helper and templated ColorConnect kiosk unit were installed on
  receiver11. No-display validation passed across a controlled service
  restart: one wait helper remained active, `NRestarts=0`, HDMI remained
  disconnected, and `colorconnect-srtreceiver.service` remained active.
  Local tests also passed a warning-free `-Wall -Wextra -Werror` build,
  immediate no-output failure, and bounded wait behavior with exactly one wait
  message.
- Still required: physically power/connect the HDMI sink and confirm the
  already-waiting unit advances automatically into Cage and UltraGrid, sets
  `Broadcast RGB=Full`, selects strict XR30/XB30 scanout, produces picture and
  unity-gain HDMI audio, and has no new kiosk restarts. Runtime unplug/replug
  handling is separate from this boot-without-display fix.
