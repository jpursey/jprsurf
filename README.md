# JPRSurf

The Jovian Path Reaper control surface Plugin

This currently supports Windows only and the X-Touch Universal and X-Touch
Extender control surfaces via the MCU and Behringer XCTL protocols.

## Building the code

The JPRSurf project is built using [GameBits](https://github.com/jpursey/game-bits) and the [Reaper SDK](https://github.com/justinfrankel/reaper-sdk). In order to build any JPRSurf code, you must have the following set up:
- The `GB_DIR` environment variable must be set to the root directory of GameBits.
- The `REAPER_EXTENSION_SDK` environment variable must be set to the root directory of the Reaper SDK.

**Platform support:** At the moment, JPRSurf only supports Windows. However the Reaper SDK is cross-platform and neither GameBits nor JPRSurf is written to be platform specific, so I don't think a port would be terribly difficult (I just have not made the effort to port it).

## Development log

We'll see how long this lasts, but I'll periodically update progress and changes here for now.
- *2026-02-22:* First version working end-to-end with only track selection supported. [Demo Video](https://youtu.be/7Z1pmt_pxGU)
- *2026-03-16:* Generalized scene / view / mapping concepts with track context and added initial support for each track mapping to a channel on the X-Touch. [Demo Video](https://youtu.be/nqRgdNaj1Y8)
- *2026-03-24:* Support for full track navigation, modifier keys, and track colors. [Demo Video](https://youtu.be/7CKpyNhSO9o)
- *2026-03-30:* Support for transport controls, metering lights, and timecode display. [DemoVideo](https://youtu.be/lK22xKMxtWY)