# Usage Guide — Akita Meshtastic ZModem

This document supplements `README.md` with detailed usage examples and troubleshooting tips.

1) Examples

- See `examples/Basic_Transfer/Basic_Transfer.ino` — a minimal send/receive example.

2) Command formats (via Command Port `AKZ_ZMODEM_COMMAND_PORTNUM`, default 250)

- Start send (from controller node):
  `SEND:!<NodeID>:/path/to/file`  — NodeID format: optionally prefixed with `!`, hex digits (e.g., `!a1b2c3d4`).
- Start receive (on recipient):
  `RECV:/path/to/save`

3) Integration checklist

- Call `akitaZmodem.begin(mesh, FS, debugStream);` once at startup.
- In your `loop()` call `akitaZmodem.loop()` frequently (every 10-100ms is fine).
- When a packet arrives on port `AKZ_ZMODEM_DATA_PORTNUM`, forward it to the library with `akitaZmodem.processDataPacket(packet);` during any active transfer. Do this for both send and receive flows.

4) Debugging

- Enable a `Stream` (e.g., `Serial`) for debug output when calling `begin()`.
- Watch for messages like `Transfer Complete!` and `Transfer Error`.
- If transfers stall, inspect hop limits and packet size settings (`setMaxPacketSize`).
- If sending stalls immediately after `SEND:`, make sure your integration forwards data-port packets while the node is sending, not only while it is receiving.

5) Filesystem notes

- The library uses the Arduino `FS` API. Ensure SPIFFS/LittleFS is mounted before calling `begin()`.
- Example uses SPIFFS in `examples/Basic_Transfer`.

6) Advanced

- Tune `_zmodemTimeout` via `setTimeout()` for networks with high latency.
- Use `setProgressUpdateInterval()` to control periodic progress logs.

## Quick build & verification

Build the library and example with PlatformIO (ESP32 dev board environment):

```bash
pio run -e esp32dev
```

To build the example sketch using the Arduino IDE, open `examples/Basic_Transfer/Basic_Transfer.ino` and compile/upload as usual.

If you're integrating into Meshtastic firmware, copy the module into the firmware `src/modules/` and rebuild the full firmware with PlatformIO.

Current verification status:

- The standalone PlatformIO build in this repository has been verified on `env:esp32dev`.
- The protocol parser and data-port routing were reviewed and corrected for active send and receive paths.
- A real transfer between Meshtastic nodes has not been exercised in this workspace, so hardware validation is still recommended.

Host-side helper prerequisites:

- `tools/serial_proxy.py` requires `python3` and `pyserial`.
- `tools/auto_xmodem_test.py` requires `python3` and `sz` from `lrzsz`.
- These helpers are optional and do not replace on-device Meshtastic transfer testing.

## Notes

- The `AKZ_ZMODEM_COMMAND_PORTNUM` (default `250`) is used for text commands (`SEND:`/`RECV:`).
- The `AKZ_ZMODEM_DATA_PORTNUM` (default `251`) is used for the binary ZModem transfer packets.
- When troubleshooting, enable a `Stream` (e.g., `Serial`) in `begin()` for debug logs.
