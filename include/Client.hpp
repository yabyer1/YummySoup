#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <vector>
#include <mutex>
#include <unistd.h> 
#include <cstring>
#include <string> 
#include <unordered_set>
#include <atomic>    // <--- Add this line!
enum class ClientState { NORMAL, SUBSCRIBED, REPLICA };

class Client {
public:
    int fd;
    int buffer_index = 0; 
    int ring_index = -1;    
    int buf_idx = 0; 
    char *  write_slab_ptr    = nullptr;  
    int write_buf_index = 0;
    char * slab_ptr = nullptr;
    std::mutex clientMutex;
    std::unordered_set<std::string> subscribed_channels; // For Pub/Sub, track which channels this client is subscribed to
    std::unordered_set<std::string> subscribed_patterns; // For Pub/Sub, track which patterns this client is subscribed to
    std::vector<std::string> response_buffer;
    std::atomic<bool> has_pending_write{false};
    Client(int client_fd) : fd(client_fd) {}
    ~Client() { if (fd >= 0) close(fd);
    }

    void resetBuffer(){
        std::lock_guard<std::mutex> lock(clientMutex);
        buffer_index = 0;
    }
};

#endif