#version 330 compatibility
// Forward alpha as a categorical extrusion/travel flag; RGB is supplied by uniforms.
out vec4 lineColor;
void main(){gl_Position=gl_ModelViewMatrix*gl_Vertex;lineColor=gl_Color;}
