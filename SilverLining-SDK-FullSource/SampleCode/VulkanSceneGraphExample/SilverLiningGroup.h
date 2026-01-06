#pragma once

#include <vsg/nodes/Group.h>
#include <vsg/app/Window.h>

#include <vsg/vk/Device.h>

#include <vsg/maths/mat4.h>


namespace SilverLining
{
    class Atmosphere;
}

class SilverLiningGroup : public vsg::Inherit<vsg::Group, SilverLiningGroup>
{
public:
    SilverLiningGroup(vsg::ref_ptr<vsg::Window> window);
    virtual ~SilverLiningGroup();

public:
    void accept(vsg::RecordTraversal& rt) const override;

public:
    SilverLining::Atmosphere* atmosphere(void);
    int imageIndex(void) const;
protected:
    virtual void SetTimeAndLocation();
    virtual void AdvanceTime(long delta);
    virtual void setUpSlViewProjection(vsg::RecordTraversal& rt) const;

protected:
    SilverLining::Atmosphere* _atmosphere = nullptr;

    vsg::ref_ptr<vsg::Window> _window;
};