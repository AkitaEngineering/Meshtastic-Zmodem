#ifndef GLOBALS_H
#define GLOBALS_H

#include <FS.h>
#include <SPIFFS.h>

// Global objects expected by ZmodemModule (provided by Meshtastic firmware at runtime).
// Declared as extern references for standalone library compilation.
extern FS& Filesystem;
extern Stream& Log;

#endif // GLOBALS_H
