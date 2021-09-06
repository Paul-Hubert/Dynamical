#ifndef PLANT_H
#define PLANT_H

namespace dy {

class Plant {
public:
    enum Type : int {
        tree,
        berry_bush
    };
    Plant(Type type) : type(type) {
        
    }
    Type type;
    
};

}

#endif
