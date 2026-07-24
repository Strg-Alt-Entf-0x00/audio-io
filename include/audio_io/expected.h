// expected.h - Rust-style Result<T, E> for C++20
// Part of audio-io-1.0.0
// Minimal implementation for error handling

#pragma once

#include <variant>
#include <utility>
#include <type_traits>

namespace audio_io {

// Unexpected type (like Rust's Err)
template<typename E>
class Unexpected {
public:
    constexpr explicit Unexpected(const E& error) : error_(error) {}
    constexpr explicit Unexpected(E&& error) : error_(std::move(error)) {}
    
    constexpr const E& error() const& { return error_; }
    constexpr E& error() & { return error_; }
    constexpr const E&& error() const&& { return std::move(error_); }
    constexpr E&& error() && { return std::move(error_); }
    
private:
    E error_;
};

// Helper function to create Unexpected
template<typename E>
constexpr Unexpected<std::decay_t<E>> makeUnexpected(E&& error) {
    return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

// Expected type (like Rust's Result<T, E>)
template<typename T, typename E>
class Expected {
public:
    // Constructors for success case
    constexpr Expected(const T& value) : storage_(value), hasValue_(true) {}
    constexpr Expected(T&& value) : storage_(std::move(value)), hasValue_(true) {}
    
    // Constructor for error case
    constexpr Expected(const Unexpected<E>& error) : storage_(error.error()), hasValue_(false) {}
    constexpr Expected(Unexpected<E>&& error) : storage_(std::move(error.error())), hasValue_(false) {}
    
    // Check if has value
    constexpr bool has_value() const { return hasValue_; }
    constexpr explicit operator bool() const { return hasValue_; }
    
    // Access value (must check has_value() first!)
    constexpr const T& value() const& { return std::get<T>(storage_); }
    constexpr T& value() & { return std::get<T>(storage_); }
    constexpr const T&& value() const&& { return std::move(std::get<T>(storage_)); }
    constexpr T&& value() && { return std::move(std::get<T>(storage_)); }
    
    constexpr const T& operator*() const& { return value(); }
    constexpr T& operator*() & { return value(); }
    constexpr const T&& operator*() const&& { return std::move(value()); }
    constexpr T&& operator*() && { return std::move(value()); }
    
    constexpr const T* operator->() const { return &value(); }
    constexpr T* operator->() { return &value(); }
    
    // Access error (must check !has_value() first!)
    constexpr const E& error() const& { return std::get<E>(storage_); }
    constexpr E& error() & { return std::get<E>(storage_); }
    constexpr const E&& error() const&& { return std::move(std::get<E>(storage_)); }
    constexpr E&& error() && { return std::move(std::get<E>(storage_)); }
    
private:
    std::variant<T, E> storage_;
    bool hasValue_;
};

} // namespace audio_io
