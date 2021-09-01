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
    float plant_growth = 0;
    float harvest_growth = 0;
};

}

#endif
