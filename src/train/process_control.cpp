#include "process_control.h"

#include <atomic>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace dy;

namespace {

std::atomic<bool> stop_flag { false };
std::atomic<const char*> stop_name { "" };

#ifdef _WIN32
BOOL WINAPI handler(DWORD event) {
    switch(event) {
    case CTRL_C_EVENT: requestStop("CTRL_C"); return TRUE;
    case CTRL_BREAK_EVENT: requestStop("CTRL_BREAK"); return TRUE;
    case CTRL_CLOSE_EVENT:
        requestStop("CTRL_CLOSE");
        // The process ends when this returns: give the main loop time to log and exit 0
        Sleep(3000);
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

}

void dy::installStopHandler() {
#ifdef _WIN32
    SetConsoleCtrlHandler(handler, TRUE);
#endif
}

bool dy::stopRequested() {
    return stop_flag.load();
}

const char* dy::stopSignal() {
    return stop_name.load();
}

void dy::requestStop(const char* signal) {
    stop_name.store(signal);
    stop_flag.store(true);
}

void dy::clearStop() {
    stop_flag.store(false);
    stop_name.store("");
}
