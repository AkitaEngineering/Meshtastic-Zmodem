/**
 * @file main.cpp
 * @brief Minimal Arduino entry point for library build verification.
 * Replace this with your application code or use the example sketch.
 */

#include <Arduino.h>
#include <SPIFFS.h>
#include "AkitaMeshZmodem.h"

// Provided by Meshtastic firmware when the module is copied into a firmware
// tree. Defined here so the standalone PlatformIO build can link ZmodemModule.
FS& Filesystem = SPIFFS;
Stream& Log = Serial;

void setup() {
    // Library is initialized by calling akitaZmodem.begin() — see examples/
}

void loop() {
    // Library state machine is driven by akitaZmodem.loop() — see examples/
}
