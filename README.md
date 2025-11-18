# Akita Meshtastic ZModem Library & Module

**Version: 1.1.0**

This project provides ZModem file transfer capabilities for [Meshtastic](https://meshtastic.org/) LoRa mesh networks. It includes:

1.  An **Arduino Library (`AkitaMeshZmodem`)**: Allows direct integration into custom Meshtastic Arduino sketches.
2.  A **Meshtastic Module (`ZmodemModule`)**: Can be compiled into the main Meshtastic firmware to provide file transfer functionality controlled via mesh commands.

This enables **reliable, targeted (node-to-node) binary file transfers** using a custom, **non-blocking** ZModem protocol engine built directly into the library, guaranteeing stability and zero external dependency issues.

Developed and maintained by [Akita Engineering](https://akitaengineering.com).

**License:** This project is licensed under the **GNU General Public License v3.0 (GPLv3)**.

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
    * `StreamUtils` (A common utility library, included in PlatformIO/Arduino)
    * `FS` (Part of the ESP32 core)

## Installation

### Option 1: Using the Library in a Custom Sketch

1.  **Install Dependencies:** Ensure `Meshtastic` and `StreamUtils` are installed via the Arduino Library Manager or PlatformIO.
2.  **Install AkitaMeshZmodem Library:**
    * **PlatformIO (Recommended):** Add the library directly to your `platformio.ini` dependencies:
        ```ini
        lib_deps =
            meshtastic/Meshtastic
            bblanchon/ArduinoStreamUtils
            https://github.com/AkitaEngineering/Meshtastic-Zmodem.git
        ```
    * **Arduino IDE:** Download this repository as a ZIP and install it via the Arduino IDE's `Sketch` -> `Include Library` -> `Add .ZIP Library...` option.

### Option 2: Integrating the Module into Meshtastic Firmware

This is the preferred method for running file transfers as a service and requires building the Meshtastic firmware from source using PlatformIO.

1.  **Prepare Files:** Copy the files from the `src/` directory into a library folder (`lib/Akita_Meshtastic_Zmodem`) within the Meshtastic firmware source tree.
2.  **Install Module:** Copy `src/modules/ZmodemModule.h` and `src/modules/ZmodemModule.cpp` into the Meshtastic firmware's `src/modules/` directory.
3.  **Register Module:** Edit the main firmware file (`src/mesh-core.cpp` or similar) to include the module header and instantiate the module, allowing it to hook into the main loop:
    ```cpp
    #include "modules/ZmodemModule.h"
    // ...
    modules.push_back(new ZmodemModule(*this));
    ```

## Usage

Control is handled by sending specific text commands to the device on the **Command Port** (`AKZ_ZMODEM_COMMAND_PORTNUM`, default 250).

### Command Structure

| Action | Format | Example (using CLI) |
| :--- | :--- | :--- |
| **Start Send** | `SEND:!NodeID:/local/file.bin` | `meshtastic --sendtext "SEND:!a1b2c3d4:/test.txt" --portnum 250` |
| **Start Receive**| `RECV:/save/path.bin` | `meshtastic --sendtext "RECV:/received.bin" --portnum 250` |

### API Reference (Library Integration)

When integrating into custom code:

* `begin(mesh, Filesystem, &Serial)`: Initialize the engine and set up the transport streams.
* `loop()`: Must be called continuously in your main loop to process the ZModem state machine.
* `processDataPacket(MeshPacket& packet)`: **CRITICAL.** This method is used to push raw data packets received on the **Data Port** (`AKZ_ZMODEM_DATA_PORTNUM`) directly into the ZModem engine's input buffer.
