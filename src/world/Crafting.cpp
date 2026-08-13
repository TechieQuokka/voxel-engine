#include "world/Crafting.hpp"

#include <algorithm>
#include <cstddef>

namespace mc {
namespace {

constexpr usize kSide = 3;

/// The bounding box of the occupied cells, as (minRow, minCol, rows, cols).
/// `rows == 0` means the grid is empty.
struct Bounds {
    usize row = 0;
    usize column = 0;
    usize rows = 0;
    usize columns = 0;
};

Bounds boundsOf(const std::array<ItemId, 9>& grid) {
    usize minRow = kSide;
    usize maxRow = 0;
    usize minColumn = kSide;
    usize maxColumn = 0;
    bool any = false;

    for (usize r = 0; r < kSide; ++r) {
        for (usize c = 0; c < kSide; ++c) {
            if (grid[r * kSide + c] == kNoItem) {
                continue;
            }
            any = true;
            minRow = std::min(minRow, r);
            maxRow = std::max(maxRow, r);
            minColumn = std::min(minColumn, c);
            maxColumn = std::max(maxColumn, c);
        }
    }

    if (!any) {
        return Bounds{};
    }
    return Bounds{minRow, minColumn, maxRow - minRow + 1, maxColumn - minColumn + 1};
}

/// Compares two grids over their own bounding boxes, so a pattern drawn in the
/// top-left matches the same pattern drawn in the bottom-right.
///
/// `mirror` flips the *grid* horizontally within its box rather than flipping the
/// recipe, which keeps the recipe table the single spelling of each shape.
bool matchesShape(const std::array<ItemId, 9>& grid, const Bounds& gridBounds,
                  const Recipe& recipe, const Bounds& recipeBounds, bool mirror) {
    if (gridBounds.rows != recipeBounds.rows || gridBounds.columns != recipeBounds.columns) {
        return false;
    }

    for (usize r = 0; r < gridBounds.rows; ++r) {
        for (usize c = 0; c < gridBounds.columns; ++c) {
            const usize sourceColumn = mirror ? gridBounds.columns - 1 - c : c;
            const ItemId have =
                grid[(gridBounds.row + r) * kSide + gridBounds.column + sourceColumn];
            const ItemId want =
                recipe.grid[(recipeBounds.row + r) * kSide + recipeBounds.column + c];
            if (have != want) {
                return false;
            }
        }
    }
    return true;
}

/// Shapeless: the same items in any arrangement, counted.
///
/// Counted rather than set-compared, so two logs in the grid do not satisfy a
/// one-log recipe -- they would craft four planks and silently eat the second log.
bool matchesShapeless(const std::array<ItemId, 9>& grid, const Recipe& recipe) {
    std::array<ItemId, 9> have{};
    usize haveCount = 0;
    for (const ItemId id : grid) {
        if (id != kNoItem) {
            have[haveCount++] = id;
        }
    }

    std::array<ItemId, 9> want{};
    usize wantCount = 0;
    for (const ItemId id : recipe.grid) {
        if (id != kNoItem) {
            want[wantCount++] = id;
        }
    }

    if (haveCount != wantCount) {
        return false;
    }

    std::sort(have.begin(), have.begin() + static_cast<std::ptrdiff_t>(haveCount));
    std::sort(want.begin(), want.begin() + static_cast<std::ptrdiff_t>(wantCount));
    return std::equal(have.begin(), have.begin() + static_cast<std::ptrdiff_t>(haveCount),
                      want.begin());
}

} // namespace

CraftResult matchRecipe(const std::array<ItemId, 9>& grid) {
    const Bounds gridBounds = boundsOf(grid);
    if (gridBounds.rows == 0) {
        return CraftResult{}; // An empty grid crafts nothing, and asks no recipe.
    }

    for (const Recipe& recipe : kRecipes) {
        if (recipe.shapeless) {
            if (matchesShapeless(grid, recipe)) {
                return CraftResult{recipe.output, recipe.count};
            }
            continue;
        }

        const Bounds recipeBounds = boundsOf(recipe.grid);
        if (matchesShape(grid, gridBounds, recipe, recipeBounds, false)
            || matchesShape(grid, gridBounds, recipe, recipeBounds, true)) {
            return CraftResult{recipe.output, recipe.count};
        }
    }

    return CraftResult{};
}

} // namespace mc
