# Akita Meshtastic ZModem Plugin

This project provides a ZModem protocol plugin for Meshtastic, enabling the transfer of binary files over the LoRa mesh network.

## Features

* **Binary File Transfer:** Enables the transfer of any binary file over Meshtastic.
* **ZModem Protocol:** Utilizes the reliable ZModem protocol for file transfers.
* **Custom Meshtastic Stream:** Implements a custom `Stream` class to handle Meshtastic packet fragmentation and reassembly.
* **SPIFFS Integration:** Reads and writes files to the ESP32's SPIFFS file system.
* **Simple Command Structure:** Initiates transfers using simple Meshtastic text commands.
* **Robust Error Handling:** Includes error handling for file operations and ZModem transfer errors, including retries.
* **Packet ID:** Implements a packet ID system to prevent packet loss from corrupting the stream.
* **Progress Tracking:** Displays progress information during file transfers.
* **Configurability:** Provides configuration macros for timeout, packet size, progress update interval, and max retry count.

## Requirements

* Meshtastic-compatible devices (ESP32-based recommended)
* Arduino IDE with ESP32 board support
* Required Arduino Libraries:
    * Meshtastic
    * ZModem
    * StreamUtils

## Installation

1.  **Install Arduino IDE and ESP32 Board Support:**
    * If you haven't already, install the Arduino IDE and add support for the ESP32 board.
2.  **Install Required Libraries:**
    * Open the Arduino IDE Library Manager (Sketch -> Include Library -> Manage Libraries...).
    * Search for and install the following libraries:
        * Meshtastic
        * ZModem
        * StreamUtils
3.  **Download the Code:**
    * Download or clone this repository to your computer.
4.  **Upload the Code:**
    * Connect your Meshtastic device to your computer.
    * In the Arduino IDE, open the `Meshtastic_ZModem.ino` file.
    * Select your board and port.
    * Upload the `Meshtastic_ZModem.ino` sketch to your board.

## Usage

1.  **Create a File:** Create a binary file on one of the Meshtastic devices SPIFFS.
2.  **Initiate Send:** On the device with the file, send a Meshtastic text message with the format `ZMODEM_SEND:filename.bin` (replace `filename.bin` with your actual filename).
3.  **Initiate Receive:** On the receiving device, send a Meshtastic text message with the format `ZMODEM_RECEIVE:filename.bin` (use the same filename).
4.  **Monitor:** Observe the serial monitor on both devices for transfer status, progress, and any error messages.

## Configuration

The following configuration macros are available in the `Meshtastic_ZModem.ino` file:

* `ZMODEM_BUFFER_SIZE`: The size of the ZModem buffer.
* `ZMODEM_TIMEOUT`: The timeout for ZModem operations (milliseconds).
* `MAX_PACKET_SIZE`: The maximum size of Meshtastic packets.
* `PROGRESS_UPDATE_INTERVAL`: The interval for displaying progress updates (milliseconds).
* `maxRetryCount`: The maximum number of retry attempts.

## Testing

1.  Upload the code to two Meshtastic devices.
2.  Create a test file on one of the devices SPIFFS.
3.  Use the commands "ZMODEM_SEND:filename.bin" and "ZMODEM_RECEIVE:filename.bin" to test the plugin.
4.  Verify that the received file is identical to the original file.

## Key Improvements

* **Retry Mechanism:** Implemented a retry mechanism for both sending and receiving, with a configurable `maxRetryCount`.
* **Progress Tracking:** Added progress tracking using `displayProgress()`, which shows the percentage or bytes transferred. Progress is updated at a configurable interval (`PROGRESS_UPDATE_INTERVAL`).
* **File Size Tracking:** `totalFileSize` and `bytesTransferred` are used to track progress. `totalFileSize` is determined when sending, and is set to 0 (unknown) when receiving.
* **Clarity and Feedback:** Added more informative messages, including retry attempts and progress information.
* **Robustness:** Improved error handling with retries and clear error messages.
* **Configurability:** Added configuration macros for timeout, packet size, progress update interval, and max retry count.
* **Readability:** Improved code formatting and added comments for clarity.

## Akita Engineering

This project is developed and maintained by Akita Engineering. We are dedicated to creating innovative solutions for LoRa and Meshtastic networks.

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues to suggest improvements or report bugs.
