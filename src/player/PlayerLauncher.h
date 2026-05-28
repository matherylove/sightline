#pragma once
#include <windows.h>
#include <string>

namespace PlayerLauncher {
    // Launch MPC-HC with the given stream URL.
    // hParent: HWND of the main window (used for /slave mode and dialogs)
    // Returns true if MPC-HC was launched successfully.
    bool Launch(HWND hParent, const std::string& streamUrl);

    // Send a WM_COMMAND to the running MPC-HC window (play/pause/stop etc.)
    // Standard MPC-HC command IDs: 887=Play, 888=Pause, 890=Stop
    bool SendCommand(int cmdId);
}
