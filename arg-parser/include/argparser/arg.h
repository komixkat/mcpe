#pragma once

#include <cstring>
#include <strings.h>
#include <stdexcept>
#include "arg_parser.h"

namespace argparser {

template <typename T>
class arg {

private:
    T value;

public:
    arg(arg_parser& parser, std::string const& name, std::string const& shortname, std::string const& desc, T def = T())
            : value(def) {
        parser.add_arg(name, shortname, desc, [this](arg_list& list) { handle_value(value, list); });
    }

    T const& get() const {
        return value;
    }

    operator T const&() const {
        return value;
    }

};

inline void handle_value(std::string& value, arg_list& list) {
    value = list.next();
}

inline void handle_value(int& value, arg_list& list) {
    value = std::stoi(list.next());
}

inline void handle_value(float& value, arg_list& list) {
    value = std::stof(list.next());
}

inline void handle_value(bool& value, arg_list& list) {
    const char* v = list.next_value_or_null();
    if (v == nullptr ||
            strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 || strcasecmp(v, "on") == 0 || strcasecmp(v, "yes") == 0)
        value = true;
    else if (strcmp(v, "0") == 0 || strcasecmp(v, "false") == 0 || strcasecmp(v, "off") == 0 || strcasecmp(v, "no") == 0)
        value = false;
    else
        throw std::invalid_argument("Invalid boolean value");
}

template <typename T>
void handle_value(std::vector<T>& value, arg_list& list) {
    handle_value(value.emplace_back(), list);
}

}
