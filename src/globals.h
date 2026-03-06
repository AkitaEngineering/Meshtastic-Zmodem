#ifndef GLOBALS_H
#define GLOBALS_H

// stubbed globals for build; real project defines mesh and filesystem objects
#include <FS.h>
// SPIFFS is a filesystem instance in the real SDK; stub it as a null reference
#define SPIFFS (*(FS*)nullptr)

#endif // GLOBALS_H
