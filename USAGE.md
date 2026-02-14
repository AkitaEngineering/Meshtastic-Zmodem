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
- When a received packet arrives on port `AKZ_ZMODEM_DATA_PORTNUM`, forward it to the library with `akitaZmodem.processDataPacket(packet);`.

4) Debugging

- Enable a `Stream` (e.g., `Serial`) for debug output when calling `begin()`.
- Watch for messages like `Transfer Complete!` and `Transfer Error`.
- If transfers stall, inspect hop limits and packet size settings (`setMaxPacketSize`).

5) Filesystem notes

- The library uses the Arduino `FS` API. Ensure SPIFFS/LittleFS is mounted before calling `begin()`.
- Example uses SPIFFS in `examples/Basic_Transfer`.

6) Advanced

- Tune `_zmodemTimeout` via `setTimeout()` for networks with high latency.
- Use `setProgressUpdateInterval()` to control periodic progress logs.
