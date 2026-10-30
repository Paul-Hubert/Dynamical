#ifndef STORAGE_H
#define STORAGE_H

#include <entt/entt.hpp>

#include <unordered_map>

#include "item.h"
#include <util/log.h>

namespace dy {

class Storage {
public:
    
    void add(Item item) {
        if(stackable_items.count(item.id) > 0) {
            stackable_items[item.id] += item.amount;
        } else {
            stackable_items[item.id] = item.amount;
        }
    }
    
    void remove(Item item) {
        if(stackable_items.count(item.id) > 0) {
            int& stack = stackable_items[item.id];
            if(stack >= item.amount) {
                stack -= item.amount;
                if(stack == 0) {
                    stackable_items.erase(item.id);
                }
            } else {
                log(error) << "removed too many items\n";
            }
        } else {
            log(error) << "removed non existant item\n";
        }
    }
    
    int amount(Item::ID id) const {
        return stackable_items.count(id) > 0 ? stackable_items.at(id) : 0;
    }
    
private:
    std::unordered_map<Item::ID, int> stackable_items;
};

}

#endif
