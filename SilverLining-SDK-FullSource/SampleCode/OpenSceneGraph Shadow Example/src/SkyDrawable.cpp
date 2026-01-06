// Copyright (c) 2008-2013 Sundog Software, LLC. All rights reserved worldwide.

#include "SkyDrawable.h"
#include "SilverLining.h"
#include "AtmosphereReference.h"

#include <GL/gl.h>
#include <GL/glu.h>

#define GL_GLEXT_PROTOTYPES 1
#include "glext.h"

#include <assert.h>

using namespace SilverLining;

SkyDrawable::SkyDrawable(osgViewer::Viewer* view)
    : osg::Drawable()
    , _view(view)
    , _skyboxSize(0)
    , _cloudShadowTexgen(0)
    , _cloudShadowTextureWhiteSubstitute(0)
    , _cloudShadowTextureStage(7)
    , _cloudShadowTexgenStage(7)
{
    setDataVariance(osg::Object::DYNAMIC);
    setUseVertexBufferObjects(false);
    setUseDisplayList(false);

    {
        // Create white fake texture for use if no cloud layer casting shadows is present
        osg::Image * image = new osg::Image;
        image->allocateImage( 1, 1, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE );
        image->data()[0] = 0xFF;

        _cloudShadowTextureWhiteSubstitute = new osg::Texture2D( image );
        _cloudShadowTextureWhiteSubstitute->setWrap(osg::Texture2D::WRAP_S,osg::Texture2D::REPEAT);
        _cloudShadowTextureWhiteSubstitute->setWrap(osg::Texture2D::WRAP_T,osg::Texture2D::REPEAT);
        _cloudShadowTextureWhiteSubstitute->setFilter(osg::Texture2D::MIN_FILTER,osg::Texture2D::NEAREST);
        _cloudShadowTextureWhiteSubstitute->setFilter(osg::Texture2D::MAG_FILTER,osg::Texture2D::NEAREST);
    }

    {
        // Create cloud shadow casting texgen
        _cloudShadowTexgen = new osg::TexGen();
        _cloudShadowTexgen->setMode( osg::TexGen::EYE_LINEAR );
    }

    _cloudShadowCoordMatrixUniform = new osg::Uniform( "cloudShadowCoordMatrix", osg::Matrix() );
    _view->getSceneData()->getOrCreateStateSet()->addUniform( _cloudShadowCoordMatrixUniform );

    _view->getSceneData()->getOrCreateStateSet()->addUniform
    ( new osg::Uniform( "cloudShadowTexture", 7 ) );

    _cloudShadowBlendUniform = new osg::Uniform( "cloudShadowBlend", 1.0f);
    _view->getSceneData()->getOrCreateStateSet()->addUniform( _cloudShadowBlendUniform );

    osg::Camera* camera = new osg::Camera;
    _view->getSceneData()->asGroup()->addChild(camera);

    osg::GraphicsContext::WindowingSystemInterface* wsi = osg::GraphicsContext::getWindowingSystemInterface();
    if (!wsi) {
        osg::notify(osg::NOTICE)<<"Error, no WindowSystemInterface available, cannot create windows."<<std::endl;
    }

    unsigned int width, height;
    wsi->getScreenResolution(osg::GraphicsContext::ScreenIdentifier(0), width, height);

    width = 1024;
    height = 1024;
    camera->setProjectionMatrix(osg::Matrix::ortho2D(0,width,0,height));
    camera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
    camera->setViewMatrix(osg::Matrix::identity());
    camera->setClearMask(GL_DEPTH_BUFFER_BIT);
    camera->setRenderOrder(osg::Camera::POST_RENDER);
    camera->setAllowEventFocus(false);

    osg::Geode* geode = new osg::Geode;
    camera->addChild(geode);

    if (1) {
        osg::Vec3Array* vertices = new osg::Vec3Array;
        vertices->push_back(osg::Vec3(100,100,-0.1));
        vertices->push_back(osg::Vec3(400,100,-0.1));
        vertices->push_back(osg::Vec3(400,400,-0.1));
        vertices->push_back(osg::Vec3(100,400,-0.1));

        osg::Geometry* geometry = _hud = new osg::Geometry;
        geometry->setVertexArray(vertices);

        osg::Vec3Array* normals = new osg::Vec3Array;
        normals->push_back(osg::Vec3(0.0f,0.0f,1.0f));
        geometry->setNormalArray(normals);
        geometry->setNormalBinding(osg::Geometry::BIND_OVERALL);

        osg::Vec2Array* uvs = new osg::Vec2Array;
        uvs->push_back(osg::Vec2(0,0));
        uvs->push_back(osg::Vec2(1,0));
        uvs->push_back(osg::Vec2(1,1));
        uvs->push_back(osg::Vec2(0,1));
        geometry->setTexCoordArray(0,uvs);

        osg::Vec4Array* colors = new osg::Vec4Array;
        colors->push_back(osg::Vec4(1.f,1.f,1.f,1.f));
        geometry->setColorArray(colors);
        geometry->setColorBinding(osg::Geometry::BIND_OVERALL);

        geometry->addPrimitiveSet(new osg::DrawArrays(GL_QUADS,0,4));

        geode->addDrawable(geometry);
    }



}

void SkyDrawable::setLighting(SilverLining::Atmosphere *atmosphere) const
{
    osg::Light *light = _view->getLight();
    osg::Vec4 ambient, diffuse;
    osg::Vec3 direction;

    if (atmosphere && light) {
        float ra, ga, ba, rd, gd, bd, x, y, z;
        atmosphere->GetAmbientColor(&ra, &ga, &ba);
        atmosphere->GetSunOrMoonColor(&rd, &gd, &bd);
        atmosphere->GetSunOrMoonPosition(&x, &y, &z);

        direction = osg::Vec3(x, y, z);
        // For shadow purpose use fraction of the ambient  otherwise shadows are hard to notice
        ambient = osg::Vec4(0.1 * ra, 0.1 * ga, 0.1 * ba, 1.0);
        diffuse = osg::Vec4(rd, gd, bd, 1.0);

        direction.normalize();

        light->setAmbient(ambient);
        light->setDiffuse(diffuse);
        light->setSpecular(osg::Vec4(0,0,0,1));
        light->setPosition(osg::Vec4(direction.x(), direction.y(), direction.z(), 0));
    }
}

void SkyDrawable::setShadow(SilverLining::Atmosphere *atmosphere, osg::RenderInfo & renderInfo ) const
{
    if (!atmosphere || !renderInfo.getState()) return;

    osg::State & state = *renderInfo.getState();

    osg::Matrix cloudShadowProjection;
    cloudShadowProjection.makeIdentity();


    void *texHandle;
    SilverLining::Matrix4 lightMVP;
    SilverLining::Matrix4 worldToShadowMapTexCoord;

    _cloudShadowBlendUniform->set(1.0f);

    if (atmosphere->GetShadowMap(texHandle, &lightMVP, &worldToShadowMapTexCoord, false, 1.0f)) {

        GLuint shadowMap = (GLuint)texHandle;

        double proj[16];
        worldToShadowMapTexCoord.ToArray(proj);
        cloudShadowProjection.set(proj);
        cloudShadowProjection = renderInfo.getCurrentCamera()->getInverseViewMatrix() * cloudShadowProjection;

        SkyDrawable* mutableThis = const_cast<SkyDrawable*>(this);
        mutableThis->_texture = new osg::Texture2D;

        osg::ref_ptr<osg::Texture::TextureObject> textureObject = new osg::Texture::TextureObject(_texture.get(),shadowMap,GL_TEXTURE_2D);
        textureObject->setAllocated();

        _texture->setTextureObject(renderInfo.getContextID(),textureObject.get());

        state.setActiveTextureUnit(_cloudShadowTextureStage);
        _texture->apply( state );

        // Tell state about our changes
        state.haveAppliedTextureAttribute(_cloudShadowTextureStage, _texture.get());

        _hud->getOrCreateStateSet()->setTextureAttributeAndModes(0,_texture.get());


    } else {
        state.setActiveTextureUnit(_cloudShadowTextureStage);
        _cloudShadowTextureWhiteSubstitute->apply( state );

        // Tell state about our changes
        state.haveAppliedTextureAttribute(_cloudShadowTextureStage, _cloudShadowTextureWhiteSubstitute);

    }

    _cloudShadowCoordMatrixUniform->set( cloudShadowProjection );

    _cloudShadowTexgen->setPlanesFromMatrix( cloudShadowProjection );

    // Now goes tricky part of the code !!!
    // We need to interact with OSG during render phase in delicate way and
    // not go out of sync with OpenGL states recorded by osg::State.

    // Change texture stage to proper one
    state.setActiveTextureUnit(_cloudShadowTexgenStage);

    // We set texgen with identity on OpenGL modelview matrix
    // since our TexGen was already premultiplied with inverse view.
    // This method minimizes precision errors as we used OSG double matrices.
    // If we had not premultiplied the texgen then we would need to set
    // view on OpenGL modelview matrix which would cause internal OpenGL
    // premutiplication of TexGen by inverse view using float matrices.
    // Such float computation may cause shadow texture jittering between
    // frames if scenes/terrains spanning large coordinate ranges are used.
    state.applyModelViewMatrix( NULL );

#if 1
    // Now apply our TexGen
    _cloudShadowTexgen->apply( state );

    state.applyTextureMode(_cloudShadowTexgenStage,GL_TEXTURE_GEN_S,osg::StateAttribute::ON);
    state.applyTextureMode(_cloudShadowTexgenStage,GL_TEXTURE_GEN_T,osg::StateAttribute::ON);
    state.applyTextureMode(_cloudShadowTexgenStage,GL_TEXTURE_GEN_R,osg::StateAttribute::ON);
    state.applyTextureMode(_cloudShadowTexgenStage,GL_TEXTURE_GEN_Q,osg::StateAttribute::ON);

    // Tell state about our changes
    state.haveAppliedTextureAttribute(_cloudShadowTexgenStage, _cloudShadowTexgen);

    // Set this TexGen as a global default
    state.setGlobalDefaultTextureAttribute(_cloudShadowTexgenStage, _cloudShadowTexgen);
    //state.setGlobalDefaultTextureAttribute(_cloudShadowTextureStage, _texture.get());
#endif
}


void SkyDrawable::initializeSilverLining(AtmosphereReference *ar) const
{
    if (ar && !ar->atmosphereInitialized) {
        ar->atmosphereInitialized = true; // only try once.
        SilverLining::Atmosphere *atmosphere = ar->atmosphere;

        if (atmosphere) {
            srand(1234); // constant random seed to ensure consistent clouds across windows

            // Update the path below to where you installed SilverLining's resources folder.
            const char *slPath = getenv("SILVERLINING_PATH");
            if (!slPath) {
                printf("Can't find SilverLining; set the SILVERLINING_PATH environment variable ");
                printf("to point to the directory containing the SDK.\n");
                exit(0);
            }
            std::string resPath(slPath);
            resPath += "\\Resources\\";

            int ret = atmosphere->Initialize(SilverLining::Atmosphere::OPENGL, resPath.c_str(),
                                             true, 0);
            if (ret != SilverLining::Atmosphere::E_NOERROR) {
                printf("SilverLining failed to initialize; error code %d.\n", ret);
                printf("Check that the path to the SilverLining installation directory is set properly ");
                printf("in SkyDrawable.cpp (in SkyDrawable::initializeSilverLining)\n");
                exit(0);
            }

            // Let SilverLining know which way is up. OSG usually has Z going up.
            atmosphere->SetUpVector(0, 0, 1);
            atmosphere->SetRightVector(1, 0, 0);

            // Set our location (change this to your own latitude and longitude)
            SilverLining::Location loc;
            loc.SetAltitude(0);
            loc.SetLatitude(10);
            loc.SetLongitude(-122);
            atmosphere->GetConditions()->SetLocation(loc);

            // Set the time to noon in PST
            SilverLining::LocalTime t;
            t.SetFromSystemTime();
            t.SetHour(12);
            t.SetTimeZone(PST);
            atmosphere->GetConditions()->SetTime(t);

            // Center the clouds around the camera's initial position
            osg::Vec3d pos = _view->getCameraManipulator()->getMatrix().getTrans();

            SilverLining::CloudLayer *cumulusCongestusLayer;
            cumulusCongestusLayer = SilverLining::CloudLayerFactory::Create(CUMULUS_CONGESTUS, *atmosphere);
            cumulusCongestusLayer->SetIsInfinite(false);
            cumulusCongestusLayer->SetBaseAltitude(4000);
            cumulusCongestusLayer->SetThickness(500);
            cumulusCongestusLayer->SetBaseLength(40000);
            cumulusCongestusLayer->SetBaseWidth(40000);
            cumulusCongestusLayer->SetDensity(0.3);

            // Note, we pass in X and -Y since this accepts "east" and "south" coordinates.
            cumulusCongestusLayer->SetLayerPosition(pos.x(), -pos.y());
            cumulusCongestusLayer->SeedClouds(*atmosphere);

            atmosphere->GetConditions()->AddCloudLayer(cumulusCongestusLayer);
            //atmosphere->GetConditions()->ClearWindVolumes();

            SilverLining::WindVolume windVolume;
            windVolume.SetWindSpeed(100);
            atmosphere->GetConditions()->SetWind(windVolume);
        }
    }
}

void SkyDrawable::drawImplementation(osg::RenderInfo& renderInfo) const
{
    SilverLining::Atmosphere *atmosphere = 0;

    AtmosphereReference *ar = dynamic_cast<AtmosphereReference *>(renderInfo.getCurrentCamera()->getUserData());
    if (ar) atmosphere = ar->atmosphere;

    renderInfo.getState()->disableAllVertexArrays();

    if (atmosphere) {
        initializeSilverLining(ar);

        osg::Matrix projMat = renderInfo.getState()->getProjectionMatrix();
        atmosphere->SetProjectionMatrix(projMat.ptr());
        osg::Matrix viewMat = renderInfo.getCurrentCamera()->getViewMatrix();
        atmosphere->SetCameraMatrix(viewMat.ptr());

        atmosphere->DrawSky(true, false, _skyboxSize);
        setLighting(atmosphere);
        setShadow(atmosphere, renderInfo);
    }

    renderInfo.getState()->dirtyAllVertexArrays();
}
