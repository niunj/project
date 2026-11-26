#version 450
layout(location=0) in vec3 trailColor;
layout(location=1) in vec2 trailSample;
layout(location=0) out vec4 outColor;

void main() {
    float age=trailSample.y;
    float edge=1.-smoothstep(.33,1.,abs(trailSample.x));
    float alpha=edge*.79*(1.-smoothstep(.10,1.,age))*step(-.002,age);
    if(alpha<.012)discard;
    outColor=vec4(trailColor*alpha,alpha);
}
