Miscellaneous tools and utilities for UG
========================================

DRM connector configuration
---------------------------

`drm_connector_config` sets a named enum property on a connected DRM output
and verifies the result. By default it requires exactly one connected output
and sets `Broadcast RGB=Full`. Use `--device` and `--connector` to select an
output explicitly on multi-output systems.

ZeroTier UDP path probe
-----------------------

`zt_udp_probe` measures UDP loss at video-like rates without UltraGrid, SRT, or
the codec. It can send either evenly paced packets or one burst per video frame.
The receiver reports sequence loss, reordering, duplicates, kernel socket
overflow, inter-arrival gaps, and the maximum packets received in one
millisecond.

Build it with:

```
make -C tools zt_udp_probe
```

Start a receiver on the relay:

```
./zt_udp_probe recv --bind :51000 --duration 35
```

Run the video-burst test through the relay's ZeroTier address:

```
./zt_udp_probe send --target 172.25.5.104:51000 --bitrate 60M \
    --packet-size 1304 --fps 24 --pattern frame --burst-ms 8 --duration 30
```

Then repeat with a continuously paced stream:

```
./zt_udp_probe send --target 172.25.5.104:51000 --bitrate 60M \
    --packet-size 1304 --pattern paced --duration 30
```

For a conclusive A/B comparison, repeat both commands with a routed non-ZeroTier
address between the same two hosts. Keep bitrate, packet size, duration, and
pattern unchanged. `--burst-ms 0` sends each entire encoded frame immediately;
larger values spread a frame's packets across that many milliseconds.

Astat
-----

Sample application demonstrating parsing audio volume statistics from the control port.

Not useful alone.


Convert
-------

Command-line tool providing UltraGrid pixel format conversions from command-line.


stacktrace\_addr2line.sh
------------------------

Shell tool to parse UltraGrid stack trace (produced when UG crashes).


uyvy2yuv422p
------------

Simple conversion from UYVY to planar YUV 4:2:2 (i422) utilized eg. by FFmpeg.

ipc\_frame
------------

Structures and functions to hold and parse video frames when transferring
between ultragrid and some other process through the unix\_socket display for
example.
