#pragma once
#include <string>

struct Conf {
    static int snaplookKey;

    static bool changingKey;

    static std::string getPath();
    static void load();
    static void save();
};
