# UltraGrid R12L Streaming Handoff

Last updated: 2026-08-01 (America/Los_Angeles)

## Repository state

- Current checkout: `/home/administrator/UltraGrid`
- Current branch: `decklinkscheduling`
- Latest implementation commit: `e907a7ecb` — Recover DeckLink format
  detection after signal loss
- Organization remote: `instinctual` (`https://github.com/instinctual/UltraGrid.git`)
- Upstream remote: `origin` (`https://github.com/CESNET/UltraGrid.git`)
- `decklinkscheduling` is pushed to `instinctual/decklinkscheduling`.
- The branch is merged into `instinctual/master`.
- Current local binary SHA-256:
  `7f9097fbfb9b1ebd6220823f0d8503a377bc6881c22aac225c83e4da0c34604e`

Recent branch commits:

1. `e907a7ecb` — Recover DeckLink format detection after signal loss
2. `5d3032b2e` — Reduce Vulkan handoff overhead
3. `60a4e955b` — Optimize DeckLink decode scheduling
4. `a188eaace` — Optimize DeckLink buffer handoff

## Machines

| Role | Host | SSH user | Runtime unit |
|---|---|---|---|
| Encoder | `10.55.118.88` | `administrator` | `ultragrid-sender.service` |
| Receiver | `10.55.118.89` | `administrator` | `ultragrid-receiver.service` |

Credentials are intentionally not stored in this repository. Obtain them
through the operator or the normal secret-management mechanism.

Both runtime services are transient systemd units created with `systemd-run`.
They use `Restart=on-failure` and `RestartSec=1s`.

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
  continuous signal loss, with bounded retry backoff. Recovery runs from the
  capture thread rather than the DeckLink callback.

No optimization above adds a frame queue or increases scheduled depth.

## Last measured steady state

After a clean restart of both machines on commit `5d3032b2e`:

- Encoder capture: approximately 24 fps.
- Encoder encode path: approximately 16.0 ms total.
  - R12L conversion: approximately 7.0 ms.
  - VA encoder submit/output: approximately 9.0 ms.
- Receiver output: approximately 24 fps.
- Receiver decode path: approximately 10.3–10.4 ms.
  - QSV packet submit: approximately 6.2 ms.
  - Surface transfer/conversion: approximately 4.1 ms.
- Steady receiver counters: 0 late, 0 dropped, 0 schedule failures.

Counter increases during a deliberate sender restart are expected. Confirm
that counters become flat after the stream recovers.

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

## Moving the checkout

To move the complete repository while preserving `.git` and all local state:

```bash
mv /home/administrator/ccgscx/UltraGrid /home/administrator/UltraGrid
cd /home/administrator/UltraGrid
```

Then resume the existing Codex conversation from the new directory. After the
move:

```bash
make clean
./autogen.sh
./configure
make -j"$(nproc)"
make check
```

Only run the full regeneration sequence if required by the moved build tree;
otherwise a normal `make` may be sufficient. Search for stale absolute paths
if a build tool reports the old checkout location.
