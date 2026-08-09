#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace mc::rhi {

/// A linked GL program.
///
/// Construction is a loading-boundary operation, so failures throw
/// std::runtime_error carrying the driver's compile log (see DESIGN.md 6.2).
class Shader {
public:
    /// Compiles and links a vertex/fragment pair from GLSL source.
    static Shader fromSource(std::string_view vertexSource,
                             std::string_view fragmentSource,
                             std::string_view debugName = "shader");

    /// Loads both stages from files under assets/shaders/.
    static Shader fromFiles(const std::filesystem::path& vertexPath,
                            const std::filesystem::path& fragmentPath);

    /// Compiles and links a compute program.
    static Shader fromComputeSource(std::string_view source,
                                    std::string_view debugName = "compute");

    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    void bind() const;

    u32 handle() const noexcept { return m_handle; }
    bool valid() const noexcept { return m_handle != 0; }

    void setUniform(const char* name, const mat4& value) const;
    void setUniform(const char* name, const vec3& value) const;
    void setUniform(const char* name, i32 value) const;
    void setUniform(const char* name, f32 value) const;
    void setUniform(const char* name, std::span<const vec4> values) const;

    /// Returns -1 if the name is not an active uniform.
    ///
    /// Uncached on purpose: uniforms are replaced by UBO/SSBO blocks from
    /// Phase 3 onward, so a cache here would optimize a path that is about to
    /// disappear.
    i32 uniformLocation(const char* name) const;

private:
    explicit Shader(u32 handle, std::string debugName);

    u32 m_handle = 0;
    std::string m_debugName;
};

} // namespace mc::rhi
