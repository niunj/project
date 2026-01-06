// Copyright (c) 2016-2018 Sundog Software LLC. All rights reserved worldwide.

#include "StratocumulusPuff.h"

namespace SilverLining
{
StratocumulusPuff::StratocumulusPuff(float alpha, unsigned char intensity, float _randomNumber) : baseAlpha(alpha), alphaMultiplier(1.0), noiseSample(intensity), Metaball()
{
    randomNumber = _randomNumber;
}
}