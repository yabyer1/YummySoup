#ifndef SERVER_CONTEXT_HPP
#define SERVER_CONTEXT_HPP

#include "SlabSlotManager.hpp"
#include "Storage.hpp"
#include <liburing.h>
#include "AofManager.hpp"
class ServerContext {
public: // <-- Add this line!
    SlabSlotManager slabManager;
    Storage db;
    AofManager aof;
    // The constructor must also be public so 'ctx' can be created
    ServerContext() : slabManager(1024), aof("appendonly.aof") {}
};

extern ServerContext ctx; 
#endif