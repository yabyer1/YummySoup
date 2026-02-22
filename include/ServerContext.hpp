#ifndef SERVER_CONTEXT_HPP
#define SERVER_CONTEXT_HPP

#include "SlabSlotManager.hpp"
#include "Storage.hpp"
#include <liburing.h>
#include "AofManager.hpp"
#include "ReadyQueue.hpp"
#include <list>
#include <unordered_map>
#include <mutex>

// Forward declaration to avoid circular includes
class Client;

class ServerContext {
public:
    // 1. Communication Channels
    ReadyQueue<std::shared_ptr<Client>> ready_clients; // Use shared_ptr for safety

    // 2. Rings: Separate I/O paths
    struct io_uring read_ring;
    struct io_uring write_ring;

    // 3. Memory Management
    // We need managers for both rings because they have separate registered buffers
    SlabSlotManager readSlabManager;
    SlabSlotManager writeSlabManager;

    // 4. Data & Persistence
    Storage db;
    AofManager aof;

    // 5. Pub/Sub Logic
    struct PubSubPattern {
        std::shared_ptr<Client> client;
        std::string pattern;
    };
    std::unordered_map<std::string, std::list<std::shared_ptr<Client>>> pubsub_channels;
    std::list<PubSubPattern> pubsub_patterns;
    std::mutex pubsub_mutex;

    ServerContext() 
        : readSlabManager(1024), 
          writeSlabManager(1024), 
          aof("appendonly.aof.-1.base.aof") 
    {
      
    }

    // Clean up rings on shutdown
    ~ServerContext() {
        io_uring_queue_exit(&read_ring);
        io_uring_queue_exit(&write_ring);
    }
};

extern ServerContext ctx; 

#endif