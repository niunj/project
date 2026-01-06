// Copyright (c) 2016-2018  Sundog Software, LLC. All rights reserved worldwide.

/**
\file StratocumulusPuff.h
*/

#include "Metaball.h"

namespace SilverLining
{
    class StratocumulusPuff : public Metaball
    {
    public:
        StratocumulusPuff(float alpha, unsigned char intensity, float randomNumber);

        virtual ~StratocumulusPuff() {}

        float GetBaseAlpha() const { return baseAlpha; }

        float GetAlphaMutiplier() const { return alphaMultiplier; }

        void SetAlphaMultiplier(float a) { alphaMultiplier = a; }

        unsigned char GetNoiseSample() const { return noiseSample; }

        void ComputeAlpha() {
            SetAlpha(baseAlpha * alphaMultiplier);
        }

        float GetRandom() const {
            return randomNumber;
        }

    private:
        float baseAlpha, alphaMultiplier;
        unsigned char noiseSample;
        float randomNumber;
    };
}