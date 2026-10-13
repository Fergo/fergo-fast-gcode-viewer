#version 330 compatibility
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
