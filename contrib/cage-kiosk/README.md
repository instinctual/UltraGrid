# UltraGrid 10-bit Cage kiosk

This configuration runs UltraGrid directly on DRM/KMS through Cage and SDL3
Wayland. It has three deliberate guarantees:

1. the compositor accepts only an `XR30` or `XB30` 10-bit scanout/render
   format and refuses to fall back silently to 8-bit;
2. the DRM connector's enum property named `Broadcast RGB` is set to its enum
   value named `Full`, without hard-coded connector IDs or enum numbers; and
3. UltraGrid matches the physical output to the incoming raster and frame
   rate using modes advertised by the connected device's EDID.

The configuration was validated on Ubuntu 26.04 Desktop with Cage 0.2.1,
wlroots 0.19.2, Intel DRM, SDL3 Wayland, and Vulkan. The EDID endpoint during
validation was a Blackmagic HDMI instrument. The setup does not match its
vendor or connector number and is therefore reusable with other displays.

## Install build dependencies

```bash
sudo apt update
sudo apt install \
  build-essential git meson ninja-build pkg-config \
  cage wlr-randr libwlroots-0.19-dev libdrm-dev libsdl3-dev
```

Ubuntu releases with a different wlroots ABI may use a differently versioned
development package. Use the version against which the installed Cage package
is built.

## Build UltraGrid and the Full RGB helper

Build UltraGrid with SDL3 and Vulkan enabled according to the normal project
instructions. Confirm that `--enable-sdl=3` appears in `bin/uv --version`.

```bash
make -j"$(nproc)"
make -C tools drm_connector_config
```

The kiosk command uses the `vulkan:...:modeset` option. At every incoming
format reconfiguration it:

- requires exactly one enabled wlroots output;
- requires an EDID mode with exactly the incoming width and height;
- selects the closest advertised refresh, preserving timings such as
  23.976 Hz instead of rounding them to 23.98 or 24;
- uses twice the UltraGrid frame rate for true interlaced input because KMS
  describes that timing by field rate;
- treats PsF as progressive; and
- skips the mode change when the selected mode is already current.

There are two explicit raster fallbacks: 2048x1080 DCI uses 1920x1080 HD, and
4096x2160 DCI uses 3840x2160 UHD, but only if the exact DCI raster is absent.
The Vulkan renderer uses a centered aspect-preserving viewport, so these
fallbacks do not stretch the DCI image. If neither the exact nor defined
fallback raster exists, UltraGrid logs the requested raster and
field/frame rate and rejects that video reconfiguration. It does not invent a
modeline.

## Build strict 10-bit Cage

Stock Cage may choose an 8-bit compositor format. The Instinctual Cage fork
keeps the strict 10-bit output and systemd shutdown changes as separate
commits. Release tag `v0.2.1-ultragrid.1` is based directly on upstream Cage
0.2.1 and is the version validated by this deployment.

```bash
workdir=$(mktemp -d)
git clone --branch v0.2.1-ultragrid.1 --depth 1 \
  https://github.com/instinctual/cage.git "$workdir/cage"
meson setup "$workdir/cage/build" "$workdir/cage" \
  --buildtype=release -Dman-pages=disabled
ninja -C "$workdir/cage/build"
install -m 0755 "$workdir/cage/build/cage" /tmp/cage-ultragrid-10bit
```

Keep this patched binary separate from the distribution's Cage package so a
package update cannot silently replace the strict 10-bit behavior.
The two patch files beside this README are retained as an offline reproducible
fallback and for easy comparison with upstream.

## Install the kiosk

Run these commands from the UltraGrid repository root after building:

```bash
sudo install -m 0755 bin/uv /usr/local/bin/uv-vulkan-sdl3
sudo install -m 0755 tools/drm_connector_config \
  /usr/local/bin/ug-drm-connector-config
sudo install -m 0755 /tmp/cage-ultragrid-10bit \
  /usr/local/bin/cage-ultragrid-10bit
sudo install -D -m 0755 contrib/cage-kiosk/ultragrid-cage-client \
  /usr/local/libexec/ultragrid-cage-client
sudo install -m 0644 contrib/cage-kiosk/ultragrid-cage-receiver.service \
  /etc/systemd/system/ultragrid-cage-receiver.service
sudo install -m 0440 contrib/cage-kiosk/ultragrid-cage-sudoers \
  /etc/sudoers.d/ultragrid-cage
sudo visudo -cf /etc/sudoers.d/ultragrid-cage
sudo loginctl enable-linger administrator
sudo systemctl daemon-reload
```

The supplied unit assumes the login name `administrator`, UID 1000, and tty1.
Change `User=`, `user@1000.service`, `XDG_RUNTIME_DIR`, the `enable-linger`
argument, and the sudoers username before installation when deploying under a
different account. Linger starts that user's systemd manager and PipeWire at
boot without requiring an interactive desktop login. `PAMName=login` creates
the active logind seat needed for unprivileged DRM access. PAM can move the
kiosk into a login-session cgroup, so the second Cage patch explicitly signals
UltraGrid during compositor shutdown; omitting that patch can make a service
restart wait until `TimeoutStopSec=`.

The receiver command is in `ultragrid-cage-client`. It intentionally uses the
default PipeWire sink rather than a numeric target ID, because PipeWire node
IDs change across boots and HDMI renegotiations. WirePlumber follows the
active default HDMI output. The wrapper sets that sink to `1.0` (unity gain)
before launching UltraGrid; the UltraGrid stream itself also runs at unity.
Adjust the network, decoder, or other UltraGrid parameters there as needed.
Keep VSync enabled: do not add `novsync` or `tearing`.

Before first use, inspect the connector properties:

```bash
modetest -c
```

On the validated Intel system the required property is exactly
`Broadcast RGB`, with enum value `Full`. The helper discovers the DRM card,
connected output, property ID, and enum number dynamically. With multiple
connected outputs, add explicit `--device` and `--connector` arguments to
`ExecStartPre=`. A non-Intel driver may expose an equivalent setting under a
different property name; do not guess—verify the property and scope the HDMI
signal before changing the unit.

To make Cage the boot kiosk:

```bash
sudo systemctl disable gdm.service
sudo systemctl enable --now ultragrid-cage-receiver.service
```

The unit conflicts with the display manager, the former Vulkan receiver
service, the test Cage service, and tty1 getty, so only one owns DRM and tty1.
It uses `Restart=always`: when an operator presses UltraGrid's `q` hotkey,
UltraGrid and Cage exit successfully, and systemd starts a fresh kiosk session
after two seconds. `Restart=on-failure` is insufficient because a `q` exit is
reported as successful.

## Verify 10-bit and Full range

```bash
systemctl --no-pager --full status ultragrid-cage-receiver.service
journalctl -b -u ultragrid-cage-receiver.service --no-pager
sudo /usr/local/bin/ug-drm-connector-config --verify-only
```

Run `wlr-randr` inside the Cage client's Wayland environment to verify the
current physical mode. The Cage journal must contain a line saying the output
uses `XR30` or `XB30`. DRM plane inspection must likewise show `XR30` or
`XB30`; a Vulkan 10-bit swapchain alone does not prove that KMS scanout is
10-bit. Finally, validate bit depth and quantization range with an HDMI scope.

After inserting or rebooting an HDMI sink, restart the entire Cage unit, not
only UltraGrid:

```bash
sudo systemctl restart ultragrid-cage-receiver.service
```

That forces a clean EDID read and KMS/HDMI negotiation. This restart was
required when an HDMI scope was inserted during validation.

## Format-change test matrix

Change only the upstream SDI source and allow the encoder and receiver to
reconfigure normally. For every format, record the incoming UltraGrid
description, the current `wlr-randr` mode, picture, audio, Full RGB result, and
scope-reported bit depth.

At minimum test:

- 3840x2160 progressive at 23.976 and 24 Hz;
- 1920x1080 progressive at 23.976, 24, 50, 59.94, and/or 60 Hz as supported;
- 1920x1080 interlaced at 25/50 and 29.97/59.94 frame/field rates;
- 1280x720 progressive at 50 and 59.94/60 Hz; and
- 2048x1080 DCI at 23.976 and 24 Hz.

For DCI, verify whether the exact mode or the documented HD/UHD fallback was
selected. An exact 2048x1080 failure followed by a 1920x1080 selection is
expected when the HDMI endpoint does not advertise 2K DCI. Do not add a custom
timing until the display and scope specifications confirm that it is accepted.

Watch the journal through every transition. Confirm that video resumes,
audio remains live and synchronized, the output mode changes to the selected
EDID timing, and the active DRM framebuffer stays `XR30` or `XB30`.

## Roll back to GNOME

```bash
sudo systemctl disable --now ultragrid-cage-receiver.service
sudo systemctl enable --now gdm.service
```

The distribution Cage package is not modified, so it remains available for
comparison. The strict binary and UltraGrid receiver binary can also be
replaced atomically with retained pre-deployment copies.
