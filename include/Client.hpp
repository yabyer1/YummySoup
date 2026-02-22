#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <vector>
#include <mutex>
#include <unistd.h> 
#include <cstring>
#include <string> 
#include <unordered_set>
#include <atomic>    
#include <memory> 
enum class ClientState { NORMAL, SUBSCRIBED, REPLICA };

class Client : public std::enable_shared_from_this<Client> {
public:
    int fd;
    int buffer_index = 0; 
    int ring_index = -1;  
    int ring_index_w = -1;  
    int buf_idx = 0; 
    char *  write_slab_ptr    = nullptr;  
    int write_buf_index = 0;
    char * slab_ptr = nullptr;
    std::mutex clientMutex;
    std::unordered_set<std::string> subscribed_channels; // For Pub/Sub, track which channels this client is subscribed to
    std::unordered_set<std::string> subscribed_patterns; // For Pub/Sub, track which patterns this client is subscribed to
    std::vector<std::string> response_buffer;
    std::atomic<bool> has_pending_write{false};
    std::atomic<bool> is_writing{false};
    std::atomic<bool> is_reading{false};
    Client(int client_fd) : fd(client_fd) {}
    ~Client() { if (fd >= 0) close(fd);
    }

    void resetBuffer(){
        std::lock_guard<std::mutex> lock(clientMutex);
        buffer_index = 0;
    }
    std::shared_ptr<Client> get_ptr(){
        return shared_from_this(); //since im working wiht shared pointers but returning from the uring yields raw pointers i need a way to convert back from a raw pointer to a shared pointer
        
    }
};

#endif