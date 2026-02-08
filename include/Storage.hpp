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
};
#endif