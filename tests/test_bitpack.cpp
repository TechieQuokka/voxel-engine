#include "core/BitPack.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace mc;

TEST_CASE("bitsForPaletteSize picks the narrowest supported width") {
    CHECK(bitpack::bitsForPaletteSize(0) == 0);
    CHECK(bitpack::bitsForPaletteSize(1) == 0);
    CHECK(bitpack::bitsForPaletteSize(2) == 1);
    CHECK(bitpack::bitsForPaletteSize(3) == 2);
    CHECK(bitpack::bitsForPaletteSize(4) == 2);
    CHECK(bitpack::bitsForPaletteSize(5) == 4);
    CHECK(bitpack::bitsForPaletteSize(16) == 4);
    CHECK(bitpack::bitsForPaletteSize(17) == 8);
    CHECK(bitpack::bitsForPaletteSize(256) == 8);
}

TEST_CASE("every supported width divides 64 evenly") {
    // This is the property that lets an index never straddle a word boundary,
    // which is what keeps get/set to a shift and a mask.
    for (const u32 bits : {1u, 2u, 4u, 8u}) {
        CHECK(64u % bits == 0);
        CHECK(bitpack::indicesPerWord(bits) * bits == 64u);
    }
}

TEST_CASE("wordsNeeded rounds up") {
    CHECK(bitpack::wordsNeeded(0, 4) == 0);
    CHECK(bitpack::wordsNeeded(1, 4) == 1);
    CHECK(bitpack::wordsNeeded(16, 4) == 1);
    CHECK(bitpack::wordsNeeded(17, 4) == 2);
    CHECK(bitpack::wordsNeeded(32768, 4) == 2048);
    CHECK(bitpack::wordsNeeded(32768, 1) == 512);
    CHECK(bitpack::wordsNeeded(32768, 8) == 4096);
    CHECK(bitpack::wordsNeeded(100, 0) == 0);
}

TEST_CASE("set then get round-trips at every width") {
    constexpr usize kCount = 300;

    for (const u32 bits : {1u, 2u, 4u, 8u}) {
        CAPTURE(bits);
        std::vector<u64> words(bitpack::wordsNeeded(kCount, bits), 0);
        const u32 modulus = static_cast<u32>(bitpack::mask(bits)) + 1;

        for (usize i = 0; i < kCount; ++i) {
            bitpack::set(words.data(), i, bits, static_cast<u32>(i) % modulus);
        }
        for (usize i = 0; i < kCount; ++i) {
            REQUIRE(bitpack::get(words.data(), i, bits) == static_cast<u32>(i) % modulus);
        }
    }
}

TEST_CASE("writing an index leaves its neighbours untouched") {
    constexpr u32 bits = 4;
    std::vector<u64> words(bitpack::wordsNeeded(64, bits), 0);

    for (usize i = 0; i < 64; ++i) {
        bitpack::set(words.data(), i, bits, 0xF);
    }

    // Overwrite one index in the middle of a word with a different value.
    bitpack::set(words.data(), 7, bits, 0x3);

    CHECK(bitpack::get(words.data(), 6, bits) == 0xF);
    CHECK(bitpack::get(words.data(), 7, bits) == 0x3);
    CHECK(bitpack::get(words.data(), 8, bits) == 0xF);
}

TEST_CASE("indices at word boundaries are addressed correctly") {
    constexpr u32 bits = 8;
    std::vector<u64> words(bitpack::wordsNeeded(16, bits), 0);

    bitpack::set(words.data(), 7, bits, 0xAA);  // last index of word 0
    bitpack::set(words.data(), 8, bits, 0x55);  // first index of word 1

    CHECK(bitpack::get(words.data(), 7, bits) == 0xAA);
    CHECK(bitpack::get(words.data(), 8, bits) == 0x55);
    CHECK(words.size() == 2);
}
