#include "profile.h"

#ifdef TRACY_ENABLE
#include <common/TracySystem.hpp>
#endif

namespace dy::profile {

#ifdef TRACY_ENABLE

std::atomic<bool> started { false };

bool start() {
    if(active()) return true;
    // TRACY_MANUAL_LIFETIME : Tracy does not exist before this
    tracy::StartupProfiler();
    started.store(true);
    threadName("Main");
    return true;
}

void stop() {
    if(!started.exchange(false)) return;
    tracy::ShutdownProfiler();
}

void threadName(const char* name) {
    if(active()) tracy::SetThreadName(name);
}

void message(const char* text) {
    if(active()) TracyMessage(text, std::strlen(text));
}

#else

bool start() { return false; }
void stop() {}
void threadName(const char*) {}
void message(const char*) {}

#endif

}
