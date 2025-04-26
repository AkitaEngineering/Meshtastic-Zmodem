# Akita Meshtastic ZModem Library

**Version: 1.0.0**

This Arduino library provides a ZModem protocol implementation tailored for transferring binary files over [Meshtastic](https://meshtastic.org/) LoRa mesh networks. It enables reliable sending and receiving of files directly between Meshtastic nodes, typically using the device's onboard filesystem (like SPIFFS on ESP32).

Developed and maintained by [Akita Engineering](https://akitaengineering.com).

## Features

* **Binary File Transfer:** Send and receive any type of file (text, images, firmware updates, etc.) over the Meshtastic mesh.
* **Reliable ZModem Protocol:** Utilizes the robust ZModem protocol, which includes error checking (CRC) and recovery mechanisms suitable for potentially lossy LoRa links.
* **Meshtastic Integration:** Wraps Meshtastic packet communication within a standard Arduino `Stream` interface for seamless integration with the ZModem library.
* **Packet Handling:** Implements packetization, sequence tracking (Packet ID), and reassembly to handle Meshtastic's packet size limitations.
* **Filesystem Agnostic:** Uses the standard Arduino `FS` API, allowing integration with SPIFFS, SD cards, or other compatible filesystems.
* **Simple Control:** Transfers can be initiated via simple text commands sent over Meshtastic (customizable in user code).
* **Robust Error Handling:** Includes timeouts and configurable retries for ZModem operations.
* **Progress Tracking:** Provides feedback on transfer progress (bytes transferred, percentage if file size is known) via an optional debug stream (e.g., `Serial`).
* **Configurable:** Allows customization of timeouts, packet sizes, retry counts, and progress update intervals.

## Requirements

* **Hardware:** Meshtastic-compatible devices (ESP32-based recommended due to filesystem and memory requirements).
* **Arduino IDE or PlatformIO:**
    * Arduino IDE with ESP32 board support installed.
    * PlatformIO IDE (recommended for easier dependency management).
* **Required Arduino Libraries:**
    * `Meshtastic` (The official Meshtastic device library)
    * `ZModem` (A compatible ZModem Arduino library, e.g., [ropg/ZModem](https://github.com/ropg/ZModem))
    * `StreamUtils` (Required by some ZModem libraries, e.g., [bblanchon/ArduinoStreamUtils](https://github.com/bblanchon/ArduinoStreamUtils))
    * `FS` (Part of the ESP32 core)

## Installation

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
    * **Option A: Arduino Library Manager (Future - if published):** Search for `Akita Meshtastic Zmodem` and install.
    * **Option B: Install from ZIP:** Download this repository as a ZIP file (`Code` -> `Download ZIP`). In the Arduino IDE, go to `Sketch` -> `Include Library` -> `Add .ZIP Library...` and select the downloaded file.
    * **Option C: Manual Installation (Arduino IDE):** Clone or download this repository. Place the entire `AkitaEngineering-Meshtastic-Zmodem-main` (or similar) folder into your Arduino `libraries` directory (usually found in `Documents/Arduino/libraries`). Rename the folder to `Akita_Meshtastic_Zmodem`.
    * **Option D: PlatformIO:** Add the library via its Git URL or path in your `platformio.ini`:
        ```ini
        lib_deps =
            ; ... other dependencies
            [https://github.com/AkitaEngineering/Meshtastic-Zmodem.git](https://github.com/AkitaEngineering/Meshtastic-Zmodem.git) ; Or path to local clone
        ```

3.  **Restart IDE:** Restart your Arduino IDE or PlatformIO environment.

## Basic Usage

```cpp
#include <Meshtastic.h>      // Meshtastic library
#include <SPIFFS.h>          // Filesystem (e.g., SPIFFS)
#include "AkitaMeshZmodem.h" // Include this library

// Assume 'mesh' is your initialized Meshtastic instance
// Meshtastic mesh;

// Create an instance of the ZModem handler
AkitaMeshZmodem akitaZmodem;

void setup() {
  Serial.begin(115200);
  SPIFFS.begin(true); // Initialize filesystem

  // Initialize Meshtastic (as per your setup)
  // mesh.init();

  // Initialize the Akita ZModem library
  // Pass the mesh instance, filesystem, and optional debug stream
  akitaZmodem.begin(mesh, SPIFFS, &Serial);

  // Add your code to handle incoming commands (e.g., from text messages)
  // mesh.addOnReceiveCallback(yourPacketHandler); // Example callback registration
}

void loop() {
  // Handle incoming Meshtastic packets (if not using callbacks)
  if (mesh.available()) {
    ReceivedPacket packet = mesh.receive();
    yourPacketHandler(packet); // Your function to parse commands
    mesh.releaseReceiveBuffer();
  }

  // IMPORTANT: Call the library's loop function frequently
  AkitaMeshZmodem::TransferState state = akitaZmodem.loop();

  // Optional: Check the state and react if needed
  // if (state == AkitaMeshZmodem::TransferState::COMPLETE) { ... }
  // if (state == AkitaMeshZmodem::TransferState::ERROR) { ... }

  delay(20); // Yield CPU
}

// Your function to handle incoming packets and trigger transfers
void yourPacketHandler(ReceivedPacket& packet) {
  if (packet.isValid && packet.decoded.portnum == PortNum_TEXT_MESSAGE_APP) {
    String msg = packet.decoded.payload.toString();

    if (msg.startsWith("SEND:")) {
      String filename = msg.substring(5);
      if (filename.length() > 0 && filename.startsWith("/")) {
        if (akitaZmodem.getCurrentState() == AkitaMeshZmodem::TransferState::IDLE) {
          akitaZmodem.startSend(filename);
        } else {
          Serial.println("Transfer already active.");
        }
      }
    } else if (msg.startsWith("RECV:")) {
      String filename = msg.substring(5);
       if (filename.length() > 0 && filename.startsWith("/")) {
        if (akitaZmodem.getCurrentState() == AkitaMeshZmodem::TransferState::IDLE) {
          akitaZmodem.startReceive(filename);
        } else {
           Serial.println("Transfer already active.");
        }
      }
    }
  }
}
```
See the examples/Basic_Transfer/Basic_Transfer.ino sketch for a more complete example.

How it Works
Initiation: A user typically sends a command via a Meshtastic text message (e.g., SEND:/myfile.bin or RECV:/newfile.bin).

Library Call: Your code parses the command and calls akitaZmodem.startSend() or akitaZmodem.startReceive().

ZModem Handshake: The library uses the underlying ZModem library to initiate the transfer handshake over the mesh network via the custom MeshtasticZModemStream.

Packetization: Data is read from the source file (sending) or written to the destination file (receiving). The MeshtasticZModemStream intercepts the ZModem protocol data, breaks it into chunks small enough for Meshtastic packets, adds a header (packet identifier + sequence ID), and sends it using the Meshtastic library.

Reception & Reassembly: On the receiving end, the MeshtasticZModemStream listens for packets with the correct identifier. It checks the sequence ID to detect duplicates or loss. Valid data chunks are passed to the ZModem library, which handles the protocol logic and writes data to the destination file stream.

Error Handling & Retries: The ZModem protocol handles CRC checks and requests retransmission of corrupted or missing data segments. The AkitaMeshZmodem library adds timeouts and can manage high-level retries if the ZModem library reports persistent errors.

Completion/Error: The akitaZmodem.loop() function returns the current state (TRANSFERRING, COMPLETE, ERROR).

Configuration
Default configuration values are defined in src/AkitaMeshZmodemConfig.h. You can override these by:

Using #define before including the library:

#define AKZ_DEFAULT_ZMODEM_TIMEOUT 60000 // Override timeout to 60s
#include "AkitaMeshZmodem.h"

Using the setter methods after calling begin():

akitaZmodem.begin(mesh, SPIFFS, &Serial);
akitaZmodem.setTimeout(60000);
akitaZmodem.setMaxRetries(5);

Key Configuration Options:

AKZ_DEFAULT_ZMODEM_TIMEOUT: Timeout for ZModem operations (ms). Default: 30000.

AKZ_DEFAULT_MAX_PACKET_SIZE: Max Meshtastic payload size used. Default: 230. (Ensure this doesn't exceed radio limits minus 3 bytes for header).

AKZ_DEFAULT_PROGRESS_UPDATE_INTERVAL: How often progress is logged (ms). Default: 5000. (0 disables).

AKZ_DEFAULT_MAX_RETRY_COUNT: Currently informational - ZModem library handles internal retries. Default: 3.

AKZ_PACKET_IDENTIFIER: Byte used to mark ZModem packets. Default: 0xFF.

API Reference (Key Methods)
AkitaMeshZmodem(): Constructor.

~AkitaMeshZmodem(): Destructor.

begin(Meshtastic& meshInstance, FS& filesystem, Stream* debugStream): Initializes the library.

loop(): Processes ZModem state and communication. Call repeatedly. Returns TransferState.

startSend(const String& filePath): Initiates sending a file. Returns true on success.

startReceive(const String& filePath): Initiates receiving a file. Returns true on success.

abortTransfer(): Immediately stops the current transfer.

getCurrentState(): Returns the current TransferState.

getBytesTransferred(): Returns bytes transferred in the current/last session.

getTotalFileSize(): Returns the total file size (known after header on receive).

getFilename(): Returns the filename being transferred.

setTimeout(unsigned long timeoutMs): Sets the ZModem timeout.

setMaxRetries(uint16_t retries): Sets the max retry count (informational).

setProgressUpdateInterval(unsigned long intervalMs): Sets the progress logging interval.

setMaxPacketSize(size_t maxSize): Sets the max Meshtastic payload size.

Testing
Upload the examples/Basic_Transfer/Basic_Transfer.ino sketch (or your own implementation) to two Meshtastic devices.

Ensure both devices have SPIFFS initialized and optionally contain test files (the example sketch creates some).

On the sending device, send a Meshtastic text message like: SEND:/test_transfer.txt

On the receiving device, send a Meshtastic text message like: RECV:/received.txt (use the desired save path).

Monitor the Serial output on both devices for status messages, progress, and completion/error reports.

After a successful transfer, verify the content of the received file on the receiving device's filesystem (you might need another sketch or tool to read files from SPIFFS).

Contributing
Contributions are welcome! Please feel free to submit pull requests or open issues on the GitHub repository to suggest improvements, report bugs, or add features.
