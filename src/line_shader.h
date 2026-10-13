#pragma once
#include "colors.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <line_shaders.generated.h>
#include <stdexcept>
#include <string>

// Toolpath rendering pipeline:
//   vertex shader   -> world positions become camera/eye-space positions;
//   geometry shader -> each line becomes a camera-facing quad (two triangles);
//   fragment shader -> shade that quad as the visible half of a small cylinder.
// This is an impostor, not a full cylinder mesh. Four generated vertices per
// segment keep GPU geometry small and leave CPU geometry/slider indices unchanged.
struct LineShader {
    // Entry points are resolved from the active context, rather than linking
    // against a platform-specific GL loader library. The PFNGL types come from
    // SDL's OpenGL extension headers and match the driver function signatures.
    PFNGLCREATESHADERPROC createShader{};
    PFNGLSHADERSOURCEPROC shaderSource{};
    PFNGLCOMPILESHADERPROC compileShader{};
    PFNGLGETSHADERIVPROC getShaderiv{};
    PFNGLGETSHADERINFOLOGPROC getShaderInfoLog{};
    PFNGLDELETESHADERPROC deleteShader{};
    PFNGLCREATEPROGRAMPROC createProgram{};
    PFNGLATTACHSHADERPROC attachShader{};
    PFNGLLINKPROGRAMPROC linkProgram{};
    PFNGLGETPROGRAMIVPROC getProgramiv{};
    PFNGLGETPROGRAMINFOLOGPROC getProgramInfoLog{};
    PFNGLDELETEPROGRAMPROC deleteProgram{};
    PFNGLUSEPROGRAMPROC useProgram{};
    PFNGLGETUNIFORMLOCATIONPROC getUniformLocation{};
    PFNGLUNIFORM1FPROC uniform1f{};
    PFNGLUNIFORM3FPROC uniform3f{};
    // OpenGL object name for the linked executable containing all three stages.
    GLuint program = 0;

    void init() {
        // Must run after SDL_GL_CreateContext has made a context current. Fail
        // immediately if the driver cannot supply any required shader operation.
#define LOAD(member, type, name)                                  \
    member = reinterpret_cast<type>(SDL_GL_GetProcAddress(name)); \
    if (!member)                                                  \
    throw std::runtime_error("Missing " name)
        LOAD(createShader, PFNGLCREATESHADERPROC, "glCreateShader");
        LOAD(shaderSource, PFNGLSHADERSOURCEPROC, "glShaderSource");
        LOAD(compileShader, PFNGLCOMPILESHADERPROC, "glCompileShader");
        LOAD(getShaderiv, PFNGLGETSHADERIVPROC, "glGetShaderiv");
        LOAD(getShaderInfoLog, PFNGLGETSHADERINFOLOGPROC, "glGetShaderInfoLog");
        LOAD(deleteShader, PFNGLDELETESHADERPROC, "glDeleteShader");
        LOAD(createProgram, PFNGLCREATEPROGRAMPROC, "glCreateProgram");
        LOAD(attachShader, PFNGLATTACHSHADERPROC, "glAttachShader");
        LOAD(linkProgram, PFNGLLINKPROGRAMPROC, "glLinkProgram");
        LOAD(getProgramiv, PFNGLGETPROGRAMIVPROC, "glGetProgramiv");
        LOAD(getProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC, "glGetProgramInfoLog");
        LOAD(deleteProgram, PFNGLDELETEPROGRAMPROC, "glDeleteProgram");
        LOAD(useProgram, PFNGLUSEPROGRAMPROC, "glUseProgram");
        LOAD(getUniformLocation, PFNGLGETUNIFORMLOCATIONPROC, "glGetUniformLocation");
        LOAD(uniform1f, PFNGLUNIFORM1FPROC, "glUniform1f");
        LOAD(uniform3f, PFNGLUNIFORM3FPROC, "glUniform3f");
#undef LOAD
        // Generated at build time from src/shaders/line.*; no runtime file reads.
        const char *vertex = EmbeddedShaders::vertex;
        const char *geometry = EmbeddedShaders::geometry;
        const char *fragment = EmbeddedShaders::fragment;
        // Compile every stage independently so errors identify the offending GLSL.
        // shaderSource receives the embedded UTF-8/ASCII source string directly;
        // a null length means OpenGL reads up to its terminating zero byte.
        program = createProgram();
        for (auto entry : {std::pair<GLenum, const char *>{GL_VERTEX_SHADER, vertex}, {GL_GEOMETRY_SHADER, geometry}, {GL_FRAGMENT_SHADER, fragment}}) {
            GLuint shader = createShader(entry.first);
            shaderSource(shader, 1, &entry.second, nullptr);
            compileShader(shader);
            GLint ok = 0;
            getShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[4096]{};
                getShaderInfoLog(shader, sizeof log, nullptr, log);
                deleteShader(shader);
                throw std::runtime_error(std::string("Line shader: ") + log);
            }

            // Attach before deleting the handle. OpenGL retains the attached
            // shader object until the program no longer needs it; deletion here
            // marks it for eventual cleanup rather than invalidating the attachment.
            attachShader(program, shader);
            deleteShader(shader);
        }

        // Linking checks stage interfaces and creates the complete executable.
        // A compile success alone does not guarantee compatible varyings/layouts.
        linkProgram(program);
        GLint ok = 0;
        getProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[4096]{};
            getProgramInfoLog(program, sizeof log, nullptr, log);
            throw std::runtime_error(std::string("Line shader link: ") + log);
        }
    }

    void begin(float height, float distance, float span, bool travel, const Colors &colors) {
        // Uniforms are small values shared by this draw. Updating them changes
        // camera scale, visibility, and colors without touching the vertex buffer.
        // Uniform updates apply to the currently bound program, hence useProgram first.
        useProgram(program);
        uniform1f(getUniformLocation(program, "viewportHeight"), height);
        uniform1f(getUniformLocation(program, "cameraDistance"), distance);
        uniform1f(getUniformLocation(program, "modelSpan"), span);
        uniform1f(getUniformLocation(program, "showTravel"), travel ? 1.f : 0.f);
        auto setColor = [&](const char *name, RGB c) { uniform3f(getUniformLocation(program, name), c.red(), c.green(), c.blue()); };
        setColor("extrusionColor", colors[Colors::Line]);
        setColor("travelColor", colors[Colors::Travel]);
        setColor("backgroundColor", colors[Colors::Background]);
    }

    void end() {
        // Program zero restores compatibility fixed-function rendering for UI/grid.
        useProgram(0);
    }

    void destroy() {
        // Delete while the owning OpenGL context is still current, before SDL teardown.
        deleteProgram(program);
        program = 0;
    }
};
