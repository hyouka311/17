#pragma once
// Shared between the menu (main.cpp) and the logic (cheat.cpp). Everything is OFF by default.
struct Settings {
    bool esp = false;        // WH master switch
    bool espBox = true;
    bool espLine = true;
    bool skipTeam = true;    // don't draw / aim at teammates
    bool aim = false;
    float aimFov = 250.f;    // pixels from screen centre
    float aimGain = 0.02f;   // look-input units per pixel of error
    float aimMaxStep = 3.f;  // max look-input added per call
    bool invX = false;       // flip if the camera turns AWAY from the target
    bool invY = false;
    int  lookSel = 0;        // which look-input hook is steered (0 = A, 1 = B)
};
extern Settings S;
