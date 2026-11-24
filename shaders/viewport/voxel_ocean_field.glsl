// Shared production wave/optical field. CPU stores the identical immutable
// harmonic table; phases are never advanced by renderer wall-clock time.
layout(set=0,binding=0) uniform Ocean {vec4 p[796];} ocean;
layout(set=0,binding=4) uniform sampler2D meanSeaMap;
vec3 oceanMean(vec2 xz){if(ocean.p[128].x<2.)return vec3(ocean.p[0].y,0,0);vec2 raw=(xz-ocean.p[127].xy)/ocean.p[127].zw;vec2 uv=clamp(raw,vec2(0),vec2(ocean.p[128].x-1.));ivec2 ij=min(ivec2(floor(uv)),ivec2(int(ocean.p[128].x)-2));vec2 f=uv-vec2(ij);float a=texelFetch(meanSeaMap,ij,0).r,b=texelFetch(meanSeaMap,ij+ivec2(1,0),0).r,c=texelFetch(meanSeaMap,ij+ivec2(0,1),0).r,d=texelFetch(meanSeaMap,ij+ivec2(1,1),0).r;return vec3(ocean.p[0].y+mix(mix(a,b,f.x),mix(c,d,f.x),f.y),(raw.x<0.||raw.x>ocean.p[128].x-1.)?0.:mix(b-a,d-c,f.y)/ocean.p[127].z,(raw.y<0.||raw.y>ocean.p[128].x-1.)?0.:mix(c-a,d-b,f.x)/ocean.p[127].w);}
// height, d(height)/dx, d(height)/dz, decaying aerated-wake coverage.
vec4 oceanWake(vec2 xz,bool foam){
    vec4 result=vec4(0);
    for(int i=0;i<int(ocean.p[50].z);++i){
        vec4 a=ocean.p[55+2*i],b=ocean.p[56+2*i],body=ocean.p[103+i];float age=ocean.p[0].x-a.z;
        if(age<=0.||age>=b.z)continue;
        vec2 q=xz-a.xy-ocean.p[50].xy*age;float R=3.*b.x,reach=max(.55*b.x+b.y*age+R,4.*b.x+body.z*.5+age);if(max(abs(q.x),abs(q.y))>reach)continue;float r=sqrt(dot(q,q)+.04*b.x*b.x),d=r-(.55*b.x+b.y*age);
        if(r>max(.55*b.x+b.y*age+R,4.*b.x+body.z*.5+age))continue;
        // Aerated water is deposited behind the hull and advects with the
        // current. It retains the emitter's direction after turns/stops.
        if(foam){
        float along=dot(q,body.xy),across=dot(q,vec2(-body.y,body.x));
        float ellipse=pow(along/(b.x*.9+body.z*.15),2.)+pow(across/(b.x*.28+age*.12),2.);
        if(ellipse<4.){float coverage=clamp(body.z*.16+a.w*5.,0.,.8),edge=max(0.,1.-ellipse*.25);float core=edge*edge*exp(-ellipse*1.2);float behind=max(0.,-along),spread=b.x*.20+behind*.12,filament=b.x*.11+age*.045;float ribbons=exp(-pow((abs(across)-spread)/filament,2.)*1.8)*exp(-pow(along/(b.x+body.z*.18),2.)*1.4);float streak=sin(along*5.1+across*9.7+float(i)*1.9)*sin(across*16.-along*2.3);float torn=clamp(.78+streak*.3,.25,1.);float aeration=ocean.p[128].y>.5?core:ocean.p[166].w>.5?(.35*core+.8*ribbons):(.25*core+.9*ribbons)*torn;result.w+=ocean.p[129+i].x*aeration;}
        }
        if(abs(d)>=R)continue;
        float A=body.w,u=1.-d*d/(R*R),E=u*u*u,Er=-6.*d/(R*R)*u*u,c=cos(b.w*d),sn=sin(b.w*d);
        float f=A*E*c,fr=A*(Er*c-b.w*E*sn);result.xyz+=vec3(f,fr*q/r);
        if(foam)result.w+=ocean.p[129+i].y*E*smoothstep(-.25,.8,c);
    }return result;
}

vec4 oceanField(vec2 xz){vec4 field=vec4(oceanMean(xz),0);for(int i=0;i<int(ocean.p[0].w);++i){vec4 w=ocean.p[18+i];vec2 t=ocean.p[34+i].xy;float phase=w.y*dot(w.zw,xz)-t.x*ocean.p[0].x+t.y;field.x+=w.x*cos(phase);field.yz-=w.x*w.y*w.zw*sin(phase);}return field+oceanWake(xz,false);}
// Resolve only screen-visible frequencies in the optical normal. This does
// not alter the canonical CPU/GPU wave field used by physics and queries.
vec4 oceanFilteredField(vec2 xz,float footprint,out float unresolvedSlopes){
    vec4 field=vec4(oceanMean(xz),0);
    unresolvedSlopes=0.;
    for(int i=0;i<int(ocean.p[0].w);++i){
        vec4 w=ocean.p[18+i];
        float weight=1.-smoothstep(.5,2.,w.y*footprint);
        float slope=w.x*w.y;
        unresolvedSlopes+=.5*slope*slope*(1.-weight*weight);
        if(weight<=0.)continue;
        vec2 t=ocean.p[34+i].xy;
        float phase=w.y*dot(w.zw,xz)-t.x*ocean.p[0].x+t.y;
        field.x+=w.x*cos(phase)*weight;
        field.yz-=slope*w.zw*sin(phase)*weight;
    }
    return field+oceanWake(xz,true);
}
float oceanWakeHeight(vec2 xz){float h=0.;for(int i=0;i<int(ocean.p[50].z);++i){vec4 a=ocean.p[55+2*i],b=ocean.p[56+2*i];float A=ocean.p[103+i].w;if(A<=0.)continue;float age=ocean.p[0].x-a.z;vec2 q=xz-a.xy-ocean.p[50].xy*age;float front=.55*b.x+b.y*age,R=3.*b.x;if(max(abs(q.x),abs(q.y))>front+R)continue;float r=sqrt(dot(q,q)+.04*b.x*b.x),d=r-front;if(abs(d)>=R)continue;float u=1.-d*d/(R*R);h+=A*u*u*u*cos(b.w*d);}return h;}
float oceanHeight(vec2 xz){if(ocean.p[128].y>.5)return oceanField(xz).x;float h=oceanMean(xz).x;for(int i=0;i<int(ocean.p[0].w);++i){vec4 w=ocean.p[18+i];vec2 t=ocean.p[34+i].xy;h+=w.x*cos(w.y*dot(w.zw,xz)-t.x*ocean.p[0].x+t.y);}return h+oceanWakeHeight(xz);}
float oceanMeshHeight(vec2 xz,float spacing){if(ocean.p[166].w>.5)return oceanHeight(xz);float h=oceanMean(xz).x;for(int i=0;i<int(ocean.p[0].w);++i){vec4 w=ocean.p[18+i];float weight=1.-smoothstep(.5,2.,w.y*spacing);if(weight<=0.)continue;vec2 t=ocean.p[34+i].xy;h+=w.x*cos(w.y*dot(w.zw,xz)-t.x*ocean.p[0].x+t.y)*weight;}return h+oceanWakeHeight(xz);}
vec3 oceanNormal(vec2 xz){vec2 g=oceanField(xz).yz;return normalize(vec3(-g.x,1,-g.y));}
vec2 oceanOpticalSurface(vec2 xz){float h=oceanMean(xz).x,curvature=0.;for(int i=0;i<int(ocean.p[0].w);++i){vec4 w=ocean.p[18+i];vec2 t=ocean.p[34+i].xy;float c=w.x*cos(w.y*dot(w.zw,xz)-t.x*ocean.p[0].x+t.y);h+=c;curvature+=c*w.y*w.y;}return vec2(h+oceanWakeHeight(xz),curvature);}
// Safeguarded Newton solve, normally converging in two or three iterations.
// CPU measurement rays retain their exact bracketed intersection path.
float oceanInterfaceParameter(vec3 eye,vec3 point,float eyeHeight,float pointHeight,bool wetEye){float lo=0.,hi=1.,u=clamp((eye.y-eyeHeight)/((eye.y-eyeHeight)-(point.y-pointHeight)),0.,1.);vec3 delta=point-eye;for(int i=0;i<4;++i){vec3 q=mix(eye,point,u);vec4 field=oceanField(q.xz);float f=q.y-field.x;if(abs(f)<.0001)return u;if((f<0.)==wetEye)lo=u;else hi=u;float derivative=delta.y-dot(field.yz,delta.xz);float next=u-f/(abs(derivative)>.00001?derivative:-.00001);u=next>lo&&next<hi?next:(lo+hi)*.5;}return u;}
vec3 waterTransmission(float length){return exp(-(ocean.p[6].rgb+ocean.p[7].rgb)*max(0.,length));}
vec3 waterAmbient(){vec3 c=ocean.p[6].rgb+ocean.p[7].rgb;return ocean.p[7].rgb/max(c,vec3(1e-5))*(ocean.p[53].rgb*1.8+ocean.p[52].rgb*.16)+ocean.p[54].rgb*.08;}
float waterCaustic(vec3 point){float depth=max(0.,oceanHeight(point.xz)-point.y);float curvature=0.;for(int i=0;i<int(ocean.p[0].w);++i){vec4 w=ocean.p[18+i];vec2 t=ocean.p[34+i].xy;curvature+=w.x*w.y*w.y*cos(w.y*dot(w.zw,point.xz)-t.x*ocean.p[0].x+t.y);}return clamp(1.+curvature*min(depth,8.)*.7,.45,2.3);}
vec3 waterInscatter(vec3 eye,vec3 direction){float depth=max(0.,ocean.p[51].y-eye.y);vec3 source=waterAmbient()*exp(-(ocean.p[6].rgb+ocean.p[7].rgb)*depth*.35);float forward=pow(max(0.,dot(direction,normalize(ocean.p[2].xyz))),8.);return source*(1.+.18*forward);}
vec3 waterTransport(vec3 radiance,vec3 point,float pointHeight){vec3 eye=ocean.p[1].xyz;bool wetEye=eye.y<ocean.p[51].y;if(ocean.p[128].y<.5&&!wetEye&&point.y>ocean.p[0].y+ocean.p[128].w+ocean.p[50].w)return radiance;float range=length(point-eye);bool wetPoint=point.y<(ocean.p[128].y>.5?oceanHeight(point.xz):pointHeight);float length=wetEye?range:0.;if(wetEye!=wetPoint){float crossing;if(ocean.p[128].y>.5){float a=0,b=1;for(int i=0;i<8;++i){float m=(a+b)*.5;vec3 q=mix(eye,point,m);bool wet=q.y<oceanHeight(q.xz);if(wet==wetEye)a=m;else b=m;}crossing=(a+b)*.5;}else crossing=oceanInterfaceParameter(eye,point,ocean.p[51].y,pointHeight,wetEye);length=range*(wetEye?crossing:1.-crossing);}if(!wetEye&&!wetPoint)return radiance;vec3 transmission=waterTransmission(length);vec3 source=waterInscatter(eye,normalize(point-eye));return radiance*transmission+source*(1.-transmission);}

vec3 displayWater(vec3 radiance){return 1.-exp(-max(radiance,vec3(0))*max(.01,ocean.p[1].w));}
