# Akita Meshtastic ZModem Library & Module

**Version: 1.0.0**

This project provides ZModem file transfer capabilities for [Meshtastic](https://meshtastic.org/) LoRa mesh networks. It includes:

1.  An **Arduino Library (`AkitaMeshZmodem`)**: Allows direct integration into custom Meshtastic Arduino sketches.
2.  A **Meshtastic Module (`ZmodemModule`)**: Can be compiled into the main Meshtastic firmware to provide file transfer functionality controlled via mesh commands.

This enables reliable sending and receiving of binary files directly between Meshtastic nodes, typically using the device's onboard filesystem (like SPIFFS on ESP32).

Developed and maintained by [Akita Engineering](https://akitaengineering.com).

**License:** This project is licensed under the **GNU General Public License v3.0 (GPLv3)**. See the [LICENSE](LICENSE) file for details.

## Features

* **Binary File Transfer:** Send and receive any type of file (text, images, firmware updates, etc.) over the Meshtastic mesh.
* **Reliable ZModem Protocol:** Utilizes the robust ZModem protocol, which includes error checking (CRC) and recovery mechanisms suitable for potentially lossy LoRa links.
* **Meshtastic Integration:** Wraps Meshtastic packet communication within a standard Arduino `Stream` interface for seamless integration with the ZModem library.
* **Packet Handling:** Implements packetization, sequence tracking (Packet ID), and reassembly to handle Meshtastic's packet size limitations.
* **Filesystem Agnostic:** Uses the standard Arduino `FS` API, allowing integration with SPIFFS, SD cards, or other compatible filesystems.
* **Simple Control (Module):** The included Meshtastic module allows transfers to be initiated via simple text commands sent over Meshtastic.
* **Flexible Control (Library):** The library can be integrated into custom sketches with user-defined control mechanisms.
* **Robust Error Handling:** Includes timeouts and configurable retries for ZModem operations.
* **Progress Tracking:** Provides feedback on transfer progress (bytes transferred, percentage if file size is known) via an optional debug stream (e.g., `Serial`).
* **Configurable:** Allows customization of timeouts, packet sizes, retry counts, and progress update intervals.

## Requirements

* **Hardware:** Meshtastic-compatible devices (ESP32-based recommended due to filesystem and memory requirements).
* **Development Environment:**
    * Arduino IDE with ESP32 board support installed.
    * OR PlatformIO IDE (recommended for easier dependency management, especially for module integration).
* **Required Arduino Libraries:**
    * `Meshtastic` (The official Meshtastic device library/firmware source)
    * `ZModem` (A compatible ZModem Arduino library, e.g., [ropg/ZModem](https://github.com/ropg/ZModem))
    * `StreamUtils` (Required by some ZModem libraries, e.g., [bblanchon/ArduinoStreamUtils](https://github.com/bblanchon/ArduinoStreamUtils))
    * `FS` (Part of the ESP32 core)

## Installation

You can use this project in two ways: as a standalone library in your own sketches, or by compiling the module into the main Meshtastic firmware.

### Option 1: Using the Library in a Custom Sketch

1.  **Install Dependencies:**
    * **Arduino IDE:** Open the Library Manager (`Sketch` -> `Include Library` -> `Manage Libraries...`). Search for and install `Meshtastic`, `ZModem` (e.g., by ropg), and `StreamUtils`.
    * **PlatformIO:** Add the following to your `platformio.ini` file under `lib_deps`:
        ```ini
        lib_deps =
            meshtastic/Meshtastic     ; Or the correct identifier
            ropg/ZModem               ; Or the correct identifier
            bblanchon/ArduinoStreamUtils
        ```

2.  **Install AkitaMeshZmodem Library:**
    * **Option A: Install from ZIP:** Download this repository as a ZIP file (`Code` -> `Download ZIP`). In the Arduino IDE, go to `Sketch` -> `Include Library` -> `Add .ZIP Library...` and select the downloaded file.
    * **Option B: Manual Installation (Arduino IDE):** Clone or download this repository. Place the entire `AkitaEngineering-Meshtastic-Zmodem-main` (or similar) folder into your Arduino `libraries` directory (usually found in `Documents/Arduino/libraries`). Rename the folder to `Akita_Meshtastic_Zmodem`.
    * **Option C: PlatformIO:** Add the library via its Git URL or path in your `platformio.ini`:
        ```ini
        lib_deps =
            ; ... other dependencies
            [https://github.com/AkitaEngineering/Meshtastic-Zmodem.git](https://github.com/AkitaEngineering/Meshtastic-Zmodem.git) ; Or path to local clone
        ```

3.  **Restart IDE:** Restart your Arduino IDE or PlatformIO environment.
4.  **Include and Use:** `#include "AkitaMeshZmodem.h"` in your sketch and use the `AkitaMeshZmodem` class as shown in the `examples/` directory.

### Option 2: Integrating the Module into Meshtastic Firmware

This requires building the Meshtastic firmware from source using PlatformIO.

1.  **Clone Meshtastic Firmware:** Clone the official [Meshtastic device firmware repository](https://github.com/meshtastic/firmware).
2.  **Clone/Copy This Repository:** Clone or download this `AkitaEngineering/Meshtastic-Zmodem` repository.
3.  **Install Library:**
    * Copy the `src/` directory from *this* repository and rename it to `Akita_Meshtastic_Zmodem`. Place this `Akita_Meshtastic_Zmodem` folder inside the `lib/` directory of the *Meshtastic firmware* source tree.
    * Alternatively, add this repository as a dependency in the main Meshtastic `platformio.ini` file (under `lib_deps`).
4.  **Install Module:**
    * Copy the `src/modules/ZmodemModule.h` and `src/modules/ZmodemModule.cpp` files from *this* repository into the `src/modules/` directory within the *Meshtastic firmware* source tree.
5.  **Register Module:**
    * Edit `src/mesh-core.cpp` (or a similar central file like `MeshService.cpp` depending on firmware version) in the Meshtastic firmware source.
    * Add the include near the top: `#include "modules/ZmodemModule.h"`
    * Find where other modules are instantiated (e.g., in `MeshService::setupModules()` or similar).
    * Add the line to create an instance of the Zmodem module:
        ```cpp
        modules.push_back(new ZmodemModule(*this)); // 'this' usually refers to the MeshInterface instance
        ```
6.  **Verify PortNum:** Check that `PortNum_ZMODEM_APP` (defined in `ZmodemModule.h`, default is 250) does not conflict with other PortNums used in the Meshtastic firmware (see `mesh/mesh.options.proto` or `PortNum.h`). Change it if necessary.
7.  **Build and Flash:** Build the modified Meshtastic firmware using PlatformIO and flash it to your device(s).

## Usage

### Library Usage (Custom Sketch)

Refer to the `examples/Basic_Transfer/Basic_Transfer.ino` sketch. The basic steps are:

1.  Include the header: `#include "AkitaMeshZmodem.h"`
2.  Create an instance: `AkitaMeshZmodem akitaZmodem;`
3.  Initialize in `setup()`: `akitaZmodem.begin(mesh, Filesystem, &Serial);` (passing mesh instance, filesystem, and optional debug stream).
4.  Call `loop()` repeatedly: `akitaZmodem.loop();`
5.  Trigger transfers based on your logic (e.g., serial commands, button presses, incoming packets):
    * `akitaZmodem.startSend("/path/to/local/file.dat");`
    * `akitaZmodem.startReceive("/path/to/save/received_file.dat");`
6.  Check the state: `akitaZmodem.getCurrentState()`

### Module Usage (Integrated into Firmware)

Once the module is compiled into the firmware:

1.  **Initiate Send:** On the sending device, send a Meshtastic text message to the network (or directly to the receiving node) using the defined command PortNum (`PortNum_ZMODEM_APP`, e.g., 250):
    * **Command:** `SEND:/path/to/file_on_sender.bin`
    * Example using Meshtastic Python CLI: `meshtastic --dest !<receiver_node_id> --sendtext "SEND:/my_firmware.bin" --portnum 250`
    * Example using Android App: Send a direct message to the receiver node with the text `SEND:/my_firmware.bin` and set the PortNum to 250 (if the app allows setting PortNum).
2.  **Initiate Receive:** On the receiving device, send a Meshtastic text message using the same PortNum:
    * **Command:** `RECV:/path/to/save_location.bin`
    * Example: `meshtastic --dest !<sender_node_id> --sendtext "RECV:/received_fw.bin" --portnum 250`
3.  **Monitor:** Observe the device logs (Serial output if connected) for status messages, progress, and completion/error reports generated by the module and library. The module also sends back simple text replies ("OK: Starting...", "Error: ...") to the command sender on the same PortNum.

## Configuration

Default configuration values are defined in `src/AkitaMeshZmodemConfig.h` within the library source. When using the library directly or the module, you can override these by:

1.  **Using `#define` before including the library header** (if using the library directly in a sketch).
2.  **Using the setter methods** (`setTimeout`, `setMaxPacketSize`, etc.) on the `AkitaMeshZmodem` instance after calling `begin()`. This works for both library and module usage (modify `ZmodemModule.cpp` to call setters in its `setup()` if needed).

**Key Configuration Options:**

* `AKZ_DEFAULT_ZMODEM_TIMEOUT`: Timeout for ZModem operations (ms). Default: 30000.
* `AKZ_DEFAULT_MAX_PACKET_SIZE`: Max Meshtastic payload size used. Default: 230. (Ensure this doesn't exceed radio limits minus 3 bytes for header).
* `AKZ_DEFAULT_PROGRESS_UPDATE_INTERVAL`: How often progress is logged (ms). Default: 5000. (0 disables).
* `AKZ_PACKET_IDENTIFIER`: Byte used to mark ZModem data packets. Default: 0xFF.

## How it Works

The core `AkitaMeshZmodem` library wraps a standard ZModem implementation. It uses a custom `MeshtasticZModemStream` class that acts as the communication channel. This stream class intercepts ZModem protocol data, packetizes it with a sequence header, sends/receives it via the standard Meshtastic API, and handles reassembly and duplicate detection before passing valid data back to the ZModem engine. The `ZmodemModule` acts as a high-level interface within the Meshtastic firmware, handling command parsing and invoking the library functions.

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues on the [GitHub repository](https://github.com/AkitaEngineering/Meshtastic-Zmodem) to suggest improvements, report bugs, or add features.

## License

This project is licensed under the **GNU General Public License v3.0 (GPLv3)**.

Permissions of this strong copyleft license are conditioned on making available complete source code of licensed works and modifications, which include larger works using a licensed work, under the same license. Copyright and license notices must be preserved. Contributors provide an express grant of patent rights. See the [LICENSE](LICENSE) file for the full license text.
