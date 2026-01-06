#pragma once

#include <vsg/commands/Command.h>

namespace SilverLining
{
    class Atmosphere;
}

class LayersContainer;
class SilverLiningGroup;

class SilverLiningCommand : public vsg::Inherit<vsg::Command, SilverLiningCommand>
{
public:
    SilverLiningCommand(SilverLining::Atmosphere* atmosphere, SilverLiningGroup* parent);
    virtual ~SilverLiningCommand();

public:
    void compile(vsg::Context& context) override;
    void record(vsg::CommandBuffer& cb) const override;

protected:
    
    void NextCloudLayer();

protected:
    SilverLining::Atmosphere* _atmosphere = nullptr;
    LayersContainer* _layersContainer = nullptr;
    SilverLiningGroup* _parent = nullptr;

    bool _compiled = false;
};