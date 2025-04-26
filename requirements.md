# Requirements for Akita Meshtastic ZModem Library & Module

This document outlines the necessary hardware, software, and library dependencies required to use and/or compile the Akita Meshtastic ZModem project.

## Hardware Requirements

* **Meshtastic Device(s):** One or more Meshtastic-compatible devices are required.
    * **Recommendation:** ESP32-based devices (e.g., TTGO T-Beam, Heltec LoRa 32, T-Echo) are strongly recommended due to the memory and filesystem (SPIFFS/LittleFS) requirements for storing files and running the ZModem protocol. Performance on nRF52-based devices might be limited or untested.

## Software Requirements

* **Development Environment:** You need an environment capable of compiling Arduino code for ESP32.
    * **Option 1: Arduino IDE:**
        * Latest version of the Arduino IDE.
        * ESP32 Board Support Package installed via the Board Manager.
    * **Option 2: PlatformIO IDE:**
        * Visual Studio Code with the PlatformIO IDE extension installed.
        * This is the **recommended** environment, especially for integrating the module into the main Meshtastic firmware, due to better dependency management.

* **Meshtastic Firmware (for Module Integration):**
    * If compiling the `ZmodemModule` into the main firmware, you need the source code of the [Meshtastic device firmware](https://github.com/meshtastic/firmware).

## Library Dependencies

The following Arduino libraries are required and must be installed in your development environment:

1.  **Meshtastic:**
    * The official Meshtastic device library.
    * **Source:** Typically installed via PlatformIO (`meshtastic/Meshtastic`) or Arduino Library Manager. If compiling the firmware, it's included in the source tree.

2.  **ZModem:**
    * A compatible Arduino implementation of the ZModem protocol.
    * **Recommended:** [`ropg/ZModem`](https://github.com/ropg/ZModem)
    * **Installation:** Via PlatformIO (`ropg/ZModem`) or Arduino Library Manager.

3.  **StreamUtils:**
    * A utility library often required by ZModem implementations.
    * **Recommended:** [`bblanchon/ArduinoStreamUtils`](https://github.com/bblanchon/ArduinoStreamUtils)
    * **Installation:** Via PlatformIO (`bblanchon/ArduinoStreamUtils`) or Arduino Library Manager.

4.  **FS (Filesystem):**
    * The standard Arduino filesystem library, included with the ESP32 core installation. No separate installation is usually needed, but your code must include `<FS.h>` and potentially specific filesystem headers like `<SPIFFS.h>` or `<LittleFS.h>`.

Ensure these libraries are correctly installed and accessible to the compiler when building either a custom sketch using the `AkitaMeshZmodem` library or the full Meshtastic firmware including the `ZmodemModule`.
