#pragma once
#include "colors.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
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
        // Compatibility GLSL exposes gl_Vertex, gl_Color, and the matrices set
        // by Camera::apply. The geometry shader will perform projection later.
        const char *vertex = R"GLSL(#version 330 compatibility
// Forward alpha as a categorical extrusion/travel flag; RGB is supplied by uniforms.
out vec4 lineColor;
void main(){gl_Position=gl_ModelViewMatrix*gl_Vertex;lineColor=gl_Color;}
)GLSL";
        // The draw call supplies GL_LINES, so a geometry invocation receives two
        // endpoints. A triangle strip with four output vertices creates a quad.
        const char *geometry = R"GLSL(#version 330 compatibility
layout(lines) in;
layout(triangle_strip,max_vertices=4) out;
in vec4 lineColor[];
// centerEye and across interpolate over the quad. across runs from -1 to +1
// across its width, providing the cylinder's cross-section coordinate.
out vec3 centerEye;
out float across;
// flat values belong to the whole segment; interpolation would distort its
// local orientation, radius, or material between the generated triangle vertices.
flat out vec3 sideEye;
flat out vec3 faceEye;
flat out vec3 baseColor;
flat out float tubeRadius;
uniform float viewportHeight;
uniform float showTravel;
uniform vec3 extrusionColor;
uniform vec3 travelColor;
void emitCorner(vec3 p,float x){
    // Offset one endpoint sideways by the display radius, then project it.
    // Keep the original centerline position separately for surface reconstruction.
    centerEye=p;across=x;
    gl_Position=gl_ProjectionMatrix*vec4(p+sideEye*tubeRadius*x,1);
    EmitVertex();
}
void main(){
    // Hiding travel is an early geometry-stage rejection: emit no triangles,
    // rather than building a second CPU mesh or rebuilding the GPU buffer.
    bool extrusion=lineColor[0].a>.5;
    if(!extrusion&&showTravel<.5)return;
    vec3 a=gl_in[0].gl_Position.xyz,b=gl_in[1].gl_Position.xyz;
    vec3 delta=b-a;
    // Degenerate segments have no usable tangent and would cause normalize(0).
    if(dot(delta,delta)<1e-16)return;
    vec3 tangent=normalize(delta),view=normalize(-(a+b)*.5);
    // The cross product gives a width direction perpendicular to both the
    // segment and the eye direction. The quad therefore faces the camera.
    vec3 side=cross(tangent,view);
    // Looking along the segment: choose a stable perpendicular basis.
    if(dot(side,side)<1e-8)side=cross(tangent,abs(tangent.z)<.9?vec3(0,0,1):vec3(0,1,0));
    sideEye=normalize(side);faceEye=normalize(cross(sideEye,tangent));
    // Select the cylinder half facing the viewer, independent of path direction.
    if(dot(faceEye,view)<0)faceEye=-faceEye;
    // Recover approximate world units per framebuffer pixel from perspective:
    // projection[1][1] is cot(verticalFov/2), and -eyeZ is positive view depth.
    float pixelWorld=2*max(.001,-(a.z+b.z)*.5)/(gl_ProjectionMatrix[1][1]*viewportHeight);
    // The nominal radius is in millimeters; the pixel floor keeps distant
    // toolpaths visible. This is display thickness, not real extrusion width.
    tubeRadius=max(extrusion?.11:.025,pixelWorld*(extrusion?.85:.45));
    baseColor=extrusion?extrusionColor:travelColor;
    // Alternating left/right corners makes two triangles without an index buffer.
    // EndPrimitive prevents this segment's strip from joining the next segment.
    emitCorner(a,-1);emitCorner(a,1);emitCorner(b,-1);emitCorner(b,1);EndPrimitive();
}
)GLSL";
        // Runs for covered samples of the expanded quad. Reconstruct a curved
        // normal/depth analytically, so even dense line sets show directional light.
        const char *fragment = R"GLSL(#version 330 compatibility
in vec3 centerEye;
in float across;
flat in vec3 sideEye;
flat in vec3 faceEye;
flat in vec3 baseColor;
flat in float tubeRadius;
uniform float cameraDistance;
uniform float modelSpan;
uniform vec3 backgroundColor;
out vec4 result;
void main(){
    // A unit circle obeys x*x + z*z = 1. The positive square root gives
    // the cylinder's front-facing half, with x supplied by the quad interpolation.
    float x=clamp(across,-1,1),z=sqrt(max(0,1-x*x));
    // Rotate the cross-section into the segment's eye-space basis. Its radial
    // direction is also the cylindrical surface normal used for illumination.
    vec3 normal=normalize(sideEye*x+faceEye*z);
    vec3 surface=centerEye+tubeRadius*(sideEye*x+faceEye*z);
    // Light is defined in eye space, like a lamp attached to the camera. Orbital
    // navigation changes how model segments face it and exposes the model's shape.
    vec3 light=normalize(vec3(-.45,.7,.65)),view=normalize(-surface);
    // Lambert diffuse plus a weaker second directional fill avoid a flat color
    // and prevent the unlit side from becoming completely invisible.
    float diffuse=max(dot(normal,light),0);
    float rim=max(dot(normal,normalize(vec3(.8,-.2,.25))),0);
    // Blinn-Phong: the halfway vector between light/view creates a narrow highlight.
    // Ambient + diffuse + fill + specular are visual cues, not physical simulation.
    float spec=pow(max(dot(normal,normalize(light+view)),0),32);
    vec3 lit=baseColor*(.20+.72*diffuse+.12*rim)+vec3(.32)*spec;
    // Fade farther surfaces toward the configured background. Scaling by model
    // span keeps this depth cue usable for both small and large objects.
    float fog=clamp((length(surface)-cameraDistance+modelSpan*.5)/max(modelSpan*2,.01),0,.65);
    result=vec4(mix(lit,backgroundColor,fog),1);
    // The rasterized quad is flat, but our apparent cylinder surface is curved.
    // Project that reconstructed surface and write its depth explicitly. The
    // default depth range maps normalized-device Z [-1,1] into window depth [0,1].
    // This allows intersecting toolpath segments to occlude each other by surface.
    vec4 projected=gl_ProjectionMatrix*vec4(surface,1);
    gl_FragDepth=clamp(projected.z/projected.w*.5+.5,0,1);
}
)GLSL";
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
