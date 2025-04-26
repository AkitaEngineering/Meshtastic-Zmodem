/**
 * @file ZmodemModule.h
 * @author Akita Engineering
 * @brief Meshtastic Module for handling ZModem file transfers using the AkitaMeshZmodem library.
 * @version 1.0.0
 * @date 2025-04-26
 *
 * @copyright Copyright (c) 2025 Akita Engineering
 *
 */

#pragma once // Use pragma once for header guard

#include "globals.h"   // Access to global objects like mesh, Filesystem
#include "module.h"    // Base class for Meshtastic modules
#include <AkitaMeshZmodem.h> // Include the ZModem library we created

// Define a unique PortNum for ZModem commands and potentially data
// Choose a number in the application range (PRIVATE_APP_MAX - 255?)
// Check Meshtastic documentation/source for currently unused PortNums.
// Using 250 as an example - VERIFY THIS IS NOT IN USE!
const int PortNum_ZMODEM_APP = 250;

/**
 * @brief A Meshtastic Module to enable ZModem file transfers.
 */
class ZmodemModule : public Module {
public:
    /**
     * @brief Constructor.
     * @param mesh Reference to the primary MeshInterface object.
     */
    ZmodemModule(MeshInterface& mesh);

    /**
     * @brief Module setup function, called once during firmware initialization.
     */
    virtual void setup() override;

    /**
     * @brief Module loop function, called repeatedly.
     */
    virtual void loop() override;

    /**
     * @brief Handles received packets intended for this module.
     * @param packet The received MeshPacket.
     * @return true if the packet was processed by this module.
     * @return false otherwise.
     */
    virtual bool handleReceived(MeshPacket& packet) override;

private:
    MeshInterface& mesh; // Reference to the main mesh interface
    AkitaMeshZmodem akitaZmodem; // Instance of our ZModem library handler

    // Optional: Add methods for handling MQTT, Serial commands if needed later

    /**
     * @brief Parses incoming text commands for SEND/RECV operations.
     * @param msg The command string.
     * @param fromNodeId The Node ID of the sender.
     */
    void handleCommand(const String& msg, NodeNum fromNodeId);

    /**
     * @brief Helper to send a reply text message back to the command sender.
     * @param message The text message to send.
     * @param destinationNodeId The Node ID to send the reply to.
     */
    void sendReply(const String& message, NodeNum destinationNodeId);
};
