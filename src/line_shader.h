#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <stdexcept>
#include <string>

// Expand the original line VBO on the GPU. CPU geometry and slider indices stay
// unchanged.
struct LineShader {
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
    GLuint program = 0;
    void init() {
#define LOAD(member, type, name)                                               \
    member = reinterpret_cast<type>(SDL_GL_GetProcAddress(name));              \
    if (!member)                                                               \
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
        LOAD(getProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC,
             "glGetProgramInfoLog");
        LOAD(deleteProgram, PFNGLDELETEPROGRAMPROC, "glDeleteProgram");
        LOAD(useProgram, PFNGLUSEPROGRAMPROC, "glUseProgram");
        LOAD(getUniformLocation, PFNGLGETUNIFORMLOCATIONPROC,
             "glGetUniformLocation");
        LOAD(uniform1f, PFNGLUNIFORM1FPROC, "glUniform1f");
#undef LOAD
        const char *vertex = R"GLSL(#version 330 compatibility
out vec4 lineColor;
void main(){gl_Position=gl_ModelViewMatrix*gl_Vertex;lineColor=gl_Color;}
)GLSL";
        const char *geometry = R"GLSL(#version 330 compatibility
layout(lines) in;
layout(triangle_strip,max_vertices=4) out;
in vec4 lineColor[];
out vec3 centerEye;
out float across;
flat out vec3 sideEye;
flat out vec3 faceEye;
flat out vec3 baseColor;
flat out float tubeRadius;
uniform float viewportHeight;
uniform float showTravel;
void emitCorner(vec3 p,float x){
    centerEye=p;across=x;
    gl_Position=gl_ProjectionMatrix*vec4(p+sideEye*tubeRadius*x,1);
    EmitVertex();
}
void main(){
    bool extrusion=lineColor[0].a>.5;
    if(!extrusion&&showTravel<.5)return;
    vec3 a=gl_in[0].gl_Position.xyz,b=gl_in[1].gl_Position.xyz;
    vec3 delta=b-a;
    if(dot(delta,delta)<1e-16)return;
    vec3 tangent=normalize(delta),view=normalize(-(a+b)*.5);
    vec3 side=cross(tangent,view);
    // Looking along the segment: choose a stable perpendicular basis.
    if(dot(side,side)<1e-8)side=cross(tangent,abs(tangent.z)<.9?vec3(0,0,1):vec3(0,1,0));
    sideEye=normalize(side);faceEye=normalize(cross(sideEye,tangent));
    if(dot(faceEye,view)<0)faceEye=-faceEye;
    float pixelWorld=2*max(.001,-(a.z+b.z)*.5)/(gl_ProjectionMatrix[1][1]*viewportHeight);
    tubeRadius=max(extrusion?.11:.025,pixelWorld*(extrusion?.85:.45));
    baseColor=lineColor[0].rgb;
    emitCorner(a,-1);emitCorner(a,1);emitCorner(b,-1);emitCorner(b,1);EndPrimitive();
}
)GLSL";
        const char *fragment = R"GLSL(#version 330 compatibility
in vec3 centerEye;
in float across;
flat in vec3 sideEye;
flat in vec3 faceEye;
flat in vec3 baseColor;
flat in float tubeRadius;
uniform float cameraDistance;
uniform float modelSpan;
out vec4 result;
void main(){
    float x=clamp(across,-1,1),z=sqrt(max(0,1-x*x));
    vec3 normal=normalize(sideEye*x+faceEye*z);
    vec3 surface=centerEye+tubeRadius*(sideEye*x+faceEye*z);
    vec3 light=normalize(vec3(-.45,.7,.65)),view=normalize(-surface);
    float diffuse=max(dot(normal,light),0);
    float rim=max(dot(normal,normalize(vec3(.8,-.2,.25))),0);
    float spec=pow(max(dot(normal,normalize(light+view)),0),32);
    vec3 lit=baseColor*(.20+.72*diffuse+.12*rim)+vec3(.32)*spec;
    float fog=clamp((length(surface)-cameraDistance+modelSpan*.5)/max(modelSpan*2,.01),0,.65);
    result=vec4(mix(lit,vec3(.035,.052,.078),fog),1);
    vec4 projected=gl_ProjectionMatrix*vec4(surface,1);
    gl_FragDepth=clamp(projected.z/projected.w*.5+.5,0,1);
}
)GLSL";
        program = createProgram();
        for (auto entry :
             {std::pair<GLenum, const char *>{GL_VERTEX_SHADER, vertex},
              {GL_GEOMETRY_SHADER, geometry},
              {GL_FRAGMENT_SHADER, fragment}}) {
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
            attachShader(program, shader);
            deleteShader(shader);
        }
        linkProgram(program);
        GLint ok = 0;
        getProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[4096]{};
            getProgramInfoLog(program, sizeof log, nullptr, log);
            throw std::runtime_error(std::string("Line shader link: ") + log);
        }
    }
    void begin(float height, float distance, float span, bool travel) {
        useProgram(program);
        uniform1f(getUniformLocation(program, "viewportHeight"), height);
        uniform1f(getUniformLocation(program, "cameraDistance"), distance);
        uniform1f(getUniformLocation(program, "modelSpan"), span);
        uniform1f(getUniformLocation(program, "showTravel"),
                  travel ? 1.f : 0.f);
    }
    void end() { useProgram(0); }
    void destroy() {
        deleteProgram(program);
        program = 0;
    }
};
