// Longitude wraps; elevation is linear over the upper hemisphere. Direction
// is reconstructed per fragment so the hemisphere is not a triangle blend.
vec3 reflectionDirection(vec2 uv,vec3 eye){
    vec3 up=earthAirUp(eye),east=normalize(vec3(-eye.y,eye.x,0)),north=cross(up,east);
    float az=uv.x*6.28318530718,el=uv.y*1.57079632679;
    return normalize((east*cos(az)+north*sin(az))*cos(el)+up*sin(el));
}
