#include "world/Chunk.hpp"

namespace mc {

Section* Chunk::sectionAt(i32 sectionY) {
    if (!isValidSectionY(sectionY)) {
        return nullptr;
    }
    return &m_sections[static_cast<usize>(sectionIndexInColumn(sectionY))];
}

const Section* Chunk::sectionAt(i32 sectionY) const {
    if (!isValidSectionY(sectionY)) {
        return nullptr;
    }
    return &m_sections[static_cast<usize>(sectionIndexInColumn(sectionY))];
}

void Chunk::markSectionDirty(usize index) {
    MC_ASSERT(index < kSectionCount);
    m_dirty.fetch_or(static_cast<u16>(u16{1} << index), std::memory_order_release);
}

void Chunk::clearSectionDirty(usize index) {
    MC_ASSERT(index < kSectionCount);
    m_dirty.fetch_and(static_cast<u16>(~(u16{1} << index)), std::memory_order_release);
}

bool Chunk::isSectionDirty(usize index) const {
    MC_ASSERT(index < kSectionCount);
    return (dirtyMask() & static_cast<u16>(u16{1} << index)) != 0;
}

void Chunk::markAllDirty() {
    constexpr auto kAll = static_cast<u16>((1u << kSectionCount) - 1u);
    m_dirty.store(kAll, std::memory_order_release);
}

usize Chunk::memoryUsage() const {
    usize total = sizeof(Chunk);
    for (const Section& section : m_sections) {
        total += section.memoryUsage();
    }
    return total;
}

} // namespace mc
