/*
SamplerState gTriLinearSamClamp {
Filter = MIN_MAG_MIP_LINEAR;
AddressU = CLAMP;
AddressV = CLAMP;
};
*/
SamplerState gTriLinearClampClamp :
register(s0);

/*
SamplerState gTriLinearSamClamp {
Filter = MIN_MAG_MIP_LINEAR;
AddressU = CLAMP;
AddressV = WRAP;
};
*/
SamplerState gTriLinearClampWrap :
register(s1);

/*
SamplerState gTriLinearSamWrap {
Filter = MIN_MAG_MIP_LINEAR;
AddressU = WRAP;
AddressV = WRAP;
};
*/
SamplerState gTriLinearWrapWrap :
register(s2);

/*
SamplerState gTriLinearSamWrapClamp {
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = WRAP;
    AddressV = CLAMP;
};
*/
SamplerState gTriLinearWrapClamp :
register(s3);

/*
SamplerState gNearestSamClamp {
Filter = MIN_MAG_MIP_POINT;
AddressU = WRAP;
AddressV = WRAP;
};
*/
SamplerState gNearestWrapWrap :
register(s4);

/*
SamplerState gNoiseSampler {
Filter = MIN_MAG_LINEAR_MIP_POINT;
AddressU = WRAP;
AddressV = WRAP;
AddressW = WRAP;
};
*/
SamplerState gBiLinearWrapWrap :
register(s5);

/*
SamplerState gCloudSampler {
Filter = MIN_MAG_LINEAR_MIP_POINT;
AddressU = WRAP;
AddressV = WRAP;
AddressW = CLAMP;
};
*/
SamplerState gBiLinearWrapWrapClamp :
register(s6);
