#version 450
layout(location=0) in vec3 inClip;
layout(location=1) in vec2 inLocal;
layout(location=2) in vec4 inColorOpacity;
layout(location=0) out vec2 outLocal;
layout(location=1) out vec4 outColorOpacity;
void main(){
    gl_Position=vec4(inClip.x,-inClip.y,inClip.z,1.0);
    outLocal=inLocal;
    outColorOpacity=inColorOpacity;
}
