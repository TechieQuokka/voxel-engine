#include "rhi/Shader.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"

#include <glad/gl.h>

#include <format>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mc::rhi {
namespace {

const char* stageName(GLenum stage) {
    switch (stage) {
    case GL_VERTEX_SHADER:   return "vertex";
    case GL_FRAGMENT_SHADER: return "fragment";
    case GL_COMPUTE_SHADER:  return "compute";
    default:                 return "unknown";
    }
}

std::string shaderInfoLog(GLuint shader) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length <= 0) {
        return {};
    }
    std::vector<char> buffer(static_cast<std::size_t>(length));
    glGetShaderInfoLog(shader, length, nullptr, buffer.data());
    return std::string(buffer.data());
}

std::string programInfoLog(GLuint program) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    if (length <= 0) {
        return {};
    }
    std::vector<char> buffer(static_cast<std::size_t>(length));
    glGetProgramInfoLog(program, length, nullptr, buffer.data());
    return std::string(buffer.data());
}

GLuint compileStage(GLenum stage, std::string_view source, std::string_view debugName) {
    const GLuint shader = glCreateShader(stage);
    if (shader == 0) {
        throw std::runtime_error("glCreateShader returned 0");
    }

    const auto* text = source.data();
    const auto length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        const std::string log = shaderInfoLog(shader);
        glDeleteShader(shader);
        throw std::runtime_error(std::format("{} ({} stage) failed to compile:\n{}",
                                             debugName, stageName(stage), log));
    }
    return shader;
}

GLuint linkProgram(const std::vector<GLuint>& stages, std::string_view debugName) {
    const GLuint program = glCreateProgram();
    if (program == 0) {
        throw std::runtime_error("glCreateProgram returned 0");
    }

    for (const GLuint stage : stages) {
        glAttachShader(program, stage);
    }
    glLinkProgram(program);

    // Shaders can be deleted immediately; the program keeps them alive until
    // it is itself deleted.
    for (const GLuint stage : stages) {
        glDetachShader(program, stage);
        glDeleteShader(stage);
    }

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        const std::string log = programInfoLog(program);
        glDeleteProgram(program);
        throw std::runtime_error(std::format("{} failed to link:\n{}", debugName, log));
    }
    return program;
}

} // namespace

Shader::Shader(u32 handle, std::string debugName)
    : m_handle(handle), m_debugName(std::move(debugName)) {}

Shader::~Shader() {
    if (m_handle != 0) {
        glDeleteProgram(m_handle);
    }
}

Shader::Shader(Shader&& other) noexcept
    : m_handle(std::exchange(other.m_handle, 0)),
      m_debugName(std::move(other.m_debugName)) {}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_handle != 0) {
            glDeleteProgram(m_handle);
        }
        m_handle = std::exchange(other.m_handle, 0);
        m_debugName = std::move(other.m_debugName);
    }
    return *this;
}

Shader Shader::fromSource(std::string_view vertexSource,
                          std::string_view fragmentSource,
                          std::string_view debugName) {
    std::vector<GLuint> stages;
    stages.push_back(compileStage(GL_VERTEX_SHADER, vertexSource, debugName));
    stages.push_back(compileStage(GL_FRAGMENT_SHADER, fragmentSource, debugName));

    const GLuint program = linkProgram(stages, debugName);
    logDebug("Linked shader '{}' (program {})", debugName, program);
    return Shader(program, std::string(debugName));
}

Shader Shader::fromFiles(const std::filesystem::path& vertexPath,
                         const std::filesystem::path& fragmentPath) {
    const std::string vertexSource = readTextFile(vertexPath);
    const std::string fragmentSource = readTextFile(fragmentPath);
    return fromSource(vertexSource, fragmentSource, vertexPath.stem().string());
}

Shader Shader::fromComputeSource(std::string_view source, std::string_view debugName) {
    std::vector<GLuint> stages;
    stages.push_back(compileStage(GL_COMPUTE_SHADER, source, debugName));

    const GLuint program = linkProgram(stages, debugName);
    logDebug("Linked compute shader '{}' (program {})", debugName, program);
    return Shader(program, std::string(debugName));
}

void Shader::bind() const {
    glUseProgram(m_handle);
}

i32 Shader::uniformLocation(const char* name) const {
    return glGetUniformLocation(m_handle, name);
}

} // namespace mc::rhi
