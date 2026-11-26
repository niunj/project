#version 450
layout(push_constant) uniform Camera { mat4 projection; mat4 modelView; } camera;
layout(std140,set=0,binding=1) uniform Lighting { vec4 sky; vec4 sun; vec4 sunDir; vec4 haze; } light;
layout(set=0,binding=2) uniform sampler3D turbulence;
layout(location=0) in vec3 eyeRay;
layout(location=1) flat in vec3 centreEye;
layout(location=2) flat in vec4 axisHalfLength;
layout(location=3) flat in vec4 centreRadius;
layout(location=4) flat in vec4 shape;
layout(location=5) flat in mat3 toChart;
layout(location=0) out vec4 color;

vec3 toUnit(vec3 p,vec3 axis,float r,float h) {
    float z=dot(p,axis);
    return (p-axis*z)/r+axis*z/h;
}

float turbulenceAt(vec3 p) {
    return textureLod(turbulence,p,0.).r;
}

float detailAt(vec3 p) {
    return .75*turbulenceAt(p)+.25*turbulenceAt(p*2.17+vec3(.61,.21,.33));
}

void main() {
    const float sceneEpsilon=.00001;
    vec3 ray=normalize(eyeRay),axis=axisHalfLength.xyz;
    float r=centreRadius.w,h=axisHalfLength.w;
    vec3 P=toUnit(-centreEye,axis,r,h), D=toUnit(ray,axis,r,h);
    float a=dot(D,D), b=dot(P,D), c=dot(P,P)-1.;
    float disc=b*b-a*c;
    if(disc<=0.)discard;
    float hit=sqrt(disc);
    float begin=max(.05,(-b-hit)/a), end=(-b+hit)/a;
    if(end-begin<sceneEpsilon)discard;
    float kind=shape.x, age=shape.z;
    bool launch=kind>6.5&&kind<7.5;
    bool jet=kind<1.5||launch, hot=kind>3.5&&kind<5.5;
    bool paleSmoke=kind<2.5||kind>7.5;
    float distanceM=.5*(begin+end);
    // An inexpensive four-sample integral is adequate for small distant puffs.
    float screenSpan=max(r,h)/max(1.,distanceM);
    int steps=jet?12:hot?9:screenSpan>.075?8:screenSpan>.018?6:4;
    float stepM=(end-begin)/float(steps);
    vec3 sunDirection=normalize(light.sunDir.xyz+vec3(.0001));
    float transmission=1., visibleEntry=end;
    vec3 gathered=vec3(0.);
    for(int i=0;i<steps;++i) {
        float t=begin+(float(i)+.5)*stepM;
        vec3 offset=ray*t-centreEye;
        vec3 q=toUnit(offset,axis,r,h);
        float axial=dot(q,axis);
        vec3 worldOffset=toChart*offset;
        float density;
        vec3 source;
        if(jet) {
            float d=.5+.5*axial;
            float radial=length(q-axis*axial);
            float axialWindow=smoothstep(.015,.085,d)*(1.-smoothstep(.54,.99,d));
            float flow=turbulenceAt(worldOffset*(launch?.19:.42)+vec3(shape.w*.15915,0.,0.));
            float cell=.58+.42*pow(abs(cos(d*17.2+.70*(flow-.5))),6.);
            float width=launch?mix(.35,.81,smoothstep(.05,.89,d))*(.82+.36*flow):
                mix(.80,.20,d)*(.79+.28*cell);
            float shell=1.-smoothstep(width*.25,width,radial);
            float core=exp(-radial*radial*(launch?8.:15.))*(launch?.72+.46*flow:cell);
            density=shell*axialWindow*shape.y*(.78+.44*flow)*(launch?3.9:3.35)/max(.25,r);
            vec3 edge=launch?vec3(.98,.20,.021):kind<.5?vec3(.46,.13,.091):vec3(.89,.106,.017);
            vec3 centre=kind<.5?vec3(.89,.91,1.):vec3(1.,.90,.62);
            float centreMix=clamp(core*(1.56-d*.92),0.,1.);
            source=mix(edge,centre,centreMix);
        } else if(kind>4.5&&kind<5.5) {
            density=pow(max(0.,1.-dot(q,q)),3.)*shape.y*3./r;
            source=vec3(1.,.86,.58);
        } else {
            // Chart-anchored wisps cross volume boundaries instead of restarting
            // a 2D texture on each particle. Advect that detail inside each lobe.
            vec3 world=centreRadius.xyz+worldOffset;
            vec3 localNoise=world*(paleSmoke?.093:.14) + vec3(.61,.30,.22)*shape.w + vec3(.12,-.16,.07)*age;
            float n=screenSpan>.028?detailAt(localNoise):turbulenceAt(localNoise);
            float envelope=1.-dot(q,q);
            float edge=smoothstep(.02,.56,envelope+(n-.51)*(paleSmoke?1.34:1.65));
            float curl=smoothstep(.32,.76,n+.17*envelope);
            if(hot) {
                float hotPocket=pow(curl,1.10)*max(0.,1.-age*.47);
                source=mix(vec3(.13,.016,.003),vec3(1.,.29,.026),hotPocket);
                source=mix(source,vec3(1.,.84,.46),smoothstep(.49,.83,hotPocket));
                density=edge*(.35+.65*curl)*shape.y*3.8/r;
            } else {
                vec3 towardSun=transpose(toChart)*sunDirection;
                vec3 shadowQ=toUnit(offset+towardSun*r*.57,axis,r,h);
                float block=smoothstep(.04,.85,1.-dot(shadowQ,shadowQ) + (turbulenceAt(localNoise+sunDirection*r*.037)-.5)*.84);
                float direct=exp(-block*(paleSmoke?1.55:3.25));
                vec3 tint=paleSmoke?vec3(.82,.81,.78):vec3(.119,.110,.102);
                if(kind>7.5&&kind<8.5)tint=mix(vec3(.81,.77,.69),vec3(.84,.85,.87),smoothstep(.05,.38,age));
                if(kind>8.5)tint=vec3(.86,.89,.93);
                vec3 lightIn=vec3(.021,.024,.030)+light.sky.rgb*(.29+.39*n)+light.sun.rgb*direct*.93;
                source=tint*lightIn;
                density=edge*(.30+.74*curl)*shape.y*(paleSmoke?1.78:2.85)/r;
                if(paleSmoke) {
                    float rolled=detailAt(world*.039+vec3(.3,-.2,.1)*age);
                    density*=smoothstep(.18,.69,rolled+.24*envelope);
                }
            }
        }
        float sampleAlpha=1.-exp(-max(0.,density)*stepM);
        if(sampleAlpha>.012&&visibleEntry==end)visibleEntry=t;
        gathered+=source*(sampleAlpha*transmission);
        transmission*=1.-sampleAlpha;
        if(transmission<.025)break;
    }
    float alpha=1.-transmission;
    if(alpha<.008)discard;
    float fog=1.-exp(-3.*distanceM/max(1500.,light.haze.a));
    color=vec4(mix(gathered,light.haze.rgb*alpha,fog*.68)*(1.-.67*fog),alpha*(1.-.67*fog));
    // Test the first appreciably dense point instead of the old centre plane.
    // No depth is written by this pipeline.
    vec4 clip=camera.projection*vec4(ray*visibleEntry,1.);
    gl_FragDepth=clamp(clip.z/clip.w,0.,1.);
}
