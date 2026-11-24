// Shared voxel matter and physical extinction for spectral and raster sensors.
#define CLOUD_DESCRIPTOR_SET 2
#include "../viewport/planet_cloud_shadow.glsl"
layout(set=2,binding=0) uniform CloudSceneTransform {mat4 localToECEF;} cloudScene;
layout(set=2,binding=1) uniform CloudSceneWeather {vec4 advection,properties;} cloudWeather;
vec3 cloudWorldPoint(vec3 p){return (cloudScene.localToECEF*vec4(p,1.)).xyz;}
float voxelSensorExtinction(vec3 p) {
    if(cloudWeather.properties.x<=0.)return 0.;
    return cloudExtinctionPerMetre(cloudDensityAt(cloudWorldPoint(p),cloudWeather.advection,
        cloudWeather.properties,32.),cloudWeather.properties.y);
}
vec2 voxelCloudInterval(vec3 origin,vec3 direction,float height) {
    vec3 scale=1./vec3(CLOUD_A+height,CLOUD_A+height,CLOUD_B+height);
    vec3 e=origin*scale,d=direction*scale;
    float a=dot(d,d),b=dot(e,d),c=dot(e,e)-1.,det=b*b-a*c;
    if(det<=0.)return vec2(-1.);
    float root=sqrt(det);return vec2((-b-root)/a,(-b+root)/a);
}
bool voxelSensorSlab(vec3 pos,vec3 dir,out float t0,out float t1) {
    t0=0.;t1=-1.;if(cloudWeather.properties.x<=0.)return false;
    vec3 e=cloudWorldPoint(pos),d=mat3(cloudScene.localToECEF)*dir;
    vec2 outer=voxelCloudInterval(e,d,cloudWeather.properties.w);
    vec2 inner=voxelCloudInterval(e,d,cloudWeather.properties.z);
    t0=max(outer.x,0.);t1=outer.y;
    if(inner.y>t0 && inner.x<t1) {
        if(inner.x>t0)t1=min(t1,inner.x);else t0=max(t0,inner.y);
    }
    vec2 ground=voxelCloudInterval(e,d,0.);
    if(ground.x>0.)t1=min(t1,ground.x);
    return t1>t0;
}
float voxelSensorSunOD(vec3 p,vec3 light) {
    vec3 direction=mat3(cloudScene.localToECEF)*light;
    return -log(max(cloudVisibilityFromPoint(cloudWorldPoint(p),direction,
        cloudWeather.advection,cloudWeather.properties,32.),1e-7));
}
