/**
 * @file ZmodemModule.cpp
 * @author Akita Engineering
 * @brief Implementation of the Meshtastic ZModem Module.
 * @version 1.0.0
 * @date 2025-04-26
 *
 * @copyright Copyright (c) 2025 Akita Engineering
 *
 */

#include "ZmodemModule.h"
#include "mesh-core.h" // Access to Mesh Core functionalities if needed
#include "serial-interface.h" // For logging via LOG_INFO/LOG_ERROR etc.

// --- Module Initialization ---

// Constructor
ZmodemModule::ZmodemModule(MeshInterface& mesh) : Module(mesh) {
    // Constructor can initialize members if needed, but setup() is primary.
}

// Setup: Called once during firmware boot
void ZmodemModule::setup() {
    LOG_INFO("Initializing Zmodem Module...");

    // Check if filesystem is available (should be initialized by firmware core)
    if (!Filesystem.begin()) { // Use the global 'Filesystem' object from Meshtastic
        LOG_ERROR("ZmodemModule: Filesystem not available or failed to initialize! Module disabled.");
        // Optionally disable the module or prevent further initialization
        return;
    }

    // Initialize the Akita ZModem library instance
    // Pass the global 'mesh' instance, the global 'Filesystem', and the global 'Log' stream for debugging
    akitaZmodem.begin(mesh, Filesystem, &Log);

    // Optional: Configure the library if needed (using setters)
    // akitaZmodem.setTimeout(45000);
    // akitaZmodem.setProgressUpdateInterval(3000);

    LOG_INFO("Zmodem Module initialized successfully.");
}

// Loop: Called repeatedly by the firmware scheduler
void ZmodemModule::loop() {
    // Call the Akita ZModem library's loop function frequently
    // This handles the ZModem state machine, timeouts, and data processing
    AkitaMeshZmodem::TransferState currentState = akitaZmodem.loop();

    // Optional: Add any module-specific periodic tasks here
    // For example, report status via MQTT or Serial periodically if a transfer is active.
    static unsigned long lastStatusReport = 0;
    if (currentState != AkitaMeshZmodem::TransferState::IDLE && millis() - lastStatusReport > 15000) { // Report every 15s if active
        LOG_INFO("Zmodem Status: %d, Transferred: %d / %d",
                 (int)currentState,
                 akitaZmodem.getBytesTransferred(),
                 akitaZmodem.getTotalFileSize());
        lastStatusReport = millis();
    }
     // Report final state changes immediately
     static AkitaMeshZmodem::TransferState lastReportedState = AkitaMeshZmodem::TransferState::IDLE;
     if(currentState != lastReportedState && (currentState == AkitaMeshZmodem::TransferState::COMPLETE || currentState == AkitaMeshZmodem::TransferState::ERROR)) {
         LOG_INFO("Zmodem Final State: %d", (int)currentState);
         lastReportedState = currentState;
     } else if (currentState == AkitaMeshZmodem::TransferState::IDLE) {
         lastReportedState = AkitaMeshZmodem::TransferState::IDLE; // Reset reporting state
     }

}

// Handle Received Packets: Called by firmware when a packet arrives
bool ZmodemModule::handleReceived(MeshPacket& packet) {
    // Check if the packet is addressed to our ZModem PortNum
    if (packet.decoded.portnum == PortNum_ZMODEM_APP) {
        LOG_DEBUG("ZmodemModule received packet on PortNum %d", PortNum_ZMODEM_APP);

        // Check if it's a plain text message (used for commands)
        // Note: ZModem data packets are handled internally by the library's stream wrapper,
        // which listens for the AKZ_PACKET_IDENTIFIER (0xFF) on ANY portnum if not filtered earlier.
        // We only process explicit commands sent to our PortNum here.
        if (packet.decoded.datatype == MeshPacket_DataType_OPAQUE || packet.decoded.datatype == MeshPacket_DataType_TEXT_MESSAGE) { // Treat OPAQUE as potential text too
             String msg = packet.decoded.payload.toString();
             LOG_INFO("ZmodemModule received command: '%s' from 0x%x", msg.c_str(), packet.from);

             // Handle the command
             handleCommand(msg, packet.from);

             return true; // Packet was processed by this module
        } else {
             LOG_DEBUG("ZmodemModule ignoring non-text packet on PortNum %d", PortNum_ZMODEM_APP);
             return false; // Let other modules or default handling take it
        }

    } else {
        // Not for us, let other modules handle it
        return false;
    }
}

// --- Private Helper Methods ---

// Parse and handle incoming commands (SEND:/..., RECV:/...)
void ZmodemModule::handleCommand(const String& msg, NodeNum fromNodeId) {
    String command;
    String filename;

    // Basic parsing (case-sensitive)
    if (msg.startsWith("SEND:")) {
        command = "SEND";
        filename = msg.substring(5);
    } else if (msg.startsWith("RECV:")) {
        command = "RECV";
        filename = msg.substring(5);
    } else {
        LOG_WARNING("ZmodemModule: Received unknown command '%s'", msg.c_str());
        sendReply("Unknown command: " + msg, fromNodeId);
        return;
    }

    // Validate filename (must be absolute path)
    if (filename.length() == 0 || !filename.startsWith("/")) {
        LOG_ERROR("ZmodemModule: Invalid filename format in command '%s'", msg.c_str());
        sendReply("Error: Invalid filename format (must start with '/')", fromNodeId);
        return;
    }

    // Check if a transfer is already active
    if (akitaZmodem.getCurrentState() != AkitaMeshZmodem::TransferState::IDLE) {
        LOG_WARNING("ZmodemModule: Ignoring command '%s', transfer already in progress.", msg.c_str());
        sendReply("Error: Transfer already in progress (State: " + String((int)akitaZmodem.getCurrentState()) + ")", fromNodeId);
        return;
    }

    // Execute the command using the AkitaMeshZmodem library
    bool success = false;
    if (command == "SEND") {
        LOG_INFO("ZmodemModule: Initiating SEND for '%s'", filename.c_str());
        success = akitaZmodem.startSend(filename);
        if (success) {
            sendReply("OK: Starting SEND for " + filename, fromNodeId);
        } else {
            sendReply("Error: Failed to start SEND for " + filename, fromNodeId);
            LOG_ERROR("ZmodemModule: akitaZmodem.startSend failed for '%s'", filename.c_str());
        }
    } else if (command == "RECV") {
        LOG_INFO("ZmodemModule: Initiating RECV to '%s'", filename.c_str());
        success = akitaZmodem.startReceive(filename);
         if (success) {
            sendReply("OK: Starting RECV to " + filename + ". Waiting for sender...", fromNodeId);
        } else {
            sendReply("Error: Failed to start RECV to " + filename, fromNodeId);
            LOG_ERROR("ZmodemModule: akitaZmodem.startReceive failed for '%s'", filename.c_str());
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
    replyPacket.set_portnum(PortNum_ZMODEM_APP); // Send reply on the same PortNum
    replyPacket.set_datatype(MeshPacket_DataType_TEXT_MESSAGE); // Mark as text
    replyPacket.set_want_ack(false); // Replies usually don't need ACK
    replyPacket.set_hop_limit(mesh.getHopLimit()); // Use default hop limit

    // Send the packet via the mesh interface
    if (!mesh.sendPacket(&replyPacket)) {
        LOG_ERROR("Failed to send reply message to 0x%x", destinationNodeId);
    }
}
