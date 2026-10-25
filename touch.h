#pragma once
void TouchStart();                                   // starts the /dev/input reader thread
bool TouchGet(float W, float H, float& x, float& y); // returns true while a finger is down
