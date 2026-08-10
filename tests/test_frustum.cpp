#include "render/Camera.hpp"
#include "render/Frustum.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace mc;

namespace {

/// A camera at the origin looking along -Z, which is the direction
/// Camera::forward() returns for yaw 0, pitch 0.
Camera lookingForward(f32 nearPlane = 0.05f) {
    Camera camera;
    camera.setPosition({0.0f, 0.0f, 0.0f});
    camera.setOrientation(0.0f, 0.0f);
    camera.setPerspective(math::radians(70.0f), 16.0f / 9.0f, nearPlane);
    return camera;
}

/// Normalized device depth of a world-space point, as the depth test sees it.
f32 depthOf(const Camera& camera, const vec3& point) {
    const vec4 clip = camera.viewProjectionMatrix() * vec4{point, 1.0f};
    return clip.z / clip.w;
}

void boxAround(const vec3& center, f32 halfSize, vec3& minCorner, vec3& maxCorner) {
    minCorner = center - vec3{halfSize};
    maxCorner = center + vec3{halfSize};
}

} // namespace

TEST_CASE("forward and right are orthogonal and unit length") {
    Camera camera;
    camera.setOrientation(0.9f, 0.3f);

    CHECK(math::length(camera.forward()) == doctest::Approx(1.0f));
    CHECK(math::length(camera.right()) == doctest::Approx(1.0f));
    CHECK(math::dot(camera.forward(), camera.right()) == doctest::Approx(0.0f).epsilon(0.001));
}

TEST_CASE("yaw 0 pitch 0 looks along negative Z") {
    Camera camera;
    camera.setOrientation(0.0f, 0.0f);

    const vec3 forward = camera.forward();
    CHECK(forward.x == doctest::Approx(0.0f));
    CHECK(forward.y == doctest::Approx(0.0f));
    CHECK(forward.z == doctest::Approx(-1.0f));
}

TEST_CASE("pitch is clamped short of straight up and straight down") {
    Camera camera;

    camera.setOrientation(0.0f, 10.0f);
    CHECK(camera.pitch() < math::radians(90.0f));
    // The view basis must not degenerate at the limit.
    CHECK(math::length(camera.right()) == doctest::Approx(1.0f));

    camera.setOrientation(0.0f, -10.0f);
    CHECK(camera.pitch() > math::radians(-90.0f));
    CHECK(math::length(camera.right()) == doctest::Approx(1.0f));
}

TEST_CASE("reversed-Z puts depth 1 at the near plane and approaches 0 with distance") {
    constexpr f32 kNear = 0.05f;
    const Camera camera = lookingForward(kNear);

    CHECK(depthOf(camera, vec3{0.0f, 0.0f, -kNear}) == doctest::Approx(1.0f));

    const f32 close = depthOf(camera, vec3{0.0f, 0.0f, -10.0f});
    const f32 medium = depthOf(camera, vec3{0.0f, 0.0f, -1000.0f});
    const f32 distant = depthOf(camera, vec3{0.0f, 0.0f, -100000.0f});

    // Monotonically decreasing, and never reaching zero -- so nothing is ever
    // clipped for being too far away.
    CHECK(close > medium);
    CHECK(medium > distant);
    CHECK(distant > 0.0f);
}

TEST_CASE("depth precision is dense far away, which is the point of reversed-Z") {
    const Camera camera = lookingForward();

    // Two points a block apart at 1000 blocks must still differ in depth. With a
    // conventional projection at this near plane they would collapse to the same
    // value long before here.
    const f32 a = depthOf(camera, vec3{0.0f, 0.0f, -1000.0f});
    const f32 b = depthOf(camera, vec3{0.0f, 0.0f, -1001.0f});
    CHECK(a != b);
    CHECK(a > b);
}

TEST_CASE("the frustum keeps what is in front and rejects what is behind") {
    const Camera camera = lookingForward();
    const Frustum frustum(camera.viewProjectionMatrix());

    vec3 minCorner{0.0f};
    vec3 maxCorner{0.0f};

    boxAround(vec3{0.0f, 0.0f, -50.0f}, 4.0f, minCorner, maxCorner);
    CHECK(frustum.intersectsAabb(minCorner, maxCorner));

    boxAround(vec3{0.0f, 0.0f, 50.0f}, 4.0f, minCorner, maxCorner);
    CHECK_FALSE(frustum.intersectsAabb(minCorner, maxCorner));
}

TEST_CASE("there is no far plane, at any distance") {
    // The property the infinite projection exists for, and the one a textbook
    // six-plane extraction would destroy: the sixth plane's normal is zero, and
    // normalizing it yields a plane that rejects everything.
    const Camera camera = lookingForward();
    const Frustum frustum(camera.viewProjectionMatrix());

    vec3 minCorner{0.0f};
    vec3 maxCorner{0.0f};

    for (const f32 distance : {100.0f, 10000.0f, 1.0e6f, 1.0e9f}) {
        boxAround(vec3{0.0f, 0.0f, -distance}, distance * 0.01f, minCorner, maxCorner);
        CAPTURE(distance);
        REQUIRE(frustum.intersectsAabb(minCorner, maxCorner));
    }
}

TEST_CASE("the frustum rejects boxes outside each side plane") {
    const Camera camera = lookingForward();
    const Frustum frustum(camera.viewProjectionMatrix());

    vec3 minCorner{0.0f};
    vec3 maxCorner{0.0f};

    // At 50 blocks ahead with a 70 degree vertical FOV and 16:9 aspect, the
    // half-extents are roughly 35 vertically and 62 horizontally. 400 is well clear
    // of both in every direction.
    const vec3 ahead{0.0f, 0.0f, -50.0f};

    for (const vec3 offset : {vec3{400.0f, 0.0f, 0.0f},
                              vec3{-400.0f, 0.0f, 0.0f},
                              vec3{0.0f, 400.0f, 0.0f},
                              vec3{0.0f, -400.0f, 0.0f}}) {
        boxAround(ahead + offset, 4.0f, minCorner, maxCorner);
        CAPTURE(offset.x);
        CAPTURE(offset.y);
        REQUIRE_FALSE(frustum.intersectsAabb(minCorner, maxCorner));
    }
}

TEST_CASE("a box straddling the camera is visible") {
    const Camera camera = lookingForward();
    const Frustum frustum(camera.viewProjectionMatrix());

    // The camera is inside it, so no plane can reject it.
    const vec3 minCorner{-10.0f, -10.0f, -10.0f};
    const vec3 maxCorner{10.0f, 10.0f, 10.0f};
    CHECK(frustum.intersectsAabb(minCorner, maxCorner));
}

TEST_CASE("turning around changes what is visible") {
    Camera camera = lookingForward();
    const vec3 behindCamera{0.0f, 0.0f, 50.0f};

    vec3 minCorner{0.0f};
    vec3 maxCorner{0.0f};
    boxAround(behindCamera, 4.0f, minCorner, maxCorner);

    CHECK_FALSE(Frustum(camera.viewProjectionMatrix()).intersectsAabb(minCorner, maxCorner));

    // Yaw by 180 degrees; forward becomes +Z.
    camera.setOrientation(math::radians(180.0f), 0.0f);
    CHECK(Frustum(camera.viewProjectionMatrix()).intersectsAabb(minCorner, maxCorner));
}

TEST_CASE("section and column bounds line up with the grid") {
    vec3 minCorner{0.0f};
    vec3 maxCorner{0.0f};

    sectionBounds(SectionPos{0, 0, 0}, minCorner, maxCorner);
    CHECK(minCorner == vec3{0.0f, 0.0f, 0.0f});
    CHECK(maxCorner == vec3{32.0f, 32.0f, 32.0f});

    sectionBounds(SectionPos{-1, -2, 3}, minCorner, maxCorner);
    CHECK(minCorner == vec3{-32.0f, -64.0f, 96.0f});
    CHECK(maxCorner == vec3{0.0f, -32.0f, 128.0f});

    // A column spans the whole world height, which is what makes one test able to
    // reject twelve sections.
    columnBounds(ChunkPos{2, -3}, minCorner, maxCorner);
    CHECK(minCorner == vec3{64.0f, static_cast<f32>(kWorldMinY), -96.0f});
    CHECK(maxCorner == vec3{96.0f, static_cast<f32>(kWorldMaxY), -64.0f});
}

TEST_CASE("a column containing the camera is never culled") {
    // A regression guard for the plane signs: get one backwards and the world
    // vanishes from directly underfoot while distant terrain still draws.
    Camera camera;
    camera.setPosition({100.0f, 70.0f, -250.0f});
    camera.setOrientation(0.4f, -0.2f);
    camera.setPerspective(math::radians(70.0f), 16.0f / 9.0f, 0.05f);

    const Frustum frustum(camera.viewProjectionMatrix());

    vec3 minCorner{0.0f};
    vec3 maxCorner{0.0f};
    columnBounds(toChunkPos(BlockPos{100, 70, -250}), minCorner, maxCorner);

    CHECK(frustum.intersectsAabb(minCorner, maxCorner));
}
