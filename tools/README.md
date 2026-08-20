Miscellaneous tools and utilities for UG
========================================

DRM connector configuration
---------------------------

`drm_connector_config` sets a named enum property on a connected DRM output
and verifies the result. By default it requires exactly one connected output
and sets `Broadcast RGB=Full`. Use `--device` and `--connector` to select an
output explicitly on multi-output systems. `--wait` waits indefinitely for a
connected output with an advertised mode instead of failing. This is intended
for kiosk service startup before an HDMI/DisplayPort sink is powered or
connected.

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
