/**
 * @file AkitaMeshZmodemConfig.h
 * @author Akita Engineering
 * @brief Default configuration values for the AkitaMeshZmodem library.
 * Users can override these by defining them before including AkitaMeshZmodem.h,
 * or by using the configuration setter methods.
 * @version 1.0.0
 * @date 2025-04-26
 *
 * @copyright Copyright (c) 2025 Akita Engineering
 *
 */

#ifndef AKITA_MESH_ZMODEM_CONFIG_H
#define AKITA_MESH_ZMODEM_CONFIG_H

// --- Default Configuration Values ---

/**
 * @brief Default timeout for ZModem operations in milliseconds.
 * This needs to be long enough to account for LoRa latency and potential retries.
 */
#ifndef AKZ_DEFAULT_ZMODEM_TIMEOUT
#define AKZ_DEFAULT_ZMODEM_TIMEOUT 30000 // 30 seconds
#endif

/**
 * @brief Default maximum payload size for Meshtastic packets used by this library.
 * This should not exceed the actual MTU (Maximum Transmission Unit) of the
 * Meshtastic network/radio configuration (typically around 230-240 bytes).
 * The ZModem stream wrapper needs 3 bytes for header (ID + Packet ID).
 */
#ifndef AKZ_DEFAULT_MAX_PACKET_SIZE
#define AKZ_DEFAULT_MAX_PACKET_SIZE 230
#endif

/**
 * @brief Default interval in milliseconds for displaying progress updates via the debug stream.
 * Set to 0 to disable periodic progress updates.
 */
#ifndef AKZ_DEFAULT_PROGRESS_UPDATE_INTERVAL
#define AKZ_DEFAULT_PROGRESS_UPDATE_INTERVAL 5000 // 5 seconds
#endif

/**
 * @brief Default maximum number of retry attempts when a ZModem error occurs during transfer.
 */
#ifndef AKZ_DEFAULT_MAX_RETRY_COUNT
#define AKZ_DEFAULT_MAX_RETRY_COUNT 3
#endif

/**
 * @brief The byte value used to identify packets belonging to this ZModem stream.
 * This helps differentiate ZModem data from other Meshtastic traffic.
 * Ensure this doesn't conflict with other protocols on the network.
 */
#ifndef AKZ_PACKET_IDENTIFIER
#define AKZ_PACKET_IDENTIFIER 0xFF // Example identifier (255)
#endif

/**
 * @brief Internal buffer size for the MeshtasticZModemStream receive buffer.
 * Should be at least AKZ_DEFAULT_MAX_PACKET_SIZE.
 */
#ifndef AKZ_STREAM_RX_BUFFER_SIZE
#define AKZ_STREAM_RX_BUFFER_SIZE 256 // Slightly larger than max packet size
#endif

/**
 * @brief Internal buffer size for the MeshtasticZModemStream transmit buffer.
 * Should be at least AKZ_DEFAULT_MAX_PACKET_SIZE.
 */
#ifndef AKZ_STREAM_TX_BUFFER_SIZE
#define AKZ_STREAM_TX_BUFFER_SIZE 256 // Slightly larger than max packet size
#endif


#endif // AKITA_MESH_ZMODEM_CONFIG_H
