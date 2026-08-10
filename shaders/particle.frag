#version 450
layout(location=0) in vec4 inColor;
layout(location=0) out vec4 outColor;
void main(){
    vec2 d=gl_PointCoord*2.0-1.0;
    float r2=dot(d,d);
    if(r2>1.0) discard;
    float lighting=0.45+0.55*sqrt(max(0.0,1.0-r2));
    outColor=vec4(inColor.rgb*lighting,inColor.a);
}
