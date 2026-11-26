#version 450
layout(set=0,binding=0) uniform sampler2D artwork;
layout(set=0,binding=1,std140) uniform CabinLight {
    vec4 sky;
    vec4 sun;
    vec4 sunDirection;
} light;
layout(location=0) in vec3 localPosition;
layout(location=1) in vec3 surfaceNormal;
layout(location=2) in vec2 texcoord;
layout(location=3) in vec4 finish;
layout(location=0) out vec4 pixel;

float hashCell(vec3 p) {
    p=fract(p*vec3(.1031,.11369,.13787));
    p+=dot(p,p.yxz+19.19);
    return fract((p.x+p.y)*p.z);
}

float grain(vec3 p) {
    vec3 i=floor(p), f=fract(p);
    f=f*f*(3.-2.*f);
    return mix(
        mix(mix(hashCell(i),hashCell(i+vec3(1,0,0)),f.x),
            mix(hashCell(i+vec3(0,1,0)),hashCell(i+vec3(1,1,0)),f.x),f.y),
        mix(mix(hashCell(i+vec3(0,0,1)),hashCell(i+vec3(1,0,1)),f.x),
            mix(hashCell(i+vec3(0,1,1)),hashCell(i+1.),f.x),f.y),f.z);
}

void main() {
    vec3 n = normalize(surfaceNormal);
    vec3 toEye = normalize(-localPosition);
    if (dot(n, toEye) < 0.0) n = -n;
    float facing = clamp(dot(n, toEye), 0.0, 1.0);
    vec3 halfAngle = light.sunDirection.xyz + toEye;
    float shininess = clamp(dot(n, halfAngle * inversesqrt(max(dot(halfAngle,halfAngle),1e-6))), 0.0, 1.0);
    if (finish.a < .5) {
        float edge = pow(1.0-facing, 2.5);
        float reflection = pow(shininess, 72.0);
        pixel = vec4(vec3(.035,.057,.048) + light.sky.rgb*.20 + light.sun.rgb*reflection*.10,
                     finish.a + .085*edge + .075*reflection*light.sun.g);
        return;
    }

    float metal = smoothstep(.73,.97,finish.a);
    float elastomer = 1.0-smoothstep(.51,.58,finish.a);
    float photo = step(.001,abs(texcoord.x)+abs(texcoord.y));
    vec3 tex = texture(artwork,texcoord).rgb;
    vec3 base = tex * finish.rgb;
    // Brushed alloy has longer marks along the console. Paint and grip
    // elastomer have finer isotropic grain. Fade grain below pixel scale.
    vec3 grit = localPosition * mix(vec3(450),vec3(1080,950,142),metal);
    float footprint=max(length(dFdx(grit)),length(dFdy(grit)));
    float grainValue=grain(grit)-.5;
    float mottle=grain(localPosition*33.0)-.5;
    base *= 1.0 + (1.0-photo*.9) *
        (grainValue*mix(.40,.20,metal)*(1.-smoothstep(.5,2.2,footprint))+.20*mottle);
    float diffuse = max(dot(n,light.sunDirection.xyz),0.0);
    float overhead = .64 + .36 * max(n.y,0.0);
    vec3 indirect = vec3(.026,.028,.035) + light.sky.rgb * (1.64*overhead);
    vec3 direct = light.sun.rgb * (.63 * diffuse)*(1.-metal*.35);
    float fresnel=.025+.975*pow(1.-facing,5.0);
    float gloss=mix(12.,62.,metal)*(1.-.42*elastomer);
    vec3 spec = light.sun.rgb * diffuse * pow(shininess,gloss)
               * mix(vec3(fresnel*.055),base*.29+fresnel*.09,metal);
    vec3 reflectedSky = light.sky.rgb * (metal*.19 + fresnel*.025)*base;
    float lampFalloff = 1.0 - smoothstep(.10,1.18,length(localPosition-vec3(0,-.31,-1.04)));
    vec3 spill=vec3(.066,.078,.060)*light.sky.a*lampFalloff;
    // Only the light markings pick up the stronger rear illumination.
    // This avoids making the entire black faceplate glow.
    float value=dot(tex,vec3(.2126,.7152,.0722));
    float legend=photo*smoothstep(.22,.61,value);
    vec3 legendBacklight=vec3(.087,.117,.068)*light.sky.a*lampFalloff*legend;
    pixel = vec4(base*(indirect+direct+spill)+spec+reflectedSky+legendBacklight,1.0);
}
