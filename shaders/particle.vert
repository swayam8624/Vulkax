#version 450
layout(location=0) in vec3 inPosition;
layout(location=1) in vec4 inColor;
layout(location=2) in float inPointSize;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform View { float worldScale; } view;
void main(){
    gl_Position=vec4(inPosition.x/view.worldScale, -inPosition.y/view.worldScale,
                     clamp(inPosition.z/view.worldScale,-1.0,1.0),1.0);
    gl_PointSize=max(inPointSize,1.0);
    outColor=inColor;
}
