#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <vector>
#include <mutex>
#include <unistd.h> 
#include <cstring>
#include <unordered_set>
enum class ClientState { NORMAL, SUBSCRIBED, REPLICA };

class Client {
public:
    int fd;
    int buffer_index = 0; 
    int ring_index = 0;    
    int buf_idx = 0;       
    
    alignas(4096) char data[4096]; 
    
    ClientState state = ClientState::NORMAL;
    std::mutex clientMutex;
    std::unordered_set<std::string> subscribed_channels; // For Pub/Sub, track which channels this client is subscribed to
    std::unordered_set<std::string> subscribed_patterns; // For Pub/Sub, track which patterns this client is subscribed to
    Client(int client_fd) : fd(client_fd) {}
    ~Client() { if (fd >= 0) close(fd); }

    void appendToInput(const char* buf, size_t len) {
        std::lock_guard<std::mutex> lock(clientMutex);
        if (buffer_index + len <= sizeof(data)) {
            memcpy(data + buffer_index, buf, len);
            buffer_index += len;
        }
    }
};

#endif