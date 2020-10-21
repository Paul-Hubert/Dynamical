#ifndef SYSTEM_LIST_H
#define SYSTEM_LIST_H

#include "entt/entt.hpp"

#include "system.h"

class InputSys : public System {
public:
    InputSys(entt::registry& reg) : System(reg) {};
    void preinit() override;
    void init() override;
    void tick() override;
    const char* name() override {return "Input";};
};

class DebugSys : public System {
public:
    DebugSys(entt::registry& reg) : System(reg) {};
    void init() override;
    void tick() override;
    const char* name() override {return "Debug";};
};

class PlayerControlSys : public System {
public:
    PlayerControlSys(entt::registry& reg) : System(reg) {};
    void init() override;
    void tick() override;
    const char* name() override {return "Camera";};
};

class ChunkManagerSys : public System {
public:
    ChunkManagerSys(entt::registry& reg) : System(reg) {};
    void init() override;
    void tick() override;
    const char* name() override {return "ChunkManager";};
};

class ChunkLoaderSys : public System {
public:
    ChunkLoaderSys(entt::registry& reg) : System(reg) {};
    void init() override;
    void tick() override;
    const char* name() override {return "ChunkLoader";};
};

class ChunkGeneratorSys : public System {
public:
    ChunkGeneratorSys(entt::registry& reg) : System(reg) {};
    void init() override;
    void tick() override;
    void finish() override;
    const char* name() override {return "ChunkGenerator";};
};

class ChunkSys : public System {
public:
    ChunkSys(entt::registry& reg) : System(reg) {};
    void init() override;
    void tick() override;
    const char* name() override {return "Chunk";};
};

#endif
