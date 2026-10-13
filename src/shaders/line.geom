#version 330 compatibility
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
