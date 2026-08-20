#version 450
layout(location=0) in vec2 inLocal;
layout(location=1) in vec4 inColorOpacity;
layout(location=0) out vec4 outColor;
void main(){
    float exponent=-0.5*dot(inLocal,inLocal);
    float alpha=inColorOpacity.a*exp(exponent);
    if(alpha<1.0/255.0) discard;
    alpha=min(alpha,0.999);
    outColor=vec4(inColorOpacity.rgb,alpha);
}
