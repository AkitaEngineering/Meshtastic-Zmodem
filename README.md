# Akita Meshtastic ZModem

Version: 1.1.1

Adds reliable, targeted node-to-node file transfers to Meshtastic LoRa networks.

Contents:

- `AkitaMeshZmodem` — Arduino library embedding a non-blocking ZModem engine.
- `ZmodemModule` — Meshtastic firmware module for in-firmware operation.

License: GNU GPLv3 — see `LICENSE`.

## Features

* **Zero External Dependencies:** The **ZModem protocol stack is entirely built-in** (`src/utility/ZModemEngine`), eliminating reliance on external, unstable, or missing ZModem libraries.
* **Non-Blocking Operation:** The implementation is designed to run seamlessly within the main Meshtastic firmware loop, ensuring the device remains responsive, routing packets, and managing the mesh network during transfers.
* **Targeted Sending:** Files are sent directly to a specified Node ID, **not broadcast** across the entire mesh, which is efficient and network-friendly.
* **Reliable Protocol:** Utilizes simplified ZModem state handling and CRC checks optimized for robust, 8-bit clean LoRa links.
* **Dedicated Port Handling:** Uses separate, configurable Meshtastic PortNums for command initiation and data transmission to prevent conflicts.
* **Filesystem Agnostic:** Uses the standard Arduino `FS` API for compatibility with SPIFFS, LittleFS, or SD cards.

## Requirements

* **Hardware:** Meshtastic-compatible devices (**ESP32-based recommended**) with sufficient flash memory for the firmware and a filesystem (SPIFFS/LittleFS) to store files.
* **Required Arduino Libraries (Minimal):**
    * `Meshtastic` (Official Meshtastic device library/firmware source)
    * `FS` (Part of the ESP32 core)

## Installation

### Option 1: Using the Library in a Custom Sketch

1.  **Install Dependencies:** Ensure `Meshtastic` is installed via the Arduino Library Manager or PlatformIO.
2.  **Install AkitaMeshZmodem Library:**
    * **PlatformIO (Recommended):** Add the library directly to your `platformio.ini` dependencies:
        ```ini
        lib_deps =
            meshtastic/Meshtastic
            https://github.com/AkitaEngineering/Meshtastic-Zmodem.git
        ```
    * **Arduino IDE:** Download this repository as a ZIP and install it via the Arduino IDE's `Sketch` -> `Include Library` -> `Add .ZIP Library...` option.

### Option 2: Integrating the Module into Meshtastic Firmware

This is the preferred method for running file transfers as a service and requires building the Meshtastic firmware from source using PlatformIO.

1.  **Create the library folder:** Inside your Meshtastic firmware checkout, create a library folder that preserves the same relative include layout used by this repository:
    ```text
    lib/Akita_Meshtastic_Zmodem/src/
    lib/Akita_Meshtastic_Zmodem/src/utility/
    ```
2.  **Copy only the runtime library files:** Copy these files into that folder:
    ```text
    src/AkitaMeshZmodem.h
    src/AkitaMeshZmodem.cpp
    src/AkitaMeshZmodemConfig.h
    src/utility/ZModemEngine.h
    src/utility/ZModemEngine.cpp
    ```
    Keep the `utility/` subdirectory intact so includes such as `utility/ZModemEngine.h` continue to resolve.
3.  **Do not copy the standalone stub files:** These files exist in this repository only to let the library compile outside the full Meshtastic firmware tree and should not overwrite firmware-provided headers:
    ```text
    src/main.cpp
    src/globals.h
    src/module.h
    src/mesh-core.h
    src/serial-interface.h
    ```
4.  **Install the Meshtastic module:** Copy `src/modules/ZmodemModule.h` and `src/modules/ZmodemModule.cpp` into the Meshtastic firmware's `src/modules/` directory.
5.  **Register the module:** Edit the main firmware file (`src/mesh-core.cpp` or similar) to include the module header and instantiate the module, allowing it to hook into the main loop:
    ```cpp
    #include "modules/ZmodemModule.h"
    // ...
    modules.push_back(new ZmodemModule(*this));
    ```
6.  **Confirm firmware-side prerequisites:**
    - The real Meshtastic firmware must provide `Filesystem`, `Log`, `Module`, `MeshInterface`, and the packet delivery path used by `handleReceived()`.
    - The filesystem must be mounted before `ZmodemModule::setup()` runs.
    - `AKZ_ZMODEM_COMMAND_PORTNUM` and `AKZ_ZMODEM_DATA_PORTNUM` must not collide with any other application ports in your firmware build.
7.  **If you integrate the library without `ZmodemModule`:** Forward all packets received on `AKZ_ZMODEM_DATA_PORTNUM` to `akitaZmodem.processDataPacket(packet)` during any active transfer. The sender also depends on responses received on the data port.

## Usage

Control is handled by sending specific text commands to the device on the **Command Port** (`AKZ_ZMODEM_COMMAND_PORTNUM`, default 250).

### Command Structure

| Action | Format | Example (using CLI) |
| :--- | :--- | :--- |
| **Start Send** | `SEND:!NodeID:/local/file.bin` | `meshtastic --sendtext "SEND:!a1b2c3d4:/test.txt" --portnum 250` |
| **Start Receive**| `RECV:/save/path.bin` | `meshtastic --sendtext "RECV:/received.bin" --portnum 250` |
| **Abort** | `ABORT` | `meshtastic --sendtext "ABORT" --portnum 250` |
| **Status** | `STATUS` | `meshtastic --sendtext "STATUS" --portnum 250` |

### API Reference (Library Integration)

When integrating into custom code:

* `begin(mesh, Filesystem, &Serial)`: Initialize the engine and set up the transport streams.
* `loop()`: Must be called continuously in your main loop to process the ZModem state machine.
* `processDataPacket(MeshPacket& packet)`: **CRITICAL.** Forward every packet received on the **Data Port** (`AKZ_ZMODEM_DATA_PORTNUM`) while a transfer is active. The sender needs control responses on this port just as much as the receiver needs file data.
* `reset()`: Return to IDLE after COMPLETE/ERROR so another transfer can start. `startSend()` / `startReceive()` also do this automatically when no transfer is busy.
* `abortTransfer()` / command `ABORT`: Cancel an active transfer and return to IDLE.


## Additional tools and testing

Two helper scripts have been added under `tools/` to simplify exercising the
module on a host machine:

* `serial_proxy.py` – creates a PTY and proxies it to a real serial device.
    Run your host-side serial tool against the PTY to talk to the board.
    Requires `python3` and `pyserial`.
* `auto_xmodem_test.py` – automation harness that invokes `sz --xmodem` and
    validates the received file. Requires `python3` and `sz` from `lrzsz`.

These tools are for local protocol experimentation only. The default library
and module flow documented in this repository is the Meshtastic ZModem path
driven by `SEND:` and `RECV:` commands over the command and data ports.

### Building without Meshtastic

The PlatformIO project has been configured to allow the code to compile with
only a stubbed Meshtastic API (see `lib/…`); this makes it possible to verify
the ZModem engine builds even when the real firmware is not available. Native
protocol and transport tests live in `tests/native/` and can be run with
`tests/native/run.sh`. To perform a full on-device build the real Meshtastic
package from the Arduino registry is required.

## Documentation updates (recent)

- Fixed several protocol/parser bugs in the internal ZModem engine (`src/utility/ZModemEngine.cpp`) including proper ZDLE-escaping and header scanning for robustness over noisy links.
- Resolved stream-handling issues in the library wrapper (`src/AkitaMeshZmodem.cpp`), including buffering multiple data-port packets while the engine drains the stream.
- Corrected the module and example integration so packets on the data port are forwarded during both send and receive operations.
- The example sketch `examples/Basic_Transfer/Basic_Transfer.ino` was corrected for proper packet handling, command parsing, and transfer-state cleanup.
- Stub headers and library stubs were improved so the library can be compiled and verified with PlatformIO even when the full Meshtastic firmware is not present.

These fixes were verified by building the project with PlatformIO on ESP32 (`env: esp32dev`). End-to-end radio validation on real Meshtastic nodes is still recommended before treating the transfer path as field-proven. See the quick build steps in `USAGE.md`.

