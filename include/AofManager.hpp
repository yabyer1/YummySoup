#ifndef AOF_MANAGER_HPP
#define AOF_MANAGER_HPP
#include <string>
#include <vector>
#include <mutex>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "ProtocolHandler.hpp"
#include "Storage.hpp"
#define AOF_BUF_SIZE 65536 
class AofManager {
    private:
    int aof_fd;
    struct file_buffer{
        char data[AOF_BUF_SIZE];
        int buf_ptr = 0;
    };
    file_buffer * aobuffer; //pointer to the filebuffer
    file_buffer * flushbuffer;
    std::mutex aofMutex;
    std::string filename;
    public:
    AofManager(const std::string& path) : filename(path) {
            aof_fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644); //oappend ensures the kernel handels the write offset for us and we dont have to worry about multiple threads writing at the same time. the kernel will serialize the writes for us. this is much more efficient than locking around the file and managing offsets ourselves in user space.
            if(aof_fd == -1) {
                perror("Failed to open AOF file");
            }

    }
    ~AofManager() {
        if (aof_fd >= 0) {close(aof_fd); // if the file descriptor is valid, close it when the AofManager is destroyed to free system resources. This ensures that we do not leak file descriptors and that the AOF file is properly closed when the server shuts down or when the AofManager instance goes out of scope. By managing the file descriptor in this way, we can ensure safe and efficient handling of AOF persistence in our server architecture.
            
            delete aobuffer;
            delete flushbuffer;
        }
    }
    void log(const std::vector<std::string_view>& tokens) {
        std::string serialized = ProtocolHandler::to_resp(tokens); //this will serialize the tokens into a format that the log deems ideal 
        std::lock_guard<std::mutex> lock(aofMutex); // lock mutex so only one thread writes to this aof file
      if(serialized.size() > AOF_BUF_SIZE) {
            // If the command is larger than the buffer, write it directly
            write(aof_fd, serialized.c_str(), serialized.size());
            return;
        }
        if (aobuffer->buf_ptr + serialized.size() > AOF_BUF_SIZE) {
            // If the new command doesn't fit, flush the buffer first
          write(aof_fd, aobuffer->data, aobuffer->buf_ptr); // Write the contents of the primary buffer to disk. This system call will block until the data is written to the AOF file, ensuring that our persistence mechanism is reliable and that commands are not lost in case of a crash. By writing the buffer to disk, we can maintain a durable log of all commands executed by the server, allowing for recovery and replication in our architecture. After the write operation completes, we can safely reset the primary buffer and allow it to continue accepting new commands without interruption.
            aobuffer->buf_ptr = 0; // Reset the primary buffer after flushing to disk. This allows us to reuse the same buffer for future commands without needing to allocate a new one, improving memory efficiency and reducing overhead in our AOF management. By resetting the buffer, we can ensure that subsequent log calls will write to the correct position in the buffer, maintaining the integrity of our AOF persistence mechanism.
        }
        // Copy the serialized command into the buffer
        memcpy(aobuffer -> data + aobuffer -> buf_ptr, serialized.data(), serialized.size());
        aobuffer -> buf_ptr += serialized.size();

    }
    void flushToDisk(){
       file_buffer* tempbuffer = nullptr; //local pointer to the buffer to flush
        
        {
        std::lock_guard<std::mutex> lock(aofMutex); // Lock the mutex to safely access the buffer and file descriptor. This ensures that we do not have concurrent writes to the AOF file while flushing, preventing data corruption and ensuring thread safety. By locking around the flush operation, we can guarantee that all buffered commands are written to disk atomically, maintaining the integrity of our AOF persistence mechanism.
        if(aobuffer -> buf_ptr == 0) return; // If there is nothing to flush, return early to avoid unnecessary system calls and locking. This optimization allows us to skip the flush operation when there are no pending commands in the buffer, improving performance by reducing overhead in cases where flushes are called frequently but there are no new commands to write.
        tempbuffer = aobuffer; // Reset the buffer pointer to indicate that the buffer is now empty and ready for new commands. This allows us to reuse the same buffer for future commands without needing to allocate a new one, improving memory efficiency and reducing overhead in our AOF management. By resetting the buffer pointer, we can ensure that subsequent log calls will write to the correct position in the buffer, maintaining the integrity of our AOF persistence mechanism.
        aobuffer = flushbuffer;   
        flushbuffer =tempbuffer; // Swap the buffer pointers so that the AOF manager can continue accepting new commands while we flush the current buffer to disk. This double-buffering technique allows us to achieve better performance by minimizing the time spent holding the mutex lock, enabling concurrent logging and flushing without blocking each other. By swapping the buffers, we can ensure that the AOF manager remains responsive to new commands while we handle the I/O operation of writing to disk.
    }
    if(!tempbuffer) return; // If the temp buffer is null, return early to avoid dereferencing a null pointer. This is a safety check to ensure that we do not attempt to flush an empty buffer or encounter unexpected states in our AOF management. By checking for a null temp buffer, we can prevent potential crashes and maintain the stability of our server architecture.
    ssize_t n = write(aof_fd, tempbuffer -> data, tempbuffer -> buf_ptr); // Write the contents of the buffer to disk. This system call will block until the data is written to the AOF file, ensuring that our persistence mechanism is reliable and that commands are not lost in case of a crash. By writing the buffer to disk, we can maintain a durable log of all commands executed by the server, allowing for recovery and replication in our architecture. After the write operation completes, we can safely release the local buffer and allow the AOF manager to continue accepting new commands without interruption.
    if(n == -1) {
        perror("Failed to flush AOF buffer to disk");
    }
    fdatasync(aof_fd); // Ensure that the data is flushed to disk. This system call will block until the data is physically written to the storage device, providing an additional layer of durability for our AOF persistence mechanism. By calling fdatasync after writing the buffer, we can guarantee that all logged commands are safely stored on disk, allowing for reliable recovery in case of a server crash. This is especially important in scenarios where data integrity is critical, as it ensures that we do not lose any commands that have been logged but not yet flushed to disk.
    tempbuffer->buf_ptr = 0; // Reset the buffer pointer after flushing to disk. This allows us to reuse the same buffer for future commands without needing to allocate a new one, improving memory efficiency and reducing overhead in our AOF management. By resetting the buffer pointer, we can ensure that subsequent log calls will write to the correct position in the buffer, maintaining the integrity of our AOF persistence mechanism.

}
void trigger_aof_rewrite(Storage& current_db){
    pid_t pid = fork();
    if(pid < 0){
        perror("Failed to fork for AOF rewrite");
        return;
    }
    if(pid == 0){

            //child
          int temp_fd = open("temp_base.aof", O_WRONLY | O_CREAT | O_TRUNC, 0644);
          current_db.serializeToAof(temp_fd); //have the child serialize to the temp file 
        close(temp_fd);
        _exit(0);
    }
    else{
        std::cout<<"Background rewrite with pid: "<<pid<<std::endl;
    }
}

bool should_trigger_rewrite_aof() {
    struct stat st;
    if (stat("appendonly.aof", &st) == 0) {
        // Trigger if file exceeds 100MB
        // 100 * 1024 * 1024 = 104857600 bytes
        return st.st_size > 104857600; 
    }
    return false;
}
pid_t rewrite_child_pid = -1;
void check_rewrite_status(){
    if(rewrite_child_pid == -1)return; // No rewrite in progress
    int status ;
    pid_t result = waitpid(rewrite_child_pid, &status, WNOHANG); //wait no hanf tells the kernel to check if the child is a zombie
    //if the child is a zombie give me its info and clean up the child process, else return.
    if(result == rewrite_child_pid){
        if(WIFEXITED(status) && WEXITSTATUS(status) == 0){
            // Rewrite successful, replace old AOF with new one
            if(rename("temp_base.aof", "appendonly.aof") != 0){
                perror("Failed to replace old AOF with new AOF");
            }
            else{
                std::cout<<"AOF rewrite completed successfully."<<std::endl;
            }
        }
        else{
            std::cerr<<"AOF rewrite failed in child process."<<std::endl;
        }
        rewrite_child_pid = -1; // Reset the rewrite child PID
    }
}
};



#endif