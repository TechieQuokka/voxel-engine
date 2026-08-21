#include <doctest/doctest.h>

#include "world/WalkMove.hpp"

using namespace mc;

// The rules these cases cover were all found by playing rather than by reasoning --
// the tree canopy that was walked through, the wall that stopped a diagonal dead, the
// player wedged in place by terrain that streamed in around them. They had no test at
// all while they lived inside `Engine::updateWalk`, which needs a window, a device and
// a streaming world before it can be called once.
//
// The blocking test is a lambda here rather than a world, which is the whole point of
// the extraction: what "blocked" means belongs to the caller, and these cases can
// therefore put a wall at 0.4 of a block, which no lattice of full cubes can.

namespace {

/// Everything with x >= wallX is solid.
auto wallAtX(f32 wallX) {
    return [wallX](const vec3& feet) { return feet.x >= wallX; };
}

/// Nothing is ever blocked. Open ground.
auto openWorld() {
    return [](const vec3&) { return false; };
}

constexpr f32 kEps = 1e-4f;

} // namespace

TEST_CASE("an unobstructed move applies both axes") {
    const vec3 start{10.0f, 64.0f, 10.0f};
    const vec3 end = slideWithStepUp(start, 0.5f, 0.25f, true, openWorld());

    CHECK(end.x == doctest::Approx(10.5f));
    CHECK(end.z == doctest::Approx(10.25f));
    CHECK(end.y == doctest::Approx(64.0f));
}

TEST_CASE("a wall refuses its own axis and leaves the other one moving") {
    // The diagonal into a wall. Testing the move as one vector would stop the player
    // dead; refusing X while Z goes through is what makes them slide along it.
    const vec3 start{10.0f, 64.0f, 10.0f};
    const vec3 end = slideWithStepUp(start, 1.0f, 1.0f, false, wallAtX(10.5f));

    CHECK(end.x == doctest::Approx(10.0f));
    CHECK(end.z == doctest::Approx(11.0f));
}

TEST_CASE("a corner is not cut diagonally") {
    // Solid unless the box is on one of the two axis-aligned sides of the corner.
    // A single diagonal test would find the destination free and pass through the
    // block that forms the corner.
    const auto corner = [](const vec3& feet) { return feet.x > 10.5f && feet.z > 10.5f; };

    const vec3 end = slideWithStepUp(vec3{10.0f, 64.0f, 10.0f}, 1.0f, 1.0f, true, corner);

    // X went first and succeeded, which then makes Z the blocked one -- so the player
    // ends up beside the corner rather than through it.
    CHECK(end.x == doctest::Approx(11.0f));
    CHECK(end.z == doctest::Approx(10.0f));
}

TEST_CASE("a rise within the step height is walked up") {
    // Solid below y = 64.4 past the line: a step, not a wall.
    const auto ledge = [](const vec3& feet) { return feet.x >= 10.5f && feet.y < 64.4f; };

    const vec3 end = slideWithStepUp(vec3{10.0f, 64.0f, 10.0f}, 1.0f, 0.0f, true, ledge);

    CHECK(end.x == doctest::Approx(11.0f));
    CHECK(end.y > 64.0f);
    CHECK(end.y <= 64.0f + WalkMove::kStepHeight + kEps);
}

TEST_CASE("a rise past the step height is a wall") {
    // A full block is 1.0 high and the step is 0.6, which is why every rise in this
    // world has to be jumped. That is vanilla's rule and it is what the number is for.
    const auto blockHigh = [](const vec3& feet) { return feet.x >= 10.5f && feet.y < 65.0f; };

    const vec3 end = slideWithStepUp(vec3{10.0f, 64.0f, 10.0f}, 1.0f, 0.0f, true, blockHigh);

    CHECK(end.x == doctest::Approx(10.0f));
    CHECK(end.y == doctest::Approx(64.0f));
}

TEST_CASE("nothing is stepped up while in mid-air") {
    // Stepping up without the ground under you is climbing, which is not a thing a
    // player can do. Same ledge as the case that passes on the ground.
    const auto ledge = [](const vec3& feet) { return feet.x >= 10.5f && feet.y < 64.4f; };

    const vec3 end = slideWithStepUp(vec3{10.0f, 64.0f, 10.0f}, 1.0f, 0.0f, false, ledge);

    CHECK(end.x == doctest::Approx(10.0f));
    CHECK(end.y == doctest::Approx(64.0f));
}

TEST_CASE("a box that starts wedged may always move") {
    // Terrain arrives around a standing player and falling blocks land on them.
    // Refusing motion then would leave no way out but quitting, so every axis is
    // allowed however solid the destination is.
    const auto solidEverywhere = [](const vec3&) { return true; };

    const vec3 end = slideWithStepUp(vec3{10.0f, 64.0f, 10.0f}, 0.5f, 0.5f, true,
                                     solidEverywhere);

    CHECK(end.x == doctest::Approx(10.5f));
    CHECK(end.z == doctest::Approx(10.5f));
}

TEST_CASE("being wedged is judged once, before either axis moves") {
    // A player who begins wedged must stay permitted for the whole move. If the wedge
    // were re-judged per axis, the first axis could carry them out of the solid block
    // and the second would then be refused half way through the same step.
    int calls = 0;
    const auto onlyStartIsSolid = [&](const vec3& feet) {
        ++calls;
        return feet.x == 10.0f && feet.z == 10.0f;
    };

    const vec3 end = slideWithStepUp(vec3{10.0f, 64.0f, 10.0f}, 0.5f, 0.5f, true,
                                     onlyStartIsSolid);

    CHECK(end.x == doctest::Approx(10.5f));
    CHECK(end.z == doctest::Approx(10.5f));

    // One test for the wedge, and none for either destination: a wedged move does not
    // need to ask, and asking would be the bug above.
    CHECK(calls == 1);
}

TEST_CASE("an axis with no motion is not tested and does not move") {
    int calls = 0;
    const auto counting = [&](const vec3&) {
        ++calls;
        return false;
    };

    const vec3 end = slideWithStepUp(vec3{10.0f, 64.0f, 10.0f}, 0.0f, 0.0f, true, counting);

    CHECK(end.x == doctest::Approx(10.0f));
    CHECK(end.z == doctest::Approx(10.0f));

    // Only the wedge test. Two axes of nothing ask nothing.
    CHECK(calls == 1);
}

TEST_CASE("a move backwards out of a wall is allowed") {
    // Walking away from something solid must never be refused by the thing being
    // walked away from.
    const vec3 end = slideWithStepUp(vec3{10.0f, 64.0f, 10.0f}, -1.0f, 0.0f, true,
                                     wallAtX(10.5f));

    CHECK(end.x == doctest::Approx(9.0f));
}

TEST_CASE("the step lands above the surface rather than exactly on it") {
    // The caller's ground probe snaps the feet down in the same frame, so the step
    // deliberately overshoots: landing exactly on the surface would leave the box
    // touching what it just climbed, and touching is overlapping for a probe that
    // works in fractions.
    const auto ledge = [](const vec3& feet) { return feet.x >= 10.5f && feet.y < 64.25f; };

    const vec3 end = slideWithStepUp(vec3{10.0f, 64.0f, 10.0f}, 1.0f, 0.0f, true, ledge);

    CHECK(end.y > 64.25f);
    CHECK(end.y == doctest::Approx(64.3f).epsilon(0.01));
}

// -- sneaking at an edge ------------------------------------------------------
//
// `kSneakSpeed` changed the speed and nothing else until this landed, so the key
// whose purpose is to let you work at the edge of a roof was the key that let you
// walk off it slowly.

namespace {

/// A floor that stops at `edgeX`: solid below y = 64 for x < edgeX, nothing beyond.
///
/// The blocking test is what a player box overlapping something answers, so "there
/// is floor here" is `feet.y < 64` -- a box resting exactly at 64 touches the floor
/// and does not overlap it, which is why `supported` drops the box before asking.
auto floorEndingAtX(f32 edgeX) {
    return [edgeX](const vec3& feet) { return feet.x < edgeX && feet.y < 64.0f; };
}

} // namespace

TEST_CASE("sneaking stops at the edge instead of walking off it") {
    const vec3 start{9.0f, 64.0f, 10.0f};

    // Walking: straight off, because nothing about the destination is solid.
    const vec3 walked = slideWithStepUp(start, 2.0f, 0.0f, true, floorEndingAtX(10.0f));
    CHECK(walked.x == doctest::Approx(11.0f));

    // Sneaking: held on the floor.
    const vec3 sneaked =
        slideWithStepUp(start, 2.0f, 0.0f, true, floorEndingAtX(10.0f), true);
    CHECK(sneaked.x < 10.0f);
}

TEST_CASE("sneaking creeps up to the edge rather than stopping short of it") {
    // **The reason this shortens the move rather than refusing it.** A refused move
    // leaves the player a whole step back from the edge, which is exactly where you
    // cannot stand to build outward -- and building outward from a ledge is what
    // sneaking is for. Within one back-off increment of the edge is the requirement.
    const vec3 start{9.9f, 64.0f, 10.0f};
    const vec3 end = slideWithStepUp(start, 1.0f, 0.0f, true, floorEndingAtX(10.0f), true);

    CHECK(end.x < 10.0f);
    CHECK(end.x > 10.0f - WalkMove::kEdgeBackOff - kEps);
}

TEST_CASE("sneaking away from an edge is never held back") {
    // Only the direction that leaves the ledge is restrained. Backing away from one
    // that the player is already standing on the lip of has to stay free, or sneaking
    // near an edge would be a trap rather than a safety.
    const vec3 start{9.98f, 64.0f, 10.0f};
    const vec3 end = slideWithStepUp(start, -1.0f, 0.0f, true, floorEndingAtX(10.0f), true);

    CHECK(end.x == doctest::Approx(8.98f));
}

TEST_CASE("sneaking in mid-air is falling, and the edge rule does not apply") {
    // There is no ledge to be held back from once the player has left it, and
    // applying the rule would stop someone mid-jump in open air.
    const vec3 start{9.0f, 70.0f, 10.0f};
    const vec3 end = slideWithStepUp(start, 2.0f, 0.0f, false, floorEndingAtX(10.0f), true);

    CHECK(end.x == doctest::Approx(11.0f));
}

TEST_CASE("a sneaking player who is already wedged may still move") {
    // The same escape the wedge check grants a walking player. Terrain streams in
    // around a standing player, and a sneak that refused motion then would leave
    // them stuck with no way out but quitting -- worse than a fall, because a fall
    // ends.
    const vec3 start{10.0f, 64.0f, 10.0f};
    const vec3 end = slideWithStepUp(start, 1.0f, 0.0f, true,
                                     [](const vec3&) { return true; }, true);

    CHECK(end.x == doctest::Approx(11.0f));
}

TEST_CASE("the edge rule holds each axis separately") {
    // A player sneaking diagonally toward a corner should be stopped on the axis
    // that leaves the floor and keep the one that does not -- the same separation
    // walking already has, for the same reason.
    const vec3 start{9.0f, 64.0f, 10.0f};
    const vec3 end =
        slideWithStepUp(start, 2.0f, 2.0f, true, floorEndingAtX(10.0f), true);

    CHECK(end.x < 10.0f);
    CHECK(end.z == doctest::Approx(12.0f));
}
