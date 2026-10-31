#ifndef DY_PROCESS_CONTROL_H
#define DY_PROCESS_CONTROL_H

namespace dy {

// stop-run sends CTRL_C to the trainer's console group (mlagents-bridge.md, process behaviour): the handler takes
// CTRL_C, CTRL_BREAK and CTRL_CLOSE, sets a flag and returns TRUE. The loops poll the flag (gRPC waits are cut in
// 1 s slices) and the process exits 0 once it is set, whatever the connection state
void installStopHandler();
bool stopRequested();
const char* stopSignal();       // the event that set the flag, "" when none

// For tests: set or clear the flag as the handler would
void requestStop(const char* signal);
void clearStop();

}

#endif
