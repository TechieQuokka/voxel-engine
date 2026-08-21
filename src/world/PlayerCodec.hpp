#pragma once

#include "core/Result.hpp"
#include "core/Types.hpp"
#include "world/Player.hpp"

#include <span>
#include <vector>

namespace mc {

/// The player, to bytes and back.
///
/// **Separate from the level file on purpose.** `level.bin` carries the world's
/// identity -- the magic, the version and the seed -- and it is written exactly once,
/// at creation, then only ever read. The player is rewritten on every quit. Putting
/// the two in one file would mean the seed record passing through a rewrite it has no
/// reason to take, and the seed is the one thing in a save that cannot be lost: every
/// unedited column is regenerated from it, so losing it does not lose the player, it
/// loses the world. A write-once file and a write-often file are different files.
///
/// **Separate from the region files for the same shape of reason.** A column is
/// addressed by where it is; the player is not anywhere in particular, and giving
/// them a column would mean the save for "the player" living or dying with whichever
/// column they happened to quit inside.
///
/// The format is the same style as `ChunkCodec`: little-endian fixed-width fields,
/// no padding, a version byte at the front. One architecture by constraint
/// (DESIGN.md 1), so no byte swapping.
namespace PlayerCodec {

enum class Error {
    /// Not a player record, or a version this build does not know.
    BadHeader,
    /// The buffer ended in the middle of a field.
    Truncated,
    /// A field held something that cannot be true -- an item id past the table, a
    /// count past the stack limit, a position that is not a number.
    Corrupt,
};

const char* describe(Error error);

/// Current format version. Bump when a field is added; `decode` refuses anything it
/// does not know rather than reading a shorter record as a longer one.
inline constexpr u8 kVersion = 1;

std::vector<u8> encode(const Player& player);

/// **Returns a whole `Player` rather than filling one in place**, so a record that
/// fails half way through cannot leave the live player half-loaded. The caller keeps
/// what it had and the run continues with a fresh spawn, which is the same thing an
/// unreadable column does.
Result<Player, Error> decode(std::span<const u8> bytes);

} // namespace PlayerCodec
} // namespace mc
