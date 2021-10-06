#include "pos_error.hpp"

namespace tiny {

PosError::PosError(Pos pos, const std::string& message)
    : m_pos{std::move(pos)},
      m_what{m_pos.filename + ":" + std::to_string(m_pos.line) + ": " + message} {}

const char* PosError::what() const noexcept { return m_what.c_str(); }

const Pos& PosError::pos() const { return m_pos; }

}  // namespace tiny
