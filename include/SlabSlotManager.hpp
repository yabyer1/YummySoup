#ifndef SLAB_SLOT_MANAGER_HPP
#define SLAB_SLOT_MANAGER_HPP

#include <vector>
#include <stack>
#include <mutex>
#include <stdexcept>

class SlabSlotManager {
private:
    std::stack<int> free_slots;
    std::mutex mtx;

public:
    SlabSlotManager(int count) {
        for (int i = 0; i < count; ++i) {
            free_slots.push(i);
        }
    }

    int pick_slot() {
        std::lock_guard<std::mutex> lock(mtx);
        if (free_slots.empty()) {
            return -1; // Out of memory slots!
        }
        int slot = free_slots.top();
        free_slots.pop();
        return slot;
    }

    void release_slot(int slot) {
        std::lock_guard<std::mutex> lock(mtx);
        free_slots.push(slot);
    }
};

#endif