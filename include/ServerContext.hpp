#ifndef SERVER_CONTEXT_HPP
#define SERVER_CONTEXT_HPP

#include "SlabSlotManager.hpp"
#include "Storage.hpp"
#include <liburing.h>
#include "AofManager.hpp"
#include <list>


class ServerContext {
    struct PubSubPattern {
    Client* client;
    std::string pattern;
};
public: // <-- Add this line!
    SlabSlotManager slabManager;
    Storage db;
    AofManager aof;
    std::unordered_map<std::string, std::list<Client*>> pubsub_channels;
std::list<PubSubPattern> pubsub_patterns;
std::mutex pubsub_mutex;
    // The constructor must also be public so 'ctx' can be created
    ServerContext() : slabManager(1024), aof("appendonly.aof.-1.base.aof") {}
};

extern ServerContext ctx; 
#endif