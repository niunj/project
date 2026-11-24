// Small display corrections to the existing molecular radiance. Preserve
// grazing and circumsolar light while retaining overhead sky contrast.
vec3 skyAirDisplay(vec3 radiance,float rayUp,float sunUp,float sunCosine,float altitude) {
    const vec3 lumaWeights=vec3(.2126,.7152,.0722);
    if(sunUp>0.) {
        float elevationWeight=smoothstep(.035,.63,rayUp);
        float clearSkyWeight=1.-smoothstep(.73,.994,sunCosine);
        float cruiseWeight=mix(.6,1.,smoothstep(800.,15500.,altitude));
        float strength=smoothstep(0.,.27,sunUp)*elevationWeight*clearSkyWeight*cruiseWeight;
        float luma=dot(radiance,lumaWeights);
        return max(vec3(0),radiance*(1.-.19*strength)+.12*strength*(radiance-luma));
    }
    float twilight=smoothstep(-.152,-.072,sunUp)*(1.-smoothstep(-.028,0.,sunUp));
    float strength=twilight*smoothstep(.19,.87,-sunCosine)*(1.-smoothstep(.14,.34,rayUp));
    if(strength<=0.)return radiance;

    // Project a representative low-troposphere scatter point into Earth's
    // shadow. Its local horizontal tilts with distance along the view ray.
    // This separates the anti-solar blue-grey shadow from the pink band of
    // still-illuminated backscattering without adding a fixed horizon stripe.
    const float earthRadius=6371000.;
    const float scatterRange=125000.;
    float scatterHeight=max(0.,altitude+scatterRange*rayUp+
        .5*scatterRange*scatterRange/earthRadius*(1.-rayUp*rayUp));
    float localSun=sunUp+(scatterRange/earthRadius)*(sunCosine-rayUp*sunUp);
    float clearance=localSun+sqrt(2.*scatterHeight/earthRadius);
    float sunlight=smoothstep(-.018,.018,clearance);
    float arch=sunlight*(1.-smoothstep(.012,.057,clearance));
    float luma=dot(radiance,lumaWeights);
    vec3 shadow=mix(radiance,luma*vec3(.45,.68,1.10),.72*strength*(1.-sunlight));
    // Light scattered from the still illuminated upper air also reaches the
    // shadowed column. The direct-light LUT alone can be very dark here.
    float slantWeight=inversesqrt(1.+5.7*max(0.,rayUp)*max(0.,rayUp));
    vec3 diffuse=mix(vec3(.009,.014,.026),vec3(.047,.025,.037),arch);
    return mix(shadow,luma*vec3(1.25,.87,.95),.38*strength*arch)+strength*slantWeight*diffuse;
}

// Low-altitude optical column not represented by the clear-air LUT. The
// unresolved veil provides background light while resolved clouds supply
// silhouettes and optical depth in their own pass.
// Surface-derived arguments work in either observer ENU or ECEF. The same
// cloudy illumination is visible directly and in the reflection probe.
vec2 skyWeatherTransmission(float rayUp,vec3 optics) {
    float above=max(rayUp,0.);
    float airMass=inversesqrt(.037+.963*above*above);
    float aerosol=exp(-optics.x*airMass);
    float cloud=exp(-optics.y*(2.9+3.4*optics.z)*sqrt(airMass));
    return vec2(aerosol,cloud);
}

vec3 skyWeatherRadiance(vec3 clearRadiance,float rayUp,vec2 viewCosines,vec3 bodies,
                        float precipitation,vec2 transmission) {
    const vec3 luma=vec3(.2126,.7152,.0722);
    float solar=bodies.x;
    float daylight=smoothstep(-9.,13.,solar);
    float lowSun=smoothstep(-8.,-1.5,solar)*(1.-smoothstep(1.,18.,solar));
    float above=max(rayUp,0.);
    float horizonLayer=exp(-3.8*above);
    float towardSun=max(viewCosines.x,0.);
    float angularFalloff=1.+18.*(1.-towardSun);
    float forwardLight=.52*pow(towardSun,3.)+.48/(angularFalloff*angularFalloff);
    float lunar=pow(clamp(bodies.z,0.,1.),2.)*smoothstep(-2.,20.,bodies.y)*
        (1.-smoothstep(-15.,-5.,solar));
    // Longer slant paths collect more low-level aerosol. The twilight tint
    // is directional and drops off above the low mist layer.
    vec3 daylightVeil=mix(vec3(.21,.27,.36),vec3(.35,.39,.43),horizonLayer)*daylight+
        vec3(.24,.075,.030)*lowSun*forwardLight*horizonLayer;
    vec3 moonVeil=vec3(.014,.017,.023)*lunar*(.25+.75*pow(max(viewCosines.y,0.),10.));
    vec3 haze=clearRadiance*transmission.x+(daylightVeil+moonVeil)*(1.-transmission.x);
    // Overcast sky is brightest toward the zenith. Keep precipitation dark
    // and let the sign of the solar elevation drive the night transition.
    float overcastSky=(.115+.045*dot(daylightVeil,luma))*daylight;
    overcastSky*=mix(.46,1.,sqrt(above))*(1.-.48*precipitation);
    vec3 deck=vec3(.91,.97,1.05)*overcastSky+moonVeil*.15+vec3(.0002,.00035,.00055);
    return haze*transmission.y+deck*(1.-transmission.y);
}
