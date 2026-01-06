// Copyright (c) 2004-2016 Sundog Software, LLC. All rights reserved worldwide.

/** \file CloudTypes.h
   \brief Defines the enumerated values of cloud layer types that may be passed
   to CloudLayerFactory::Create.
 */

#ifndef CLOUD_TYPES_H
#define CLOUD_TYPES_H

#ifdef SWIG
%module SilverLiningCloudTypes
%{
#include "CloudTypes.h"
%}
#endif

/*! Cloud types suitable for passing into CloudLayerFactory::Create */
enum CloudTypes
{
    CIRROCUMULUS,               /*!<  High planar cloud puffs */
    CIRROSTRATUS,               /*!<  High, thin uniform planar cloud */
    CIRRUS_FIBRATUS,            /*!<  High, thicker and fibrous clouds that signal changing weather */
    STRATUS,                    /*!<  Low clouds represented as a slab */
    CUMULUS_MEDIOCRIS,          /*!<  Low, puffy clouds on fair days */
    CUMULUS_CONGESTUS,          /*!<  Large cumulus clouds, built for performance. */
    CUMULUS_CONGESTUS_HI_RES,   /*!<  Large cumulus clouds represented with high-resolution puff textures. */
    CUMULONIMBUS_CAPPILATUS,    /*!<  Big storm clouds. */
    STRATOCUMULUS,              /*!<  Low, dense, puffy clouds with some sun breaks between them. Implemented with GPU ray-casting. */
    STRATOCUMULUS_PARTICLES,    /*!<  Low, dense, puffy clouds with some sun breaks between them. Implemented with billboard particles. */
    TOWERING_CUMULUS,           /*!<  Very large, tall cumulus clouds in the process of becoming a thunderstorm.*/
    SANDSTORM,                  /*!<  A "haboob" cloud of dust intended to be positioned at ground level. */
    NUM_CLOUD_TYPES             /*!<  Total number of cloud types. */
};

#endif
