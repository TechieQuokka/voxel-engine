#include "world/PlayerCodec.hpp"

#include "world/ItemTable.hpp"

#include <cmath>
#include <cstring>
#include <type_traits>

namespace mc {
namespace PlayerCodec {
namespace {

/// 'MCPL' -- Minecraft-like player. Little-endian, so a hex dump reads "LPCM".
constexpr u32 kMagic = 0x4C50434Du;

/// Appends fixed-width fields. `memcpy` rather than a cast, for the reason
/// `ChunkCodec`'s writer spells out: nothing here is aligned.
class ByteWriter {
public:
    explicit ByteWriter(std::vector<u8>& out) : m_out(out) {}

    template <typename T>
    void put(T value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const usize offset = m_out.size();
        m_out.resize(offset + sizeof(T));
        std::memcpy(m_out.data() + offset, &value, sizeof(T));
    }

private:
    std::vector<u8>& m_out;
};

/// Bounds-checked reads with a sticky failure flag, so the decoder can read the whole
/// record and test once rather than testing every field.
class ByteReader {
public:
    explicit ByteReader(std::span<const u8> bytes) : m_bytes(bytes) {}

    template <typename T>
    T get() {
        static_assert(std::is_trivially_copyable_v<T>);
        T value{};
        if (m_offset + sizeof(T) > m_bytes.size()) {
            m_overran = true;
            return value;
        }
        std::memcpy(&value, m_bytes.data() + m_offset, sizeof(T));
        m_offset += sizeof(T);
        return value;
    }

    bool overran() const noexcept { return m_overran; }
    usize remaining() const noexcept { return m_bytes.size() - m_offset; }

private:
    std::span<const u8> m_bytes;
    usize m_offset = 0;
    bool m_overran = false;
};

/// **A saved float has to be checked before it is believed.** A NaN position would
/// put the player nowhere and pass every comparison silently -- `feet.y < 0` is false
/// for NaN, so the ground probe would never fire and the fall would never end. The
/// range is deliberately loose: this is a test for "not a number and not absurd",
/// not for "inside the world", because the world's own bounds already clamp.
bool sane(f32 value) {
    return std::isfinite(value) && std::fabs(value) < 1.0e7f;
}

} // namespace

const char* describe(Error error) {
    switch (error) {
    case Error::BadHeader: return "not a player record this build can read";
    case Error::Truncated: return "player record ends mid-field";
    case Error::Corrupt:   return "player record holds a value that cannot be true";
    }
    return "unknown error";
}

std::vector<u8> encode(const Player& player) {
    std::vector<u8> out;
    // Header, the scalars, then 36 slots of (id, count).
    out.reserve(sizeof(u32) + 1 + 8 * sizeof(f32) + 2
                + Inventory::kStorageSlots * (sizeof(ItemId) + sizeof(u32)));

    ByteWriter writer(out);
    writer.put(kMagic);
    writer.put(kVersion);

    writer.put(player.position.x);
    writer.put(player.position.y);
    writer.put(player.position.z);
    writer.put(player.yaw);
    writer.put(player.pitch);
    writer.put(player.health);
    writer.put(player.verticalVelocity);

    writer.put(static_cast<u8>(player.flying ? 1 : 0));
    writer.put(static_cast<u8>(player.onGround ? 1 : 0));
    writer.put(static_cast<u8>(player.hotbarSlot));

    // **The cursor is not written.** It is the stack being dragged with a window
    // open, and a window cannot be open across a quit: `Engine::closeScreen` returns
    // it to the slots and drops what does not fit. Writing it would save an item
    // twice -- once here and once in the slot it was about to land in.
    for (usize slot = 0; slot < Inventory::kStorageSlots; ++slot) {
        const ItemStack& stack = player.inventory.at(slot);
        // Normalised on the way out, so an empty slot is always the same bytes
        // whichever of the two ways it got that way.
        const bool empty = stack.empty();
        writer.put(empty ? kNoItem : stack.item);
        writer.put(empty ? u32{0} : stack.count);
    }

    return out;
}

Result<Player, Error> decode(std::span<const u8> bytes) {
    ByteReader reader(bytes);

    if (reader.get<u32>() != kMagic) {
        return makeError(Error::BadHeader);
    }
    if (reader.get<u8>() != kVersion) {
        // Refused rather than read as far as it goes. A shorter record read as a
        // longer one puts the tail of the inventory wherever the buffer happened to
        // end, and the failure would look like items vanishing rather than like a
        // format change.
        return makeError(Error::BadHeader);
    }

    Player player;
    player.position.x = reader.get<f32>();
    player.position.y = reader.get<f32>();
    player.position.z = reader.get<f32>();
    player.yaw = reader.get<f32>();
    player.pitch = reader.get<f32>();
    player.health = reader.get<f32>();
    player.verticalVelocity = reader.get<f32>();

    player.flying = reader.get<u8>() != 0;
    player.onGround = reader.get<u8>() != 0;
    const auto hotbarSlot = reader.get<u8>();

    for (usize slot = 0; slot < Inventory::kStorageSlots; ++slot) {
        const auto item = reader.get<ItemId>();
        const auto count = reader.get<u32>();

        if (reader.overran()) {
            break; // Checked once, below.
        }
        if (item == kNoItem || count == 0) {
            continue; // Already default-constructed empty.
        }
        if (!itemExists(item)) {
            return makeError(Error::Corrupt);
        }

        ItemStack& target = player.inventory.mutableAt(slot);
        target.item = item;
        // **Clamped rather than refused.** A count past the limit is what a stack
        // limit being lowered between builds looks like -- sixty-four of something
        // that now stacks to one -- and losing the slot would be a worse answer than
        // losing the excess.
        target.count = std::min(count, target.stackLimit());
    }

    if (reader.overran()) {
        return makeError(Error::Truncated);
    }

    if (!sane(player.position.x) || !sane(player.position.y) || !sane(player.position.z)
        || !sane(player.yaw) || !sane(player.pitch) || !sane(player.health)
        || !sane(player.verticalVelocity)) {
        return makeError(Error::Corrupt);
    }
    if (hotbarSlot >= Inventory::kHotbarSlots) {
        return makeError(Error::Corrupt);
    }
    if (player.health < 0.0f || player.health > Player::kMaxHealth) {
        return makeError(Error::Corrupt);
    }

    player.hotbarSlot = hotbarSlot;
    // Re-clamped rather than trusted, so a record written by a build with a different
    // limit cannot hand back a degenerate view basis.
    player.pitch = math::clamp(player.pitch, -Player::kMaxPitch, Player::kMaxPitch);

    return player;
}

} // namespace PlayerCodec
} // namespace mc
