#include "core/Result.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <string>

namespace {

enum class TestError {
    NotFound,
    OutOfRange,
};

mc::Result<int, TestError> divide(int numerator, int denominator) {
    if (denominator == 0) {
        return mc::makeError(TestError::OutOfRange);
    }
    return numerator / denominator;
}

mc::Result<void, TestError> requirePositive(int value) {
    if (value <= 0) {
        return mc::makeError(TestError::NotFound);
    }
    return {};
}

} // namespace

TEST_CASE("Result carries a value on success") {
    const auto result = divide(10, 2);
    REQUIRE(result.hasValue());
    CHECK(static_cast<bool>(result));
    CHECK(result.value() == 5);
    CHECK(*result == 5);
}

TEST_CASE("Result carries an error on failure") {
    const auto result = divide(10, 0);
    REQUIRE_FALSE(result.hasValue());
    CHECK_FALSE(static_cast<bool>(result));
    CHECK(result.error() == TestError::OutOfRange);
}

TEST_CASE("valueOr substitutes on error") {
    CHECK(divide(10, 2).valueOr(-1) == 5);
    CHECK(divide(10, 0).valueOr(-1) == -1);
}

TEST_CASE("Result<void, E> distinguishes success from failure") {
    CHECK(requirePositive(3).hasValue());

    const auto failure = requirePositive(-3);
    REQUIRE_FALSE(failure.hasValue());
    CHECK(failure.error() == TestError::NotFound);
}

TEST_CASE("Result supports identical value and error types") {
    // Index-based variant access, not type-based, so T == E is well defined.
    mc::Result<std::string, std::string> ok{std::string("value")};
    mc::Result<std::string, std::string> bad{mc::makeError(std::string("error"))};

    REQUIRE(ok.hasValue());
    CHECK(ok.value() == "value");

    REQUIRE_FALSE(bad.hasValue());
    CHECK(bad.error() == "error");
}

TEST_CASE("Result moves non-copyable payloads") {
    mc::Result<std::unique_ptr<int>, TestError> result{std::make_unique<int>(42)};
    REQUIRE(result.hasValue());
    CHECK(*result.value() == 42);
}
