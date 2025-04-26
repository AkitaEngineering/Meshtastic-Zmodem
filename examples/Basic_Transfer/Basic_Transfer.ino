/**
 * @file Basic_Transfer.ino
 * @author Akita Engineering
 * @brief Example sketch demonstrating basic file transfer using the AkitaMeshZmodem library.
 * Initiates transfers based on commands received via Meshtastic text messages.
 * @version 1.0.0
 * @date 2025-04-26
 *
 * @copyright Copyright (c) 2025 Akita Engineering
 *
 */

#include <Meshtastic.h> // Main Meshtastic library header
#include <SPIFFS.h>     // Using SPIFFS for file storage in this example
#include "AkitaMeshZmodem.h" // Include the library header

// --- Meshtastic Instance ---
// Assuming 'mesh' is the standard global instance provided by the Meshtastic firmware/environment.
// If running truly standalone, you might need:
// Meshtastic mesh;
// RadioInterface radio(config); // Setup radio interface
// Queue queue;
// NodeDB db;
// Power power;
// MeshInterface mesh(NULL, &radio, &queue, &db, &power);


// --- Akita ZModem Instance ---
AkitaMeshZmodem akitaZmodem;

// --- Configuration ---
const char* TEST_FILENAME_TXT = "/test_transfer.txt";
const char* TEST_FILENAME_BIN = "/test_image.bin"; // Example binary file

// --- Function Prototypes ---
void onMeshtasticReceived(ReceivedPacket& packet); // Callback for Meshtastic packets
void createTestFiles(); // Helper to create files for sending

// --- Setup ---
void setup() {
    Serial.begin(115200);
    delay(2000); // Wait for serial connection
    Serial.println("\n\n--- Akita Meshtastic ZModem - Basic Transfer Example ---");

    // --- Initialize Filesystem ---
    Serial.print("Initializing SPIFFS... ");
    if (!SPIFFS.begin(true)) { // true = format if mount failed
        Serial.println("Failed!");
        Serial.println("SPIFFS Mount Failed! Halting.");
        while (1) delay(1000); // Stop execution
    }
    Serial.println("OK.");

    // --- Initialize Meshtastic (if needed, often handled by firmware base) ---
    // mesh.init(); // Example if needed
    // mesh.setDebugOutputStream(&Serial); // Route Meshtastic logs to Serial
    Serial.println("Meshtastic initialized (assumed).");

    // --- Set Meshtastic Packet Handler ---
    // Use the appropriate method from your Meshtastic setup to register the callback
    // Example: mesh.addOnReceiveCallback(&onMeshtasticReceived);
    // Or if using a different structure: mesh.radio.onReceive = &onMeshtasticReceived; // Check actual API
    // For now, we'll poll in loop(), but a callback is better.
    Serial.println("Packet handling will be done via polling in loop().");


    // --- Initialize Akita ZModem Library ---
    // Pass the Meshtastic instance, the filesystem (SPIFFS), and Serial for debug output
    akitaZmodem.begin(mesh, SPIFFS, &Serial);
    Serial.println("AkitaMeshZmodem library initialized.");

    // --- Optional: Configure ZModem settings ---
    // akitaZmodem.setTimeout(45000); // Increase timeout to 45 seconds
    // akitaZmodem.setMaxRetries(5);   // Increase retries
    // akitaZmodem.setProgressUpdateInterval(2000); // Update progress every 2 seconds

    // --- Create Test Files (Optional) ---
    createTestFiles();

    Serial.println("\n--- Setup Complete ---");
    Serial.println("Waiting for commands via Meshtastic text messages:");
    Serial.println("  SEND:/<filename>  (e.g., SEND:/test_transfer.txt)");
    Serial.println("  RECV:/<filename>  (e.g., RECV:/received_file.txt)");
    Serial.println("----------------------------------------------------");
}

// --- Loop ---
void loop() {
    // --- Handle Incoming Meshtastic Packets (Polling Method) ---
    // It's generally better to use the Meshtastic onReceive callback if possible.
    if (mesh.available()) {
        ReceivedPacket packet = mesh.receive();
        onMeshtasticReceived(packet); // Process the packet using our handler
        mesh.releaseReceiveBuffer();  // IMPORTANT: Release buffer after processing
    }

    // --- Update the Akita ZModem State Machine ---
    // This MUST be called frequently to handle the ZModem protocol timings,
    // process buffered data, and manage timeouts.
    AkitaMeshZmodem::TransferState currentState = akitaZmodem.loop();

    // --- Optional: Provide feedback based on state changes ---
    static AkitaMeshZmodem::TransferState lastReportedState = AkitaMeshZmodem::TransferState::IDLE;

    if (currentState != lastReportedState) {
        switch (currentState) {
            case AkitaMeshZmodem::TransferState::IDLE:
                // Only report if we were previously busy
                if (lastReportedState != AkitaMeshZmodem::TransferState::COMPLETE && lastReportedState != AkitaMeshZmodem::TransferState::ERROR) {
                     Serial.println("[ZModem Status: IDLE]");
                }
                break;
            case AkitaMeshZmodem::TransferState::SENDING:
                Serial.println("[ZModem Status: SENDING]");
                break;
            case AkitaMeshZmodem::TransferState::RECEIVING:
                Serial.println("[ZModem Status: RECEIVING]");
                break;
            case AkitaMeshZmodem::TransferState::COMPLETE:
                // Final message printed by the library's internal logging
                Serial.println("[ZModem Status: COMPLETE]");
                break;
            case AkitaMeshZmodem::TransferState::ERROR:
                 // Final message printed by the library's internal logging
                Serial.println("[ZModem Status: ERROR]");
                break;
        }
        lastReportedState = currentState; // Update last reported state
    }

    // Add a small delay to prevent busy-waiting and allow other tasks
    delay(20);
}

// --- Meshtastic Packet Handler ---
void onMeshtasticReceived(ReceivedPacket& packet) {
    // Check if it's a text message potentially containing a command
    if (packet.isValid && packet.decoded.portnum == PortNum_TEXT_MESSAGE_APP && packet.decoded.payload.length() > 0) {

        String msg = packet.decoded.payload.toString();
        Serial.print("\nReceived Meshtastic Text: '"); Serial.print(msg); Serial.println("'");
        Serial.print("From Node: 0x"); Serial.println(packet.from, HEX);

        // Commands are case-sensitive: SEND:/<filepath> or RECV:/<filepath>

        String command;
        String filename;

        if (msg.startsWith("SEND:")) {
            command = "SEND";
            filename = msg.substring(5);
        } else if (msg.startsWith("RECV:")) {
            command = "RECV";
            filename = msg.substring(5);
        } else {
            // Not a ZModem command, just print it
            Serial.println("(Not a ZModem command)");
            return; // Ignore
        }

        // Validate filename format (must start with '/')
        if (filename.length() == 0 || !filename.startsWith("/")) {
            Serial.println("Invalid command format: Filename must be an absolute path (start with '/').");
            return;
        }

        // Check if a transfer is already active
        if (akitaZmodem.getCurrentState() != AkitaMeshZmodem::TransferState::IDLE) {
            Serial.println("Ignoring command: Transfer already in progress.");
            Serial.print("Current state: "); Serial.println((int)akitaZmodem.getCurrentState());
            return;
        }

        // Execute the command
        if (command == "SEND") {
            Serial.println("Initiating SEND for file: " + filename);
            if (!akitaZmodem.startSend(filename)) {
                Serial.println("-> Failed to initiate send operation.");
            } else {
                 Serial.println("-> Send operation started successfully.");
            }
        } else if (command == "RECV") {
            Serial.println("Initiating RECEIVE to file: " + filename);
             if (!akitaZmodem.startReceive(filename)) {
                Serial.println("-> Failed to initiate receive operation.");
            } else {
                 Serial.println("-> Receive operation started. Waiting for sender...");
            }
        }

    } else if (packet.isValid && packet.decoded.portnum == PortNum_APP_MAX) {
        // Potentially a ZModem data packet (check identifier if needed)
        // The library's internal stream handler deals with these via mesh.available() / mesh.receive()
        // So, we don't strictly need to handle them here if polling in loop()
         // Serial.print("."); // Indicate some packet activity
    }
    // Ignore other packet types
}

// --- Helper Function to Create Test Files ---
void createTestFiles() {
    Serial.println("Creating test files on SPIFFS...");

    // --- Text File ---
    Serial.print("Creating "); Serial.print(TEST_FILENAME_TXT); Serial.print("... ");
    File fileTxt = SPIFFS.open(TEST_FILENAME_TXT, FILE_WRITE);
    if (!fileTxt) {
        Serial.println("Failed to open!");
    } else {
        fileTxt.println("Hello from Akita Meshtastic ZModem!");
        fileTxt.println("This is line 2.");
        for (int i = 0; i < 10; i++) {
            fileTxt.print("Line "); fileTxt.print(i + 3); fileTxt.println("abcdefghijklmnopqrstuvwxyz0123456789");
        }
        fileTxt.println("End of test file.");
        size_t fileSize = fileTxt.size();
        fileTxt.close();
        Serial.print("OK ("); Serial.print(fileSize); Serial.println(" bytes).");
    }

    // --- Binary File (Example: ~1KB of dummy data) ---
     Serial.print("Creating "); Serial.print(TEST_FILENAME_BIN); Serial.print("... ");
    File fileBin = SPIFFS.open(TEST_FILENAME_BIN, FILE_WRITE);
     if (!fileBin) {
        Serial.println("Failed to open!");
    } else {
        uint8_t buffer[64];
        for(int i=0; i<16; i++) { // Write 16 * 64 bytes = 1024 bytes
            for(int j=0; j<64; j++) {
                buffer[j] = (i * 64 + j) % 256; // Fill buffer with some pattern
            }
            fileBin.write(buffer, 64);
        }
        size_t fileSize = fileBin.size();
        fileBin.close();
        Serial.print("OK ("); Serial.print(fileSize); Serial.println(" bytes).");
    }
     Serial.println("Test file creation finished.");
}
