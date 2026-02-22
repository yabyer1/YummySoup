#include <iostream>
#include <string>
#include <unordered_map>
#include <queue>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <liburing.h>
#include <sys/mman.h>
#include "/home/ubuntu/try/include/all.hpp"

#define MAX_EVENTS 64 // Maximum number of events to process at once in epoll_wait
#define THREAD_POOL_SIZE 4 // Number of worker threads in the pool
#define ENTRIES 1024 // Number of entries in the io_uring submission and completion queues
#define BUF_SIZE 4096 // Size of each buffer for zero-copy reads (must be <= page size for optimal performance)
ServerContext ctx; // Global server context containing shared resources like the slab manager and storage
struct io_uring ring;  // Global io_uring instance for all workers
void* shared_buffer_slab;  // Pre-allocated slab for zero-copy reads
struct iovec iov[1024]; // iovec array for registered buffers
int global_epoll_fd;// Global epoll instance for accepting connections and monitoring clients
std::unordered_map<int, std::shared_ptr<Client>> clients; // Map of client file descriptors to Client objects
std::mutex clientsMapMutex; // Mutex to protect access to the clients map
std::queue<int> taskQueue; // Queue of client file descriptors ready for processing
std::mutex queueMutex; // Mutex to protect access to the task queue
std::condition_variable condition; // Condition variable to notify worker threads of new tasks

// Global Submission Mutex (io_uring SQ is not thread-safe by default
std::mutex ringMutex;

void set_nonblocking(int fd) { // Set a file descriptor to non-blocking mode
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
void cleanupClientPubSub(Client* c, ServerContext& ctx) {
    std::lock_guard<std::mutex> lock(ctx.pubsub_mutex);

    // 1. Remove from exact channels using the client's local set
    for (const auto& channel : c->subscribed_channels) {
        auto it = ctx.pubsub_channels.find(channel);
        if (it != ctx.pubsub_channels.end()) {
            it->second.remove(c); 
            if (it->second.empty()) {
                ctx.pubsub_channels.erase(it);
            }
        }
    }
    c->subscribed_channels.clear();

    // 2. Remove from global patterns
    auto it = ctx.pubsub_patterns.begin();
    while (it != ctx.pubsub_patterns.end()) {
        if (it->client == c) {
            it = ctx.pubsub_patterns.erase(it);
        } else {
            ++it;
        }
    }
}
void handleDisconnect(Client* c) {
    // 1. Remove from Pub/Sub (O(N) patterns, O(1) channels)
    cleanupClientPubSub(c, ctx);
}

// --- Worker Logic ---
void workerThread() {
    CommandExecutor executor;
    struct io_uring local_ring;
    io_uring_queue_init(ENTRIES, &local_ring, 0); // Each worker initializes its own io_uring instance. In a more advanced implementation, we could share the ring across threads with proper synchronization, but for simplicity, we initialize it here. This allows each worker to independently submit and wait for I/O operations without contention on a shared ring. By using separate rings, we can avoid the need for complex locking around the submission and completion queues, improving performance and scalability in our server architecture.
    io_uring_register_buffers(&local_ring, iov, 1024); //register global slab to be used for all local rings
    while (true) { //while we need to have a client to process, we wait on the condition variable for a new task to be added to the queue. This allows worker threads to efficiently sleep when there are no tasks and wake up immediately when there are new client file descriptors to process. By using a condition variable, we can avoid busy-waiting and reduce CPU usage while still ensuring that worker threads are responsive to incoming tasks.
        std::shared_ptr<Client> client;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            condition.wait(lock, [] { return !taskQueue.empty(); });
            int fd = taskQueue.front();
            taskQueue.pop();
            
            std::lock_guard<std::mutex> mapLock(clientsMapMutex); //lock the clients map to safely access the client object associated with the file descriptor.
            if (clients.count(fd)) client = clients[fd];
        }

        if (!client) continue;
        bool close_connection = false;

        // 1. Prepare and Submit Fixed Read
        {
         
            struct io_uring_sqe *sqe = io_uring_get_sqe(&local_ring); // Get a submission queue entry for this I/O operation. This allows us to prepare a read operation that will read data directly into the client's assigned buffer slot in the slab. By using io_uring, we can achieve zero-copy reads, improving performance by avoiding unnecessary data copying between kernel and user space. The sqe will be tagged with the client pointer for easy identification when the operation completes, allowing us to efficiently process the incoming data.
           
            if (sqe) {
                // DMA directly into the client's assigned slot in the slab and submit it to local ring for reading 
               io_uring_prep_read_fixed(sqe, client->fd, client ->slab_ptr + client -> buffer_index, BUF_SIZE - client -> buffer_index, 0, client->ring_index);
                io_uring_submit_and_wait(&local_ring, 1);
            }
        }

        // 2. Wait for Completion
        struct io_uring_cqe *cqe;
        // In a true high-perf system, we'd use a separate completion thread, 
        // but for this architecture, we wait for the specific event we submitted.
        int ret = io_uring_wait_cqe(&local_ring, &cqe);
        
        if (ret < 0 || cqe->res <= 0) {
            close_connection = (ret < 0 || cqe->res == 0);
            handleDisconnect(client.get());
            if (cqe) io_uring_cqe_seen(&local_ring, cqe);
        } else {
   
            // 3. Zero-Copy Processing: Append from Slab to InputBuffer
           client-> buffer_index += cqe ->res;
             while(auto tokens = ProtocolHandler::parse(client -> slab_ptr, client -> buffer_index)) { // we are parsing input buffer line by line \n delimited. we then split into space delimited tokens on the line and execute that line
                executor.execute(client, *tokens, ctx); 
            }
            io_uring_cqe_seen(&local_ring, cqe);

          

            // 4. Re-arm Epoll one shot for next event on this client
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
            ev.data.fd = client->fd;
            epoll_ctl(global_epoll_fd, EPOLL_CTL_MOD, client->fd, &ev);
        }

        if (close_connection) {
            std::cout << "Closing connection for fd: " << client->fd << std::endl;
            ctx.slabManager.release_slot(client->ring_index); // Release the client's buffer slot back to the slab maanger
            std::lock_guard<std::mutex> mapLock(clientsMapMutex);
            clients.erase(client->fd);
        }
    }
}

int main() {
    // 1. Initialize io_uring with Shared Workers
    struct io_uring_params params; // Zero out the params struct before use
    memset(&params, 0, sizeof(params));   
    // SQPOLL would be faster here but requires sudo; we use standard for now
    io_uring_queue_init_params(ENTRIES, &ring, &params); // Initialize io_uring with specified entries and parameters

    // 2. Setup Registered Buffer Slab
    shared_buffer_slab = mmap(NULL, 1024 * BUF_SIZE, PROT_READ | PROT_WRITE, 
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0); // Allocate a large slab of memory for zero-copy reads. NULL means that the kernel chooses best location, MAP_POPULATE pre-faults the pages to avoid page faults during I/O. The size is 1024 buffers of BUF_SIZE each, which matches our iovec array. MAP_ANONUMOUS means it's not backed by any file, and MAP_PRIVATE means changes are not visible to other processes. PROT_READ | PROT_WRITE allows both reading and writing to this memory.
    //the first 1024 buffers of size BUF_SIZE each, and register them with io_uring
    
    for (int i = 0; i < 1024; i++) {
        iov[i].iov_base = (char*)shared_buffer_slab + (i * BUF_SIZE); // OFF-SET INTO THE BIG SLAB FOR EACH BUFFER. Each buffer is BUF_SIZE bytes, so we calculate the base address for each buffer by adding i * BUF_SIZE to the starting address of the slab. This way, each client can be assigned a unique buffer slot without overlap.
        iov[i].iov_len = BUF_SIZE; //this base and len are now registered with io_uring for zero-copy operations. When we prepare a read, we specify the buffer index (ring_index) which corresponds to one of these iovec entries, allowing us to read directly into the slab without intermediate copying.
    }
    io_uring_register_buffers(&ring, iov, 1024); // Register the array of iovec buffers with io_uring. This tells io_uring about our pre-allocated buffers so that we can use them for zero-copy reads. The number of buffers registered is 1024, which matches the size of our iovec array and the slab we allocated. Each buffer can be used by a different client, allowing for efficient concurrent I/O without copying data into user-space buffers.

    // 

    // 3. Socket Setup
    int listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
    int val = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    int no = 0;
    setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no));
 //AF_INET6 allows for both IPv4 and IPv6 connections when IPV6_V6ONLY is set to 0. This means the server can accept connections from both types of clients on the same socket, simplifying deployment and increasing compatibility. The SO_REUSEADDR option allows the server to quickly restart and bind to the same port without waiting for the OS to release it, which is useful during development and in production environments where downtime should be minimized.
    struct sockaddr_in6 addr = {}; //Create an IPv6 socket address structure and zero it out for safety. This structure will be used to bind the listening socket to a specific port and address. By using sockaddr_in6, we can support both IPv4 and IPv6 clients when IPV6_V6ONLY is set to 0, allowing for greater flexibility in client connections. The address is set to in6addr_any, which means the server will listen on all available network interfaces, making it accessible from any IP address assigned to the machine.
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(1234); // htons converts the port number to network byte order, which is required for socket operations. This ensures that the server listens on port 1234 for incoming connections. By using a well-known port, clients can easily connect to the server without needing to specify a custom port number. The use of AF_INET6 allows for both IPv4 and IPv6 clients to connect when IPV6_V6ONLY is set to 0, making the server more versatile and accessible.
    addr.sin6_addr = in6addr_any;

    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)); //we bind the server socket to the specified address and port. This step is crucial for the server to receive incoming connection requests on the desired port (1234 in this case). By binding to in6addr_any, the server will accept connections on all available network interfaces, allowing clients from any IP address to connect. The use of AF_INET6 allows for both IPv4 and IPv6 clients to connect when IPV6_V6ONLY is set to 0, making the server more versatile and accessible.
    listen(listen_fd, SOMAXCONN); // Start listening for incoming connections with a backlog of SOMAXCONN, which allows the maximum number of pending connections as defined by the system. This ensures that the server can handle a large number of simultaneous connection attempts without rejecting them due to a full backlog. By using AF_INET6 and setting IPV6_V6ONLY to 0, the server can accept both IPv4 and IPv6 clients on the same socket, increasing compatibility and ease of deployment.
    set_nonblocking(listen_fd);//` Set the listening socket to non-blocking mode so that accept calls do not block the main thread. This allows the server to efficiently handle multiple incoming connections without getting stuck waiting for a single accept call to complete. By using non-blocking sockets, we can integrate the accept logic into our event-driven architecture with epoll, allowing for better scalability and responsiveness under high load.

    global_epoll_fd = epoll_create1(0); // Create an epoll instance for monitoring the listening socket and client sockets. This allows us to efficiently wait for events on multiple file descriptors without blocking. By using epoll, we can scale to a large number of concurrent connections while maintaining high performance. The epoll instance will be used to monitor the listening socket for new connection events and client sockets for incoming data, enabling our event-driven architecture.
    struct epoll_event ev, events[MAX_EVENTS]; // Create an epoll_event structure for configuring the events we want to monitor and an array to hold the events returned by epoll_wait. The ev structure will be used to specify the events we are interested in (e.g., EPOLLIN for incoming data) and the file descriptor associated with those events. The events array will store the events that are triggered, allowing us to process them in our main event loop. By using epoll, we can efficiently handle a large number of concurrent connections and events without blocking, improving the scalability and responsiveness of our server.
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(global_epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev); // Add the listening socket to the epoll instance so that we can monitor it for incoming connection events. This allows us to efficiently wait for new connections without blocking, enabling our event-driven architecture. By using epoll, we can scale to a large number of concurrent connections while maintaining high performance. The listening socket will trigger an event when a new client attempts to connect, allowing us to accept the connection and add the new client socket to the epoll instance for further monitoring.

    for (int i = 0; i < THREAD_POOL_SIZE; ++i) {
        std::thread([&](){
            struct io_uring local_ring;
            struct io_uring_params local_params;
            memset(&local_params, 0, sizeof(local_params));
            local_params.wq_fd = ring.ring_fd; //use parent ring kernel threads (avoid creating speerate peR thread)
            local_params.flags = IORING_SETUP_ATTACH_WQ;
            io_uring_queue_init_params(ENTRIES, &local_ring, &local_params);
   
            
        }).detach();
    }

    std::cout << "3FS-Style io_uring Server Online. Port 1234." << std::endl;

    while (true) {
        int nfds = epoll_wait(global_epoll_fd, events, MAX_EVENTS, 100); // Wait for events on the epoll instance. This call will block until at least one event occurs on the monitored file descriptors (e.g., new connection on the listening socket or incoming data on client sockets). The events will be stored in the events array, and nfds will indicate how many events were triggered. By using epoll_wait, we can efficiently handle a large number of concurrent connections and events without blocking, improving the scalability and responsiveness of our server. we use max events as a limit to how many processed at a time. 
       //100 is last param  -> 10 cron beats per second
       if(nfds == 0){
        ctx.aof.flushToDisk();
        ctx.aof.check_rewrite_status(); //rewrite in progress will be completed by the child process, we just need to check on the status and when its done we can replace the old aof with the new one. This allows us to perform AOF rewrites in the background without blocking the main server thread, ensuring that our persistence mechanism does not impact the responsiveness of the server. By periodically checking the rewrite status, we can seamlessly transition to the new AOF file once the rewrite is complete, maintaining data integrity and durability in our server architecture.

        if(ctx.aof.rewrite_child_pid == -1 && ctx.aof.should_trigger_rewrite_aof()){ //trigger if rewrite is needed and one isnt runnign
            ctx.aof.trigger_aof_rewrite(ctx.db);
        }
        continue;
       }
       
       
       for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == listen_fd) { // if listen_fd == events[i].data.fd, it means we have a new incoming connection to accept. We will handle this in the main thread to quickly accept the connection and add it to the epoll instance for monitoring. By accepting connections in the main thread, we can ensure that we do not block worker threads with accept calls, allowing them to focus on processing client requests. Once a new connection is accepted, we will set it to non-blocking mode, assign it a buffer slot from the slab, and add it to the clients map and epoll instance for further monitoring.
                while (true) { //we may have more than one incoming connection, so we loop to accept all pending connections until there are no more (accept returns -1 with EAGAIN or EWOULDBLOCK). This allows us to efficiently handle bursts of incoming connections without missing any. By using non-blocking sockets
                    int conn_fd = accept(listen_fd, NULL, NULL);
                    if (conn_fd == -1) break;
                    
                    set_nonblocking(conn_fd);
                    int opt = 1;
                    setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

                    auto new_client = std::make_shared<Client>(conn_fd); //make client a shared pointer to manage its lifetime across threads. This allows us to safely share the client object between the main thread (which accepts connections) and worker threads (which process client requests) without worrying about manual memory management or dangling pointers. By using std::shared_ptr, we can ensure that the client object is automatically deleted when it is no longer needed, preventing memory leaks and ensuring safe access across threads.
                    // Assign a unique buffer slot from the slab
                    int assigned_slot = ctx.slabManager.pick_slot(); // Pick a free slot from the slab slot manager for this new client. This allows us to efficiently manage the allocation of buffer slots for clients, ensuring that we can reuse slots when clients disconnect and freeing up resources for new connections. By using a slab allocator, we can minimize fragmentation and improve performance when handling a large number of concurrent clients, as each client can be assigned a fixed-size buffer slot from the pre-allocated slab.
                    
                    if (assigned_slot == -1) {
                            // Log the error and drop the connection or close the socket
                            fprintf(stderr, "Error: No free buffer slots available for new client\n");
                            close(new_client->fd); 
                            continue;
                        }
                    new_client -> slab_ptr = (char*)iov[assigned_slot].iov_base;
                    if(assigned_slot == -1) {
                        std::cerr << "No free buffer slots available for new client!" << std::endl;
                        close(conn_fd);
                        continue;
                    }

                    new_client->ring_index = assigned_slot;  //find its index in the slab based on its file descriptor. This simple modulo operation allows us to assign each client a unique buffer slot in the pre-allocated slab without needing complex tracking of free slots. By using the file descriptor as the basis for the index, we can ensure that each client gets a consistent buffer slot across its lifetime, allowing for efficient zero-copy reads directly into the slab.

                    {
                        std::lock_guard<std::mutex> lock(clientsMapMutex);
                        clients[conn_fd] = new_client; //set on map
                    }

                    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    ev.data.fd = conn_fd;
                    epoll_ctl(global_epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev);// Add the new client socket to the epoll instance
                }
            } else {
                std::lock_guard<std::mutex> lock(queueMutex); //else push ewvent to queue for wokrer threads to process. This allows us to efficiently hand off client sockets that have incoming data to worker threads for processing without blocking the main thread. By using a task queue and condition variable, we can ensure that worker threads are notified when there are new tasks to process, allowing for efficient handling of client requests while maintaining responsiveness in the main thread.
                taskQueue.push(events[i].data.fd);
                condition.notify_one();
            }
        }
    }
    return 0;
}