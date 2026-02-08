#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <vector>
#include <mutex>
#include <unistd.h> 
#include <cstring>

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