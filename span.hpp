#pragma once

#include <cstdint>
#include <type_traits>

namespace tiny {

template <typename T>
struct Span {
    template <std::size_t N>
    explicit Span(const T (&data)[N]) : m_data{data}, m_len{N} {}

    explicit Span(const T* data, std::size_t len) : m_data{data}, m_len{len} {}

    // Allow conversion from non-const span of the same type
    Span(const Span<std::remove_const_t<T>>& other) : m_data{other.m_data}, m_len{other.m_len} {}

    T* data() { return m_data; }
    std::size_t size() const { return m_len; }

    T* begin() { return m_data; }
    T* end() { return m_data + m_len; }

    const T* cbegin() const { return m_data; }
    const T* cend() const { return m_data + m_len; }

    T& operator[](std::size_t i) { return m_data[i]; }
    const T& operator[](std::size_t i) const { return m_data[i]; }

private:
    T* m_data = nullptr;
    std::size_t m_len = 0;
};

}  // namespace tiny
