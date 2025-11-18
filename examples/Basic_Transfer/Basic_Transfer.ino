/**
 * @file Basic_Transfer.ino
 * @author Akita Engineering
 * @brief Example sketch demonstrating basic file transfer using the AkitaMeshZmodem library.
 * Initiates transfers based on commands received via Meshtastic text messages.
 * @version 1.1.0
 * @date 2025-11-17 // Updated date
 *
 * @copyright Copyright (c) 2025 Akita Engineering
 *
 */

#include <Meshtastic.h>      // Main Meshtastic library header
#include <SPIFFS.h>          // Using SPIFFS for file storage in this example
#include "AkitaMeshZmodem.h" // Include the library header
#include "AkitaMeshZmodemConfig.h" // Include our port definitions

// --- Meshtastic Instance ---
// Assuming 'mesh' is the standard global instance provided by the Meshtastic firmware/environment.
// If running truly standalone, you might need:
// Meshtastic mesh; // And full initialization...


// --- Akita ZModem Instance ---
AkitaMeshZmodem akitaZmodem;

// --- Configuration ---
const char* TEST_FILENAME_TXT = "/test_transfer.txt";
const char* TEST_FILENAME_BIN = "/test_image.bin"; // Example binary file

// --- Function Prototypes ---
void onMeshtasticReceived(ReceivedPacket& packet); // Callback for Meshtastic packets
void createTestFiles(); // Helper to create files for sending
NodeNum parseNodeId(const char* str); // Helper to parse Node ID

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
    // Using polling in loop() for this example.
    Serial.println("Packet handling will be done via polling in loop().");


    // --- Initialize Akita ZModem Library ---
    // Pass the Meshtastic instance, the filesystem (SPIFFS), and Serial for debug output
    akitaZmodem.begin(mesh, SPIFFS, &Serial);
    Serial.println("AkitaMeshZmodem library initialized.");

    // --- Create Test Files (Optional) ---
    createTestFiles();

    Serial.println("\n--- Setup Complete ---");
    Serial.println("Listening for commands on Text Message Port (e.g., 4403)");
    Serial.println("Listening for data on ZModem Data Port (" + String(AKZ_ZMODEM_DATA_PORTNUM) + ")");
    Serial.println("\n--- Commands (send via Meshtastic text message) ---");
    Serial.println("  SEND:!NodeID:/<filename>  (e.g., SEND:!a1b2c3d4:/test_transfer.txt)");
    Serial.println("  RECV:/<filename>          (e.g., RECV:/received_file.txt)");
    Serial.println("----------------------------------------------------");
}

// --- Loop ---
void loop() {
    // --- Handle Incoming Meshtastic Packets (Polling Method) ---
    if (mesh.available()) {
        ReceivedPacket packet = mesh.receive();
        onMeshtasticReceived(packet); // Process the packet using our handler
        mesh.releaseReceiveBuffer();  // IMPORTANT: Release buffer after processing
    }

    // --- Update the Akita ZModem State Machine ---
    AkitaMeshZmodem::TransferState currentState = akitaZmodem.loop();

    // --- Optional: Provide feedback based on state changes ---
    static AkitaMeshZmodem::TransferState lastReportedState = AkitaMeshZmodem::TransferState::IDLE;

    if (currentState != lastReportedState) {
        switch (currentState) {
            case AkitaMeshZmodem::TransferState::IDLE:
                 if (lastReportedState != AkitaMeshZmodem::TransferState::IDLE) {
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
                Serial.println("[ZModem Status: COMPLETE]");
                break;
            case AkitaMeshZmodem::TransferState::ERROR:
                Serial.println("[ZModem Status: ERROR]");
                break;
        }
        lastReportedState = currentState; // Update last reported state
    }

    delay(20);
}

// --- Meshtastic Packet Handler ---
void onMeshtasticReceived(ReceivedPacket& packet) {
    
    // 1. Check for COMMANDS (on the standard text message port for this example)
    if (packet.isValid && packet.decoded.portnum == PortNum_TEXT_MESSAGE_APP && packet.decoded.payload.length() > 0) {

        String msg = packet.decoded.payload.toString();
        Serial.print("\nReceived Meshtastic Text: '"); Serial.print(msg); Serial.println("'");
        Serial.print("From Node: 0x"); Serial.println(packet.from, HEX);

        String command;
        String args;

        if (msg.startsWith("SEND:")) {
            command = "SEND";
            args = msg.substring(5); // e.g., "!a1b2c3d4:/path/file.txt"
        } else if (msg.startsWith("RECV:")) {
            command = "RECV";
            args = msg.substring(5); // e.g., "/path/file.txt"
        } else {
            Serial.println("(Not a ZModem command)");
            return; // Ignore
        }

        // Check if a transfer is already active
        if (akitaZmodem.getCurrentState() != AkitaMeshZmodem::TransferState::IDLE) {
            Serial.println("Ignoring command: Transfer already in progress.");
            Serial.print("Current state: "); Serial.println((int)akitaZmodem.getCurrentState());
            return;
        }
        
        // --- Handle RECV Command ---
        if (command == "RECV") {
            String filename = args;
            if (filename.length() == 0 || !filename.startsWith("/")) {
                Serial.println("Invalid RECV format: Filename must be an absolute path (start with '/').");
                return;
            }

            Serial.println("Initiating RECEIVE to file: " + filename);
            if (!akitaZmodem.startReceive(filename)) {
                Serial.println("-> Failed to initiate receive operation.");
            } else {
                 Serial.println("-> Receive operation started. Waiting for sender...");
            }
        
        // --- Handle SEND Command ---
        } else if (command == "SEND") {
            // Format: SEND:!NodeID:/path/file.txt
            int idEnd = args.indexOf(':');
            if (idEnd <= 0) {
                Serial.println("Invalid SEND format. Use SEND:!NodeID:/path/file.txt");
                return;
            }

            String nodeIdStr = args.substring(0, idEnd); // e.g., "!a1b2c3d4"
            String filename = args.substring(idEnd + 1); // e.g., "/path/file.txt"

            if (filename.length() == 0 || !filename.startsWith("/")) {
                Serial.println("Invalid SEND format: Filename must be an absolute path (start with '/').");
                return;
            }

            // Parse NodeID
            NodeNum destNodeId = parseNodeId(nodeIdStr.c_str());
            if (destNodeId == 0 || destNodeId == BROADCAST_ADDR) {
                Serial.println("Invalid SEND format: Invalid destination NodeID: " + nodeIdStr);
                return;
            }

            Serial.println("Initiating SEND for file: " + filename + " to Node 0x" + String(destNodeId, HEX));
            if (!akitaZmodem.startSend(filename, destNodeId)) {
                Serial.println("-> Failed to initiate send operation.");
            } else {
                 Serial.println("-> Send operation started successfully.");
            }
        }

    // 2. Check for DATA (on the dedicated data port)
    } else if (packet.isValid && packet.decoded.portnum == AKZ_ZMODEM_DATA_PORTNUM) {
        
        // This is a data packet, feed it to the library
        if (akitaZmodem.getCurrentState() == AkitaMeshZmodem::TransferState::RECEIVING) {
            Serial.print("."); // Indicate data packet received
            akitaZmodem.processDataPacket(packet);
        } else {
            // Not expecting data, ignore it
        }
    }
    // Ignore other packet types
}

// --- Helper Function to Create Test Files ---
void createTestFiles() {
    Serial.println("Creating test files on SPIFFS...");
    // ... (rest of the function is unchanged, omitted for brevity) ...
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


/**
 * @brief Parses a node ID string (like "!1234abcd" or "1234abcd") into a NodeNum.
 * This is a simplified helper; the main firmware has a more robust one.
 * @param str The string to parse.
 * @return NodeNum The parsed ID, or 0 on error.
 */
NodeNum parseNodeId(const char* str) {
    if (!str || str[0] == '\0') return 0;
    const char* idStr = str;
    if (str[0] == '!') {
        idStr = str + 1; // Skip the '!'
    }
    if (strlen(idStr) > 8) return 0; // Max 8 hex digits for 32-bit ID

    char* endptr;
    unsigned long nodeId = strtoul(idStr, &endptr, 16);
    
    if (*endptr != '\0' || nodeId == 0) { // Check for invalid chars or zero ID
        return 0;
    }
    return (NodeNum)nodeId;
}
