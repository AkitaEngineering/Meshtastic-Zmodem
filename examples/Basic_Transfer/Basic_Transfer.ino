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

        size_t payloadLen = packet.decoded.payload.length();
        const uint8_t* payloadBuf = packet.decoded.payload.getBuffer();
        if (!payloadBuf || payloadLen == 0) return;
        char* msg = (char*)malloc(payloadLen + 1);
        if (!msg) return;
        memcpy(msg, payloadBuf, payloadLen);
        msg[payloadLen] = '\0';

        char tmpBuf[192];
        snprintf(tmpBuf, sizeof(tmpBuf), "\nReceived Meshtastic Text: '%s'", msg);
        Serial.println(tmpBuf);
        Serial.print("From Node: 0x"); Serial.println(packet.from, HEX);

        const char* args = NULL;
        bool isSend = false;
        if (strncmp(msg, "SEND:", 5) == 0) {
            isSend = true;
            args = msg + 5;
        } else if (strncmp(msg, "RECV:", 5) == 0) {
            isSend = false;
            args = msg + 5;
        } else {
            Serial.println("(Not a ZModem command)");
            free(msg);
            return; // Ignore
        }

        // Check if a transfer is already active
        if (akitaZmodem.getCurrentState() != AkitaMeshZmodem::TransferState::IDLE) {
            Serial.println("Ignoring command: Transfer already in progress.");
            Serial.print("Current state: "); Serial.println((int)akitaZmodem.getCurrentState());
            return;
        }
        
        // --- Handle RECV Command ---
        if (!isSend) {
            const char* filename = args;
            if (!filename || filename[0] == '\0' || filename[0] != '/') {
                Serial.println("Invalid RECV format: Filename must be an absolute path (start with '/').");
                free(msg);
                return;
            }

            char out[160];
            snprintf(out, sizeof(out), "Initiating RECEIVE to file: %s", filename);
            Serial.println(out);
            if (!akitaZmodem.startReceive(filename)) {
                Serial.println("-> Failed to initiate receive operation.");
            } else {
                 Serial.println("-> Receive operation started. Waiting for sender...");
            }
            free(msg);
            return;
        }
        
        // --- Handle SEND Command ---
        // --- Handle SEND Command ---
        // Format: SEND:!NodeID:/path/file.txt
        const char* colon = strchr(args, ':');
        if (!colon || colon == args) {
            Serial.println("Invalid SEND format. Use SEND:!NodeID:/path/file.txt");
            free(msg);
            return;
        }

        size_t nodeIdLen = (size_t)(colon - args);
        if (nodeIdLen == 0 || nodeIdLen >= 32) {
            Serial.println("Invalid SEND format: NodeID length invalid");
            free(msg);
            return;
        }

        char nodeBuf[40];
        memcpy(nodeBuf, args, nodeIdLen);
        nodeBuf[nodeIdLen] = '\0';
        const char* filename = colon + 1;

        if (!filename || filename[0] == '\0' || filename[0] != '/') {
            Serial.println("Invalid SEND format: Filename must be an absolute path (start with '/').");
            free(msg);
            return;
        }

        // Parse NodeID
        NodeNum destNodeId = parseNodeId(nodeBuf);
        if (destNodeId == 0 || destNodeId == BROADCAST_ADDR) {
            Serial.print("Invalid SEND format: Invalid destination NodeID: "); Serial.println(nodeBuf);
            free(msg);
            return;
        }

        char out2[192];
        snprintf(out2, sizeof(out2), "Initiating SEND for file: %s to Node 0x%lX", filename, (unsigned long)destNodeId);
        Serial.println(out2);
        if (!akitaZmodem.startSend(filename, destNodeId)) {
            Serial.println("-> Failed to initiate send operation.");
        } else {
             Serial.println("-> Send operation started successfully.");
        }
        free(msg);
        return;
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
