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

    static bool changingKey;

    static std::string getPath();
    static void load();
    static void save();
};
