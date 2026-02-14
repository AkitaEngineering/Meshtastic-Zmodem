/**
 * @file ZmodemModule.cpp
 * @author Akita Engineering
 * @brief Implementation of the Meshtastic ZModem Module.
 * @version 1.1.0
 * @date 2025-11-17 // Updated date
 *
 * @copyright Copyright (c) 2025 Akita Engineering
 *
 */

#include "ZmodemModule.h"
#include "AkitaMeshZmodemConfig.h" // Include our port definitions
#include "mesh-core.h" // Access to Mesh Core functionalities if needed
#include "serial-interface.h" // For logging via LOG_INFO/LOG_ERROR etc.
// #include "utilities.h" // For parseNodeId - not available, define locally

// --- Module Initialization ---

// Constructor
ZmodemModule::ZmodemModule(MeshInterface& mesh) : Module(mesh) {
    // Constructor
}

// Setup: Called once during firmware boot
void ZmodemModule::setup() {
    LOG_INFO("Initializing Zmodem Module...");

    // Check if filesystem is available (should be initialized by firmware core)
    // Use the global 'Filesystem' object from Meshtastic
    if (!Filesystem.begin()) { 
        LOG_ERROR("ZmodemModule: Filesystem not available or failed to initialize! Module disabled.");
        return;
    }

    // Initialize the Akita ZModem library instance
    // Pass the global 'mesh' instance, the global 'Filesystem', and the global 'Log' stream for debugging
    akitaZmodem.begin(mesh, Filesystem, &Log);

    // Optional: Configure the library if needed (using setters)
    // akitaZmodem.setTimeout(45000);
    // akitaZmodem.setProgressUpdateInterval(3000);

    LOG_INFO("Zmodem Module initialized successfully. Listening for commands on PortNum %d.", AKZ_ZMODEM_COMMAND_PORTNUM);
}

// Loop: Called repeatedly by the firmware scheduler
void ZmodemModule::loop() {
    // Call the Akita ZModem library's loop function frequently
    // This handles the ZModem state machine, timeouts, and data processing
    AkitaMeshZmodem::TransferState currentState = akitaZmodem.loop();

    // Optional: Add any module-specific periodic tasks here
    static unsigned long lastStatusReport = 0;
    static AkitaMeshZmodem::TransferState lastReportedState = AkitaMeshZmodem::TransferState::IDLE;

    if (currentState != lastReportedState) {
        if (currentState == AkitaMeshZmodem::TransferState::COMPLETE || currentState == AkitaMeshZmodem::TransferState::ERROR) {
            // Final state messages are logged by the library itself
            LOG_INFO("Zmodem transfer finished. State: %d", (int)currentState);
        } else {
             LOG_INFO("Zmodem Status: %d", (int)currentState);
        }
        lastReportedState = currentState;
        lastStatusReport = millis();
    }
    // Periodic status update if busy
    else if (currentState != AkitaMeshZmodem::TransferState::IDLE && millis() - lastStatusReport > 15000) { // Report every 15s if active
        LOG_INFO("Zmodem Status: %d, Transferred: %d / %d",
                 (int)currentState,
                 akitaZmodem.getBytesTransferred(),
                 akitaZmodem.getTotalFileSize());
        lastStatusReport = millis();
    }
}

// Handle Received Packets: Called by firmware when a packet arrives
bool ZmodemModule::handleReceived(MeshPacket& packet) {
    // Check if the packet is addressed to one of our PortNums
    
    // 1. Is it a COMMAND packet?
    if (packet.decoded.portnum == AKZ_ZMODEM_COMMAND_PORTNUM) {
        LOG_DEBUG("ZmodemModule received packet on COMMAND PortNum %d", AKZ_ZMODEM_COMMAND_PORTNUM);

        // Expecting text commands (e.g., OPAQUE or TEXT_MESSAGE type)
        if (packet.decoded.datatype == MeshPacket_DataType_OPAQUE || packet.decoded.datatype == MeshPacket_DataType_TEXT_MESSAGE) {
             String msg = packet.decoded.payload.toString();
             LOG_INFO("ZmodemModule received command: '%s' from 0x%x", msg.c_str(), packet.from);

             // Handle the command
             handleCommand(msg, packet.from);

             return true; // Packet was processed by this module
        } else {
             LOG_DEBUG("ZmodemModule ignoring non-text packet on COMMAND PortNum %d", AKZ_ZMODEM_COMMAND_PORTNUM);
             return false; // Let other modules or default handling take it
        }

    // 2. Is it a DATA packet?
    } else if (packet.decoded.portnum == AKZ_ZMODEM_DATA_PORTNUM) {
        // This is a data packet, feed it to the library's stream processor
        // Only process if we are actively receiving
        if (akitaZmodem.getCurrentState() == AkitaMeshZmodem::TransferState::RECEIVING) {
            LOG_DEBUG("ZmodemModule pushing DATA packet to library.");
            akitaZmodem.processDataPacket(packet);
            return true; // We consumed this packet
        } else {
            LOG_DEBUG("ZmodemModule ignoring DATA packet (not in RECEIVING state).");
            return false; // Not actively receiving, let it be dropped
        }

    // 3. Not for us
    } else {
        return false;
    }
}

// --- Private Helper Methods ---

// Parse and handle incoming commands (SEND:!NodeID:/path, RECV:/path)
void ZmodemModule::handleCommand(const String& msg, NodeNum fromNodeId) {
    String command;
    String args;

    // Basic parsing (case-sensitive)
    if (msg.startsWith("SEND:")) {
        command = "SEND";
        args = msg.substring(5); // e.g., "!a1b2c3d4:/path/file.txt"
    } else if (msg.startsWith("RECV:")) {
        command = "RECV";
        args = msg.substring(5); // e.g., "/path/file.txt"
    } else {
        LOG_WARNING("ZmodemModule: Received unknown command '%s'", msg.c_str());
        sendReply("Unknown command: " + msg, fromNodeId);
        return;
    }

    // Check if a transfer is already active
    if (akitaZmodem.getCurrentState() != AkitaMeshZmodem::TransferState::IDLE) {
        LOG_WARNING("ZmodemModule: Ignoring command '%s', transfer already in progress.", msg.c_str());
        sendReply("Error: Transfer already in progress (State: " + String((int)akitaZmodem.getCurrentState()) + ")", fromNodeId);
        return;
    }

    // --- Execute RECV command ---
    if (command == "RECV") {
        String filename = args;
        // Validate filename (must be absolute path)
        if (filename.length() == 0 || !filename.startsWith("/")) {
            LOG_ERROR("ZmodemModule: Invalid RECV filename format: '%s'", filename.c_str());
            sendReply("Error: Invalid RECV format. Use RECV:/path/to/save.txt", fromNodeId);
            return;
        }

        LOG_INFO("ZmodemModule: Initiating RECEIVE to '%s'", filename.c_str());
        bool success = akitaZmodem.startReceive(filename);
         if (success) {
            sendReply("OK: Starting RECV to " + filename + ". Waiting for sender...", fromNodeId);
        } else {
            sendReply("Error: Failed to start RECV to " + filename, fromNodeId);
            LOG_ERROR("ZmodemModule: akitaZmodem.startReceive failed for '%s'", filename.c_str());
        }

    // --- Execute SEND command ---
    } else if (command == "SEND") {
        // New format: SEND:!NodeID:/path/file.txt
        int idEnd = args.indexOf(':');
        if (idEnd <= 0) {
            LOG_ERROR("ZmodemModule: Invalid SEND format. No ':' separator for NodeID. Got: '%s'", args.c_str());
            sendReply("Error: Invalid SEND format. Use SEND:!NodeID:/path/file.txt", fromNodeId);
            return;
        }

        String nodeIdStr = args.substring(0, idEnd); // e.g., "!a1b2c3d4"
        String filename = args.substring(idEnd + 1); // e.g., "/path/file.txt"

        // Validate filename
        if (filename.length() == 0 || !filename.startsWith("/")) {
            LOG_ERROR("ZmodemModule: Invalid SEND filename format: '%s'", filename.c_str());
            sendReply("Error: Invalid SEND filename format. Must start with '/'.", fromNodeId);
            return;
        }

        // Parse NodeID
        NodeNum destNodeId = parseNodeId(nodeIdStr.c_str());
        if (destNodeId == 0 || destNodeId == BROADCAST_ADDR) {
             LOG_ERROR("ZmodemModule: Invalid SEND destination NodeID: '%s'", nodeIdStr.c_str());
            sendReply("Error: Invalid SEND destination NodeID: " + nodeIdStr, fromNodeId);
            return;
        }
        
        // Execute the command
        LOG_INFO("ZmodemModule: Initiating SEND for '%s' to Node 0x%x", filename.c_str(), destNodeId);
        bool success = akitaZmodem.startSend(filename, destNodeId);
        if (success) {
            sendReply("OK: Starting SEND for " + filename + " to " + nodeIdStr, fromNodeId);
        } else {
            sendReply("Error: Failed to start SEND for " + filename, fromNodeId);
            LOG_ERROR("ZmodemModule: akitaZmodem.startSend failed for '%s'", filename.c_str());
        }
    }
}

// Send a reply text message back to the sender
void ZmodemModule::sendReply(const String& message, NodeNum destinationNodeId) {
    LOG_DEBUG("Sending reply to 0x%x: %s", destinationNodeId, message.c_str());

    // Create a MeshPacket for the reply
    MeshPacket replyPacket;
    replyPacket.set_to(destinationNodeId);
    replyPacket.set_from(mesh.getNodeNum()); // Set source as this node
    replyPacket.set_payload((const uint8_t*)message.c_str(), message.length());
    replyPacket.set_portnum(AKZ_ZMODEM_COMMAND_PORTNUM); // Send reply on the COMMAND port
    replyPacket.set_datatype(MeshPacket_DataType_TEXT_MESSAGE); // Mark as text
    replyPacket.set_want_ack(false); // Replies usually don't need ACK
    replyPacket.set_hop_limit(mesh.getHopLimit()); // Use default hop limit

    // Send the packet via the mesh interface
    if (!mesh.sendPacket(&replyPacket)) {
        LOG_ERROR("Failed to send reply message to 0x%x", destinationNodeId);
    }
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
