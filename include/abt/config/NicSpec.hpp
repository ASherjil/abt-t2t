#pragma once

#include <string>

namespace abt {

struct NicSpec {
    std::string interface = "eth0";
    std::string driver    = "";
    int         cpuCore   = -1;
};

}
