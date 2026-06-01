#pragma once
#include <variant>
#include <string>

namespace merak {

// std::expected 在 C++23 才引入，g++11 不支持。
// Result<T, E> 提供相同的语义：要么包含值 T，要么包含错误 E。
template <typename T, typename E>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(E error) : data_(std::move(error)) {}

    bool has_value() const { return std::holds_alternative<T>(data_); }
    bool has_error() const { return std::holds_alternative<E>(data_); }

    T& value() { return std::get<T>(data_); }
    const T& value() const { return std::get<T>(data_); }

    T value_or(T default_val) const {
        return has_value() ? std::get<T>(data_) : std::move(default_val);
    }

    E& error() { return std::get<E>(data_); }
    const E& error() const { return std::get<E>(data_); }

    explicit operator bool() const { return has_value(); }

private:
    std::variant<T, E> data_;
};

} // namespace merak
