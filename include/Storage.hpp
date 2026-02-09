#ifndef STORAGE_HPP
#define STORAGE_HPP
#include <unordered_map>
#include <shared_mutex>
#include <string>
#include <mutex>
class Storage {
private:
    std::unordered_map<std::string, std::string> kv_store; //key value store for now
    mutable std::shared_mutex store_mutex;

public:
    void set(const std::string& key, const std::string& value) {
        std::unique_lock lock(store_mutex); // Writer lock (exclusive)
        kv_store[key] = value;
    }

    std::string get(const std::string& key) {
        std::shared_lock lock(store_mutex); // Reader lock (shared)
        auto it = kv_store.find(key);
        return (it != kv_store.end()) ? it->second : "(nil)";
    }
    void del(const std::string& key) {
        std::unique_lock lock(store_mutex);
        kv_store.erase(key);
    }
    void serializeToAof(int fd) {
        std::shared_lock lock(store_mutex); // Lock the store for reading during serialization
        for (const auto& [key, value] : kv_store) {
            std::vector<std::string_view> tokens = {"SET", key, value};
            std::string serialized = ProtocolHandler::to_resp(tokens);
            write(fd, serialized.c_str(), serialized.size());
        }
    }
};
#endif