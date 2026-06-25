#pragma once

// A non-owning view over a contiguous sequence. On C++20 toolchains (desktop,
// tests, Daisy) this is exactly std::span; on a C++17 toolchain (Teensy) it is a
// minimal allocation-free polyfill with the same shape. One vocabulary type lets
// the same core/ source compile under both standards (research.md decision 3).
//
// This is a standard-library shim, NOT a runtime fallback (Constitution V is
// about mock data/behavior, not vocabulary types for an older standard).

#include <cstddef>

#if defined(__cpp_lib_span) && __cpp_lib_span >= 202002L
#include <span>
namespace acfx {
template <typename T>
using span = std::span<T>;
} // namespace acfx
#else
#include <utility> // std::declval (used in the converting ctor below)
namespace acfx {

// Minimal dynamic-extent non-owning view (the subset core/ uses).
template <typename T>
class span {
public:
    constexpr span() noexcept : data_(nullptr), size_(0) {}
    constexpr span(T* data, std::size_t size) noexcept : data_(data), size_(size) {}

    template <std::size_t N>
    constexpr span(T (&arr)[N]) noexcept : data_(arr), size_(N) {}

    // Bind to a std::array (const or non-const) without dragging in <array>:
    // any contiguous container exposing data()/size() converts via this ctor.
    template <typename Container,
              typename = decltype(static_cast<T*>(std::declval<Container&>().data()))>
    constexpr span(Container& c) noexcept : data_(c.data()), size_(c.size()) {}

    constexpr T* data() const noexcept { return data_; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }
    constexpr T& operator[](std::size_t i) const noexcept { return data_[i]; }
    constexpr T* begin() const noexcept { return data_; }
    constexpr T* end() const noexcept { return data_ + size_; }

private:
    T* data_;
    std::size_t size_;
};

} // namespace acfx
#endif
