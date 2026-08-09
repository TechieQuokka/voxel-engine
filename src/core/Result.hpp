#pragma once

// std::expected is C++23; GCC only exposes <expected> in C++23 mode. This is a
// minimal stand-in with a deliberately compatible surface, so migrating later
// is a matter of replacing this header with a set of type aliases.
//
// Used on every path where exceptions are banned: chunk generation, meshing,
// and the render loop. See docs/DESIGN.md section 6.2.

#include "core/Assert.hpp"

#include <type_traits>
#include <utility>
#include <variant>

namespace mc {

/// Wraps an error value so it can be returned where a Result is expected.
template <typename E>
class Unexpected {
public:
    explicit Unexpected(E error) : m_error(std::move(error)) {}

    const E& error() const& noexcept { return m_error; }
    E& error() & noexcept { return m_error; }
    E&& error() && noexcept { return std::move(m_error); }

private:
    E m_error;
};

template <typename E>
Unexpected(E) -> Unexpected<E>;

/// `return makeError(MyError::NotFound);`
template <typename E>
Unexpected<std::decay_t<E>> makeError(E&& error) {
    return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

/// Either a value of type T or an error of type E.
///
/// Indices are used rather than type-based variant access so that T and E may
/// be the same type.
template <typename T, typename E>
class [[nodiscard]] Result {
    static constexpr std::size_t kValue = 0;
    static constexpr std::size_t kError = 1;

public:
    using value_type = T;
    using error_type = E;

    Result(T value) : m_storage(std::in_place_index<kValue>, std::move(value)) {}
    Result(Unexpected<E> error)
        : m_storage(std::in_place_index<kError>, std::move(error).error()) {}

    bool hasValue() const noexcept { return m_storage.index() == kValue; }
    explicit operator bool() const noexcept { return hasValue(); }

    const T& value() const& {
        MC_ASSERT_MSG(hasValue(), "Result::value() on an error Result");
        return *std::get_if<kValue>(&m_storage);
    }
    T& value() & {
        MC_ASSERT_MSG(hasValue(), "Result::value() on an error Result");
        return *std::get_if<kValue>(&m_storage);
    }
    T&& value() && {
        MC_ASSERT_MSG(hasValue(), "Result::value() on an error Result");
        return std::move(*std::get_if<kValue>(&m_storage));
    }

    const E& error() const& {
        MC_ASSERT_MSG(!hasValue(), "Result::error() on a value Result");
        return *std::get_if<kError>(&m_storage);
    }
    E& error() & {
        MC_ASSERT_MSG(!hasValue(), "Result::error() on a value Result");
        return *std::get_if<kError>(&m_storage);
    }

    template <typename U>
    T valueOr(U&& fallback) const& {
        return hasValue() ? value() : static_cast<T>(std::forward<U>(fallback));
    }

    const T* operator->() const { return &value(); }
    T* operator->() { return &value(); }
    const T& operator*() const& { return value(); }
    T& operator*() & { return value(); }

private:
    std::variant<T, E> m_storage;
};

/// Specialization for operations that either succeed or fail with no payload.
template <typename E>
class [[nodiscard]] Result<void, E> {
    static constexpr std::size_t kValue = 0;
    static constexpr std::size_t kError = 1;

public:
    using value_type = void;
    using error_type = E;

    Result() : m_storage(std::in_place_index<kValue>) {}
    Result(Unexpected<E> error)
        : m_storage(std::in_place_index<kError>, std::move(error).error()) {}

    bool hasValue() const noexcept { return m_storage.index() == kValue; }
    explicit operator bool() const noexcept { return hasValue(); }

    const E& error() const& {
        MC_ASSERT_MSG(!hasValue(), "Result::error() on a success Result");
        return *std::get_if<kError>(&m_storage);
    }
    E& error() & {
        MC_ASSERT_MSG(!hasValue(), "Result::error() on a success Result");
        return *std::get_if<kError>(&m_storage);
    }

private:
    std::variant<std::monostate, E> m_storage;
};

} // namespace mc
