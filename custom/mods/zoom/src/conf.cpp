#include "conf.h"
#include "key_mapping.h"
#include <properties/property_list.h>
#include <properties/property.h>
#include <string>
#include <fstream>

int Conf::zoomKey = 'C';
bool Conf::animated = true;
float Conf::sensitivityMultiplier = 2.0f;
float Conf::sensitivityFloor = 0.15f;
bool Conf::disableSensitivityDampening = false;

bool Conf::changingKey = false;

static properties::property_list conf('=');
static properties::property<int> zoomKey(conf, "zoomKey", 'C');
static properties::property<bool> animated(conf, "animated", true);
static properties::property<float> sensitivityMultiplier(conf, "sensitivityMultiplier", 2.0f);
static properties::property<float> sensitivityFloor(conf, "sensitivityFloor", 0.15f);
static properties::property<bool> disableSensitivityDampening(conf, "disableSensitivityDampening", false);

std::string Conf::getPath() {
    return "/data/data/com.mojang.minecraftpe/zoom.conf";
}

void Conf::load() {
    std::ifstream propertiesFile(getPath());
    if(propertiesFile) {
        conf.load(propertiesFile);
    }
    Conf::zoomKey = ::zoomKey.get();
    Conf::animated = ::animated.get();
    Conf::sensitivityMultiplier = ::sensitivityMultiplier.get();
    Conf::sensitivityFloor = ::sensitivityFloor.get();
    Conf::disableSensitivityDampening = ::disableSensitivityDampening.get();
}

void Conf::save() {
    ::zoomKey.set(Conf::zoomKey);
    ::animated.set(Conf::animated);
    ::sensitivityMultiplier.set(Conf::sensitivityMultiplier);
    ::sensitivityFloor.set(Conf::sensitivityFloor);
    ::disableSensitivityDampening.set(Conf::disableSensitivityDampening);

    std::ofstream propertiesFile(getPath());
    if(propertiesFile) {
        conf.save(propertiesFile);
    }
}
