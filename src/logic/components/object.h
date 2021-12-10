#ifndef OBJECT_H
#define OBJECT_H

namespace dy {

class Object {
public:
    enum Type {
        plant,
        being
    };

    enum Identifier {
        tree,
        berry_bush,
        human,
        bunny,
    };

    Type type;
    Identifier id;

    Object(Type type, Identifier id) : type(type), id(id) {

    }

};

}

#endif
