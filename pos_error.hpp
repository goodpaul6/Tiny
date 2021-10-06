#pragma once

#include <exception>
#include <string>

#include "pos.hpp"

namespace tiny {

struct PosError : public std::exception {
    PosError(Pos pos, const std::string& message);

    const char* what() const noexcept override;
    const Pos& pos() const;

private:
    Pos m_pos;
    std::string m_what;
};

}  // namespace tiny
