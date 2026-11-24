"""Execute production path integration against an analytic homogeneous medium.

Only density and lighting are replaced; integration and clipping are extracted
verbatim so this test cannot accidentally exercise a stale CPU transcription.
"""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text()

def function(name):
    start = re.search(r"^(?:bool|void) " + name + r"\(", source, re.M).start()
    begin = source.index("{", start)
    depth = 1
    end = begin + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

prefix = """#version 460
layout(local_size_x=16) in;
layout(set=0,binding=0) buffer Result { vec2 result[]; };
struct Band { vec4 band2; vec4 band3; };
const Band bb = Band(vec4(0,0,100,1), vec4(200,1,1,0));
struct Env { vec4 env; vec4 env2; };
const Env ub = Env(vec4(0),vec4(0,1,0,0));
const float PI=3.14159265359;
float cloudDensity(vec3 p,float d) { return 1.0; }
float cloudExtPerKm() { return 1.0; }
float cloudSunOD(vec3 p) { return 0.0; }
float cloudMultiScatter(float d) { return 1.0; }
"""
main = """
void main() {
    uint i=gl_GlobalInvocationID.x;
    int counts[4]=int[4](10,12,16,24);
    float ends[4]=float[4](50.0,150.0,1000.0,50.0);
    uint scenario=i/4;
    float tau, light;
    vec3 pos=scenario==3 ? vec3(0,150,0) : vec3(0);
    vec3 dir=scenario==3 ? vec3(1,0,0) : vec3(0,1,0);
    cloudMarch(pos,dir,counts[i%4],ends[scenario],tau,light);
    result[i]=vec2(tau,light);
}
"""
pathlib.Path(sys.argv[2]).write_text(prefix + function("cloudSlab") + "\n" + function("cloudMarch") + main)
