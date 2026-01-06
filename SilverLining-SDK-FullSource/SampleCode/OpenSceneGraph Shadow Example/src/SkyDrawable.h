// Copyright (c) 2008-2013 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include <osg/Drawable>
#include <osgViewer/Viewer>
#include <osg/TexGen>
#include <osg/Texture2D>
#include "SilverLining.h"

class AtmosphereReference;

class SkyDrawable : public osg::Drawable
{
public:
	SkyDrawable(osgViewer::Viewer* view = NULL);

    virtual bool isSameKindAs(const Object* obj) const {
        return dynamic_cast<const SkyDrawable*>(obj)!=NULL;
    }
    virtual Object* cloneType() const {
        return new SkyDrawable();
    }
    virtual Object* clone(const osg::CopyOp& copyop) const {
        return new SkyDrawable();
    }

	void setSkyboxSize(double size) {_skyboxSize = size;}

    virtual void drawImplementation(osg::RenderInfo& renderInfo) const;

protected:
    void initializeOsg(); 
    void initializeSilverLining(AtmosphereReference *ar) const;
	void setLighting(SilverLining::Atmosphere *atm) const;
    void setShadow(SilverLining::Atmosphere *atm, osg::RenderInfo & renderInfo ) const;
    
    osg::ref_ptr< osg::TexGen >      _cloudShadowTexgen;
    osg::ref_ptr< osg::Texture2D >   _cloudShadowTextureWhiteSubstitute;
    int                              _cloudShadowTextureStage;
    int                              _cloudShadowTexgenStage;
    osg::ref_ptr< osg::Uniform >     _cloudShadowCoordMatrixUniform;
    osg::ref_ptr< osg::Uniform >     _cloudShadowBlendUniform;
	 osg::ref_ptr<osg::Texture2D>			_texture;
	 osg::ref_ptr<osg::Geometry>				_hud;

    osgViewer::Viewer* _view;
	double _skyboxSize;
};
