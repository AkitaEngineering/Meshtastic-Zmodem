#ifndef MODULE_H
#define MODULE_H

#include <Meshtastic.h>

// MeshInterface is a typedef for the Meshtastic API used as a mesh reference
using MeshInterface = Meshtastic;

class Module {
public:
    MeshInterface& mesh;
    Module(MeshInterface& m) : mesh(m) {}
    virtual ~Module() {}
    virtual void setup() {}
    virtual void loop() {}
    virtual bool handleReceived(MeshPacket&) { return false; }
};

#endif // MODULE_H
