#include "jolt_runtime.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>

#include "util/log.h"

using namespace dy;

namespace {

std::atomic<int64_t> live_allocations { 0 };
bool runtime_active = false;

void* allocate(size_t size) {
    live_allocations++;
    return malloc(size);
}

void* reallocate(void* block, size_t old_size, size_t new_size) {
    if(!block) live_allocations++;
    return realloc(block, new_size);
}

void freeBlock(void* block) {
    if(block) live_allocations--;
    free(block);
}

void* alignedAllocate(size_t size, size_t alignment) {
    live_allocations++;
    return _aligned_malloc(size, alignment);
}

void alignedFree(void* block) {
    if(block) live_allocations--;
    _aligned_free(block);
}

void joltTrace(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    dy::log(dy::info) << "[Jolt] " << buffer << "\n";
}

#ifdef JPH_ENABLE_ASSERTS
bool joltAssert(const char* expression, const char* message, const char* file, JPH::uint line) {
    dy::log(dy::error) << "[Jolt] assert " << expression << " " << (message ? message : "") << " at " << file << ":" << line << "\n";
    return true; // break into the debugger
}
#endif

}

JoltRuntime::JoltRuntime() {

    // Jolt's default allocator, counted
    JPH::Allocate = allocate;
    JPH::Reallocate = reallocate;
    JPH::Free = freeBlock;
    JPH::AlignedAllocate = alignedAllocate;
    JPH::AlignedFree = alignedFree;

    JPH::Trace = joltTrace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = joltAssert;)

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    runtime_active = true;

}

JoltRuntime::~JoltRuntime() {

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    runtime_active = false;

}

bool JoltRuntime::active() {
    return runtime_active;
}

int64_t JoltRuntime::liveAllocations() {
    return live_allocations.load();
}
