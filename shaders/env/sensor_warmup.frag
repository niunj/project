#version 460
// Pipeline warmup never reads a measurement attachment before its first write.
layout(location=0) out vec4 color;
void main(){color=vec4(0.0);}
