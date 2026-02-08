#ifndef SERVER_CONTEXT_HPP
#define SERVER_CONTEXT_HPP

#include "SlabSlotManager.hpp"
#include "Storage.hpp"
#include <liburing.h>

class ServerContext {
public: // <-- Add this line!
    SlabSlotManager slabManager;
    Storage db;
    
    // The constructor must also be public so 'ctx' can be created
    ServerContext() : slabManager(1024) {}
};

extern ServerContext ctx; 
#endif