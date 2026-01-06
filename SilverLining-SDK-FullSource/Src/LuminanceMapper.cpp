// Copyright (c) 2004-2008  Sundog Software, LLC. All rights reserved worldwide.

#include "LuminanceMapper.h"
#include "Vector3.h"
#include "Configuration.h"
#include "Atmosphere.h"

#if defined(__INTEL_COMPILER)
#include <mathimf.h>
#else
#include <math.h>
#endif

using namespace SilverLining;

LuminanceMapper::LuminanceMapper()
    : Ldmax(100.0)
    , LsavgC(100.0)
    , LsavgR(100.0)
    , k(1.0)
    , mR(1.0)
    , mC(1.0)
    , disableToneMapping(false)
{
    scotopicX = 0.3, scotopicY = 0.3, scotopicZ = 0.4;
    Configuration::GetDoubleValue("scotopic-x", scotopicX);
    Configuration::GetDoubleValue("scotopic-y", scotopicY);
    Configuration::GetDoubleValue("scotopic-z", scotopicZ);
}
LuminanceMapper::~LuminanceMapper()
{

}

void LuminanceMapper::SetMaxDisplayLuminance(double nits)
{
    if (nits > 0) {
        Ldmax = nits;
    }
}

void LuminanceMapper::SetSceneLogAvg(double rodNits, double coneNits)
{
    double brightness = 0.8;
    Configuration::GetDoubleValue("brightness", brightness);

    if (rodNits < 0) rodNits = 0;
    if (coneNits < 0) coneNits = 0;

    LsavgR = rodNits / brightness;
    LsavgC = coneNits / brightness;
    ComputeScaleFactors();
}

double LuminanceMapper::GetBurnoutLuminance() const
{
    return Ldmax / (mC + k * mR);
}

void LuminanceMapper::DurandMapperXYZ(Vector3 *XYZ) const
{
    if (!(disableToneMapping || Atmosphere::GetHDREnabled())) {
        double R = (XYZ->x) * -.702 + (XYZ->y) * 1.039 + (XYZ->z) * 0.433;

        if (R < 0) R = 0;

        Vector3 scotopic(R * scotopicX, R * scotopicY, R * scotopicZ);

        *XYZ = ((*XYZ) * (1 - k) * mC + scotopic * (k * mR)) * (1.0 / Ldmax);

    }

}

void LuminanceMapper::DurandMapper(double *x, double *y, double *Y) const
{
    if (!(disableToneMapping || Atmosphere::GetHDREnabled()) && ((*y) > 0)) {
        // From Durand
        //const double xBlue = 0.3;
        //const double yBlue = 0.3;

        double X = (*x) * ((*Y) / (*y));
        double Z = (1.0 - (*x) - (*y)) * ((*Y) / (*y));
        double R = X * -.702 + (*Y) * 1.039 + Z * 0.433;

        if (R < 0) R = 0;

        // Straight Ferwerda tone mapping - gets us to normalized luminance at estimated
        // display adaptation luminance
        double Ldp = (*Y) * mC;
        double Lds = (R) *mR;

        *Y = Ldp + k * Lds;

        *Y /= Ldmax;

        // Durand's blue-shift for scotopic vision
        //*x = (*x) * (1-k) + k * xBlue;
        //*y = (*y) * (1-k) + k * yBlue;
    }
}

static double RodThreshold(double LaR)
{
    double logLaR = log(LaR);

    double logEpsR;

    if (logLaR <= -3.94) {
        logEpsR = -2.86;
    } else if (logLaR >= -1.44) {
        logEpsR = logLaR - 0.395;
    } else {
        logEpsR = pow(0.405 * logLaR + 1.6, 2.18) - 2.86;
    }

    return exp(logEpsR);
}

static double ConeThreshold(double LaC)
{
    double logLaC = log(LaC);
    double logEpsC;

    if (logLaC <= -2.6) {
        logEpsC = -0.72;
    } else if (logLaC >= 1.9) {
        logEpsC = logLaC - 1.255;
    } else {
        logEpsC = pow(0.249 * logLaC + 0.65, 2.7) - 0.72;
    }

    return exp(logEpsC);
}

void LuminanceMapper::ComputeScaleFactors()
{
    /** Ferwerda operator **/
    double LdaC, LwaR, LwaC;

    Ldmax = 100.0;

    LdaC = Ldmax;

    LwaR = LsavgR;
    LwaC = LsavgC;

    double displayThreshold = ConeThreshold(LdaC);

    mR = displayThreshold / RodThreshold(LwaR);
    mC = displayThreshold / ConeThreshold(LwaC);

    const double sigma = 100.0;
    k = (sigma - 0.25 * LwaR) / (sigma + LwaR);
    if (k < 0) k = 0;
}
