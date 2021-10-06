#pragma once

#include <string>

namespace tiny {

// Used to store a line number and filename where an entity originated from
struct Pos {
    int line = 0;
    std::string filename;
};

}  // namespace tiny
