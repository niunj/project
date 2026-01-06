// Copyright (c) 2004-2014  Sundog Software, LLC. All rights reserved worldwide.

#include "SilverLiningTypes.h"
#include "CloudGeneratorFactory.h"
#include "CloudGeneratorExponential.h"
#include "CloudGeneratorRandom.h"
#include "Configuration.h"
#include "Atmosphere.h"

using namespace SilverLining;
using namespace std;

CloudGenerator *CloudGeneratorFactory::Create(const SL_STRING& configBase)
{
    CloudGenerator *generator = 0;

    const char * type;
    if (Configuration::GetStringValue((configBase + "-generation-model").c_str(), type)) {
        string stype(type);

        if (stype == "random") {
            CloudGeneratorRandom *cgr = SL_NEW CloudGeneratorRandom();
            double beta, nu, minSize, maxSize;

            if (Configuration::GetDoubleValue((configBase + "-generation-beta").c_str(), beta)) {
                cgr->SetBeta(beta);
            }

            if (Configuration::GetDoubleValue((configBase + "-generation-nu").c_str(), nu)) {
                cgr->SetNu(nu);
            }

            if (Configuration::GetDoubleValue((configBase + "-generation-min-size").c_str(), minSize)) {
                minSize *= Atmosphere::GetUnitScale();
                cgr->SetMinimumSize(minSize);
            }

            if (Configuration::GetDoubleValue((configBase + "-generation-max-size").c_str(), maxSize)) {
                maxSize *= Atmosphere::GetUnitScale();
                cgr->SetMaximumSize(maxSize);
            }

            generator = cgr;
        } else if (stype == "plank-exponential") {
            CloudGeneratorExponential *cge = SL_NEW CloudGeneratorExponential();
            double alpha, chi, beta, nu, minSize, maxSize, bandwidth;
            if (Configuration::GetDoubleValue((configBase + "-generation-alpha").c_str(), alpha)) {
                alpha /= Atmosphere::GetUnitScale();
                cge->SetAlpha(alpha);
            }

            if (Configuration::GetDoubleValue((configBase + "-generation-chi").c_str(), chi)) {
                cge->SetChi(chi);
            }

            if (Configuration::GetDoubleValue((configBase + "-generation-beta").c_str(), beta)) {
                cge->SetBeta(beta);
            }

            if (Configuration::GetDoubleValue((configBase + "-generation-nu").c_str(), nu)) {
                cge->SetNu(nu);
            }

            if (Configuration::GetDoubleValue((configBase + "-generation-min-size").c_str(), minSize)) {
                minSize *= Atmosphere::GetUnitScale();
                cge->SetMinimumSize(minSize);
            }

            if (Configuration::GetDoubleValue((configBase + "-generation-max-size").c_str(), maxSize)) {
                maxSize *= Atmosphere::GetUnitScale();
                cge->SetMaximumSize(maxSize);
            }

            if (Configuration::GetDoubleValue((configBase + "-generation-bandwidth").c_str(), bandwidth)) {
                bandwidth *= Atmosphere::GetUnitScale();
                cge->SetBandwidth(bandwidth);
            }

            generator = cge;
        }
    }

    return generator;
}
