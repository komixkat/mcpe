#pragma once
#include <string>

struct Conf {
    static int zoomKey;
    static bool animated;

    // Sensitivity when zoomed is the exact screen-space ratio multiplied by
    // sensitivityMultiplier, never dropping below sensitivityFloor so deep
    // zoom stays usable. Both are tunable in zoom.conf.
    static float sensitivityMultiplier;
    static float sensitivityFloor;
    // If true, skip sensitivity dampening entirely (raw mouse input when zoomed).
    // Can help with "rigid" camera movement at extreme zoom levels.
    static bool disableSensitivityDampening;

    static bool changingKey;

    static std::string getPath();
    static void load();
    static void save();
};
