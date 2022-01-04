#ifndef ITEM_H
#define ITEM_H

namespace dy {

class Item {
public:
    enum ID : int {
        null,
        berry
    };
    
    Item(ID id, int amount) : id(id), amount(amount) {}
    ID id;
    int amount = 0;
};

}

#endif

