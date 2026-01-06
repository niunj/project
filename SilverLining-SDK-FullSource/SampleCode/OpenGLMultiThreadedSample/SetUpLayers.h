// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include <vector>

//! forward declaration
namespace SilverLining
{
    class CloudLayer;
    class Atmosphere;
    class ThreadCameraStreamData;
}

namespace Sample
{
    class LayersContainer
    {
    public:
        //! constructor
        LayersContainer();

        //! destructor
        virtual ~LayersContainer();

    public:
        //! setup the each cloud layer type
        //!     -if created is true, a new layer is created.
        //!
        //!     -if create is false, the layer is not created, but is set up the same for the passed in tcsData
        //!
        //!     This is needed with multiple tcsData because each layer is created once and then each
        //!     layer needs to be 'initialized' with each tcs data we wish to render it with (in our case 3)
        //! atmosphere is where the layer gets added to

        //! set up cirrus fibratus clouds
        virtual void SetupCirrusFibratusClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData);

        //! set up cumulus congestus clouds
        virtual void SetupCumulusCongestusClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData);

        //! set up stratus clouds
        virtual void SetupStratusClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData);

        //! set up cumulonimbus clouds
        virtual void SetupCumulonimbusClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData);

        //! set up cumuslu mediocris clouds
        virtual void SetupCumulusMediocrisClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData);

        //! set up stratocumulus clouds
        virtual void SetupStratocumulusClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData);

        //! set up stratocumulus clouds (represented as particles)
        virtual void SetupStratocumulusParticles(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData);

        //! set up cirro cumulus clouds
        virtual void SetupCirroCumulusClouds(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData);

        //! set up sandstrom layer
        virtual void SetupSandstorm(SilverLining::Atmosphere* atmosphere, bool create, SilverLining::ThreadCameraStreamData* tcsData);

        virtual const std::vector<SilverLining::CloudLayer*>& GetLayers(void) const { return myLayers; }

        virtual void EraseLayerEntry(int index);

    private:
        int myCirrusFibratusCloudsIndex;
        int myCumulusCongestusCloudsIndex;
        int myStratusCloudsIndex;
        int myCumulonimbusCloudsIndex;
        int myCumulusMediocrisCloudsIndex;
        int myStratocumulusCloudsIndex;
        int myStratocumulusParticlesIndex;
        int myCirroCumulusCloudsIndex;
        int mySandstormIndex;

        std::vector<SilverLining::CloudLayer*> myLayers;
    };
}

