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
