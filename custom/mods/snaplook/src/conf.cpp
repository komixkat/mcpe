#include "conf.h"
#include "key_mapping.h"
#include <properties/property_list.h>
#include <properties/property.h>
#include <string>
#include <fstream>

int Conf::snaplookKey = 'X';

bool Conf::changingKey = false;

static properties::property_list conf('=');
static properties::property<int> snaplookKey(conf, "snaplookKey", 'X');

std::string Conf::getPath() {
    return "/data/data/com.mojang.minecraftpe/snaplook.conf";
}

void Conf::load() {
    std::ifstream propertiesFile(getPath());
    if(propertiesFile) {
        conf.load(propertiesFile);
    }
    Conf::snaplookKey = ::snaplookKey.get();
}

void Conf::save() {
    ::snaplookKey.set(Conf::snaplookKey);

    std::ofstream propertiesFile(getPath());
    if(propertiesFile) {
        conf.save(propertiesFile);
    }
}
