/* OpenSceneGraph example, osgshadow.
*
*  Permission is hereby granted, free of charge, to any person obtaining a copy
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is
*  furnished to do so, subject to the following conditions:
*
*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
*  THE SOFTWARE.
*/

/*
    Code based on stripped version of osg shadow example
    demonstrating integration of SilverLining cloud shadows with OSG shadows.

    Since this demo implements sun shadows we removed small scale models
    and use island scene model. Shadow techniques were limited to
    view dependent techniques apropriate for outdor terrain scenes:

    MinimalShadowMap
    MinimalCullBoundsShadowMap
    MinimalDrawBoundsShadowMap
    LightPerspectiveShadowMapVB
    LightPerspectiveShadowMapCB
    LightPerspectiveShadowMapDB

    ParallelSplitShadowMap technque is also very appropriate (if not best) for large
    terrain scenes. Since PSSM uses monolithic all in one shader we not show
    integration with this technique here, though.
    However, such integration can be done through class override and modification
    of PSSM shaders ie in similar way to apprach presented in this sample.
*/

#include <osg/ArgumentParser>
#include <osg/ComputeBoundsVisitor>
#include <osg/Texture2D>
#include <osg/ShapeDrawable>
#include <osg/MatrixTransform>
#include <osg/Geometry>

#include <osgGA/TrackballManipulator>
#include <osgGA/FlightManipulator>
#include <osgGA/DriveManipulator>
#include <osgGA/KeySwitchMatrixManipulator>
#include <osgGA/AnimationPathManipulator>
#include <osgGA/TerrainManipulator>
#include <osgGA/AnimationPathManipulator>
#include <osgGA/StateSetManipulator>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#include <osgShadow/ShadowedScene>
#include <osgShadow/LightSpacePerspectiveShadowMap>
#include <osgShadow/StandardShadowMap>

#include <osgDB/ReadFile>
#include <osgDB/WriteFile>

#include <iostream>

// for the model number four - island scene
#include "IslandScene.h"
#include "IntegrateSilverLining.h"

static int ReceivesShadowTraversalMask = 0x1;
static int CastsShadowTraversalMask = 0x2;

int main(int argc, char** argv)
{
    // use an ArgumentParser object to manage the program arguments.
    osg::ArgumentParser arguments(&argc, argv);

    // set up the usage document, in case we need to print out how to use this program.
    arguments.getApplicationUsage()->setDescription(arguments.getApplicationName() + " is the example which demonstrates using of GL_ARB_shadow extension implemented in osg::Texture class");
    arguments.getApplicationUsage()->setCommandLineUsage(arguments.getApplicationName());
    arguments.getApplicationUsage()->addCommandLineOption("-h or --help", "Display this information");

    arguments.getApplicationUsage()->addCommandLineOption("--castsShadowMask", "Override default castsShadowMask (default - 0x2)");
    arguments.getApplicationUsage()->addCommandLineOption("--receivesShadowMask", "Override default receivesShadowMask (default - 0x1)");

    arguments.getApplicationUsage()->addCommandLineOption("--lispsm", "Select LightSpacePerspectiveShadowMap implementation. (default) ");
    arguments.getApplicationUsage()->addCommandLineOption("--msm", "Select MinimalShadowMap implementation.");
    arguments.getApplicationUsage()->addCommandLineOption("--ViewBounds", "MSM, LiSPSM & TSM optimize shadow for view frustum (weakest option)");
    arguments.getApplicationUsage()->addCommandLineOption("--CullBounds", "MSM, LiSPSM & TSM optimize shadow for bounds of culled objects in view frustum (better option).");
    arguments.getApplicationUsage()->addCommandLineOption("--DrawBounds", "MSM, LiSPSM & TSM optimize shadow for bounds of predrawn pixels in view frustum (default).");
    arguments.getApplicationUsage()->addCommandLineOption("--mapres", "MSM, LiSPSM & TSM texture resolution.");
    arguments.getApplicationUsage()->addCommandLineOption("--maxFarDist", "MSM, LiSPSM & TSM max far distance to shadow.");
    arguments.getApplicationUsage()->addCommandLineOption("--moveVCamFactor", "MSM, LiSPSM & TSM move the virtual frustum behind the real camera, (also back ground object can cast shadow).");
    arguments.getApplicationUsage()->addCommandLineOption("--minLightMargin", "MSM, LiSPSM t& TSM the same as --moveVCamFactor.");

    arguments.getApplicationUsage()->addCommandLineOption("--two-sided", "Use two-sided stencil extension for shadow volumes.");
    arguments.getApplicationUsage()->addCommandLineOption("--two-pass", "Use two-pass stencil for shadow volumes.");

    // Our code doesn't handle multiple displays; you need to associate an Atmosphere with each context in
    // that case. So, we'll force a single window / context.

    // construct the viewer.
    osgViewer::Viewer viewer(arguments);

    viewer.setUpViewInWindow( 20, 30, 1024, 768 );

    // if user request help write it out to cout.
    if (arguments.read("-h") || arguments.read("--help")) {
        arguments.getApplicationUsage()->write(std::cout);
        return 1;
    }

    while (arguments.read("--castsShadowMask", CastsShadowTraversalMask ));
    while (arguments.read("--receivesShadowMask", ReceivesShadowTraversalMask ));

    if( 1 ) {
        osg::ref_ptr<osgGA::KeySwitchMatrixManipulator> keyswitchManipulator = new osgGA::KeySwitchMatrixManipulator;

        keyswitchManipulator->addMatrixManipulator( '1', "Trackball", new osgGA::TrackballManipulator() );
        keyswitchManipulator->addMatrixManipulator( '2', "Flight", new osgGA::FlightManipulator() );
        keyswitchManipulator->addMatrixManipulator( '3', "Drive", new osgGA::DriveManipulator() );
        keyswitchManipulator->addMatrixManipulator( '4', "Terrain", new osgGA::TerrainManipulator() );

        std::string pathfile;
        char keyForAnimationPath = '5';
        while (arguments.read("-p",pathfile)) {
            osgGA::AnimationPathManipulator* apm = new osgGA::AnimationPathManipulator(pathfile);
            if (apm || !apm->valid()) {
                unsigned int num = keyswitchManipulator->getNumMatrixManipulators();
                keyswitchManipulator->addMatrixManipulator( keyForAnimationPath, "Path", apm );
                keyswitchManipulator->selectMatrixManipulator(num);
                ++keyForAnimationPath;
            }
        }
        viewer.setCameraManipulator( keyswitchManipulator );
    }

    // add the state manipulator
    viewer.addEventHandler( new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()) );

    // add the thread model handler
    viewer.addEventHandler(new osgViewer::ThreadingHandler);

    // add the window size toggle handler
    viewer.addEventHandler(new osgViewer::WindowSizeHandler);

    // add the stats handler
    viewer.addEventHandler(new osgViewer::StatsHandler);

    // add the help handler
    viewer.addEventHandler(new osgViewer::HelpHandler(arguments.getApplicationUsage()));

    // add the record camera path handler
    viewer.addEventHandler(new osgViewer::RecordCameraPathHandler);

    // add the LOD Scale handler
    viewer.addEventHandler(new osgViewer::LODScaleHandler);

    osg::ref_ptr<osgShadow::ShadowedScene> shadowedScene = new osgShadow::ShadowedScene;

    shadowedScene->setReceivesShadowTraversalMask(ReceivesShadowTraversalMask);
    shadowedScene->setCastsShadowTraversalMask(CastsShadowTraversalMask);

    osg::ref_ptr<osgShadow::MinimalShadowMap> msm = new osgShadow::LightSpacePerspectiveShadowMapDB;
    if ( arguments.read("--lispsm") ) {
        if( arguments.read( "--ViewBounds" ) )
            msm = new osgShadow::LightSpacePerspectiveShadowMapVB;
        else if( arguments.read( "--CullBounds" ) )
            msm = new osgShadow::LightSpacePerspectiveShadowMapCB;
        else // if( arguments.read( "--DrawBounds" ) ) // default
            msm = new osgShadow::LightSpacePerspectiveShadowMapDB;
    } else if( arguments.read("--msm") ) {
        if( arguments.read( "--ViewBounds" ) )
            msm = new osgShadow::MinimalShadowMap;
        else if( arguments.read( "--CullBounds" ) )
            msm = new osgShadow::MinimalCullBoundsShadowMap;
        else // if( arguments.read( "--DrawBounds" ) ) // default
            msm = new osgShadow::MinimalDrawBoundsShadowMap;
    }

    if( msm ) { // Set common MSM & LISPSM arguments
        shadowedScene->setShadowTechnique( msm.get() );
        while( arguments.read("--debugHUD") )
            msm->setDebugDraw( true );

        float minLightMargin = 10.f;
        float maxFarPlane = 0;
        unsigned int texSize = 1024;
        unsigned int baseTexUnit = 0;
        unsigned int shadowTexUnit = 1;

        while ( arguments.read("--moveVCamFactor", minLightMargin ) );
        while ( arguments.read("--minLightMargin", minLightMargin ) );
        while ( arguments.read("--maxFarDist", maxFarPlane ) );
        while ( arguments.read("--mapres", texSize ));
        while ( arguments.read("--baseTextureUnit", baseTexUnit) );
        while ( arguments.read("--shadowTextureUnit", shadowTexUnit) );

        // Override default Shaders with ours adding Cloud shadows contirbution

        osg::Shader * shadowFragmentShader = new osg::Shader( osg::Shader::FRAGMENT,
                " // following expressions are auto modified - do not change them:      \n"
                " // gl_TexCoord[1]  1 - can be subsituted with other index             \n"
                "                                                                       \n"
                "#define CLOUD_SHADOW_COORDS 7                                          \n"
                "                                                                       \n"
                "uniform sampler2DShadow shadowTexture;                                 \n"
                "uniform sampler2D       cloudShadowTexture;                            \n"
                "uniform float           cloudShadowBlend;                              \n"
                "                                                                       \n"
                "float DynamicShadow( )                                                 \n"
                "{                                                                      \n"
                "    float shadow = shadow2DProj( shadowTexture, gl_TexCoord[1] ).r;    \n"
                "    float cloudShadow = texture2DProj( cloudShadowTexture,             \n"
                "                          gl_TexCoord[CLOUD_SHADOW_COORDS] ).r;        \n"
                "    cloudShadow =  mix(1.0, cloudShadow, cloudShadowBlend);            \n"
                "    return shadow * cloudShadow;                                       \n"
                "}                                                                      \n"
                                                            );

        osg::Shader * shadowVertexShader = new osg::Shader( osg::Shader::VERTEX,
                " // following expressions are auto modified - do not change them:      \n"
                " // gl_TexCoord[1]  1 - can be subsituted with other index             \n"
                " // gl_EyePlaneS[1] 1 - can be subsituted with other index             \n"
                " // gl_EyePlaneT[1] 1 - can be subsituted with other index             \n"
                " // gl_EyePlaneR[1] 1 - can be subsituted with other index             \n"
                " // gl_EyePlaneQ[1] 1 - can be subsituted with other index             \n"
                "                                                                       \n"
                "#define CLOUD_SHADOW_COORDS 7                                          \n"
                "#define CLOUD_SHADOW_TEXGEN_STAGE 7                                    \n"
                "#if ( 0 > CLOUD_SHADOW_TEXGEN_STAGE )                                  \n"
                "    uniform mat4 cloudShadowCoordMatrix;                               \n"
                "#endif                                                                 \n"
                "                                                                       \n"
                "void DynamicShadow( in vec4 ecPosition )                               \n"
                "{                                                                      \n"
                "    // generate coords for shadow mapping                              \n"
                "    gl_TexCoord[1].s = dot( ecPosition, gl_EyePlaneS[1] );             \n"
                "    gl_TexCoord[1].t = dot( ecPosition, gl_EyePlaneT[1] );             \n"
                "    gl_TexCoord[1].p = dot( ecPosition, gl_EyePlaneR[1] );             \n"
                "    gl_TexCoord[1].q = dot( ecPosition, gl_EyePlaneQ[1] );             \n"
                "                                                                       \n"
                "                                                                       \n"
                "#if ( 0 <= CLOUD_SHADOW_TEXGEN_STAGE )                                 \n"
                "    // compute cloud shadow texture coords                             \n"
                "    gl_TexCoord[CLOUD_SHADOW_COORDS].s =                               \n"
                "         dot( ecPosition, gl_EyePlaneS[CLOUD_SHADOW_TEXGEN_STAGE] );   \n"
                "    gl_TexCoord[CLOUD_SHADOW_COORDS].t =                               \n"
                "         dot( ecPosition, gl_EyePlaneT[CLOUD_SHADOW_TEXGEN_STAGE] );   \n"
                "    gl_TexCoord[CLOUD_SHADOW_COORDS].p =                               \n"
                "         dot( ecPosition, gl_EyePlaneR[CLOUD_SHADOW_TEXGEN_STAGE] );   \n"
                "    gl_TexCoord[CLOUD_SHADOW_COORDS].q =                               \n"
                "         dot( ecPosition, gl_EyePlaneQ[CLOUD_SHADOW_TEXGEN_STAGE] );   \n"
                "#else                                                                  \n"
                "    gl_TexCoord[CLOUD_SHADOW_COORDS]                                   \n"
                "                               = cloudShadowCoordMatrix * ecPosition;  \n"
                "#endif                                                                 \n"
                "}                                                                      \n"
                                                          );

        osg::Shader * mainVertexShader = new osg::Shader( osg::Shader::VERTEX,
                " // following expressions are auto modified - do not change them:      \n"
                " // gl_TexCoord[0]      0 - can be subsituted with other index         \n"
                " // gl_TextureMatrix[0] 0 - can be subsituted with other index         \n"
                " // gl_MultiTexCoord0   0 - can be subsituted with other index         \n"
                "                                                                       \n"
                "const int NumEnabledLights = 1;                                        \n"
                "                                                                       \n"
                "void DynamicShadow( in vec4 ecPosition );                              \n"
                "                                                                       \n"
                "varying vec4 colorAmbientEmissive;                                     \n"
                "                                                                       \n"
                "void SpotLight(in int i,                                               \n"
                "               in vec3 eye,                                            \n"
                "               in vec3 ecPosition3,                                    \n"
                "               in vec3 normal,                                         \n"
                "               inout vec4 ambient,                                     \n"
                "               inout vec4 diffuse,                                     \n"
                "               inout vec4 specular)                                    \n"
                "{                                                                      \n"
                "    float nDotVP;          // normal . light direction                 \n"
                "    float nDotHV;          // normal . light half vector               \n"
                "    float pf;              // power factor                             \n"
                "    float spotDot;         // cosine of angle between spotlight        \n"
                "    float spotAttenuation; // spotlight attenuation factor             \n"
                "    float attenuation;     // computed attenuation factor              \n"
                "    float d;               // distance from surface to light source    \n"
                "    vec3 VP;               // direction from surface to light position \n"
                "    vec3 halfVector;       // direction of maximum highlights          \n"
                "                                                                       \n"
                "    // Compute vector from surface to light position                   \n"
                "    VP = vec3(gl_LightSource[i].position) - ecPosition3;               \n"
                "                                                                       \n"
                "    // Compute distance between surface and light position             \n"
                "    d = length(VP);                                                    \n"
                "                                                                       \n"
                "    // Normalize the vector from surface to light position             \n"
                "    VP = normalize(VP);                                                \n"
                "                                                                       \n"
                "    // Compute attenuation                                             \n"
                "    attenuation = 1.0 / (gl_LightSource[i].constantAttenuation +       \n"
                "                         gl_LightSource[i].linearAttenuation * d +     \n"
                "                         gl_LightSource[i].quadraticAttenuation *d*d); \n"
                "                                                                       \n"
                "    // See if point on surface is inside cone of illumination          \n"
                "    spotDot = dot(-VP, normalize(gl_LightSource[i].spotDirection));    \n"
                "                                                                       \n"
                "    if (spotDot < gl_LightSource[i].spotCosCutoff)                     \n"
                "        spotAttenuation = 0.0; // light adds no contribution           \n"
                "    else                                                               \n"
                "        spotAttenuation = pow(spotDot, gl_LightSource[i].spotExponent);\n"
                "                                                                       \n"
                "    // Combine the spotlight and distance attenuation.                 \n"
                "    attenuation *= spotAttenuation;                                    \n"
                "                                                                       \n"
                "    halfVector = normalize(VP + eye);                                  \n"
                "                                                                       \n"
                "    nDotVP = max(0.0, dot(normal, VP));                                \n"
                "    nDotHV = max(0.0, dot(normal, halfVector));                        \n"
                "                                                                       \n"
                "    if (nDotVP == 0.0)                                                 \n"
                "        pf = 0.0;                                                      \n"
                "    else                                                               \n"
                "        pf = pow(nDotHV, gl_FrontMaterial.shininess);                  \n"
                "                                                                       \n"
                "    ambient  += gl_LightSource[i].ambient * attenuation;               \n"
                "    diffuse  += gl_LightSource[i].diffuse * nDotVP * attenuation;      \n"
                "    specular += gl_LightSource[i].specular * pf * attenuation;         \n"
                "}                                                                      \n"
                "                                                                       \n"
                "void PointLight(in int i,                                              \n"
                "                in vec3 eye,                                           \n"
                "                in vec3 ecPosition3,                                   \n"
                "                in vec3 normal,                                        \n"
                "                inout vec4 ambient,                                    \n"
                "                inout vec4 diffuse,                                    \n"
                "                inout vec4 specular)                                   \n"
                "{                                                                      \n"
                "    float nDotVP;      // normal . light direction                     \n"
                "    float nDotHV;      // normal . light half vector                   \n"
                "    float pf;          // power factor                                 \n"
                "    float attenuation; // computed attenuation factor                  \n"
                "    float d;           // distance from surface to light source        \n"
                "    vec3  VP;          // direction from surface to light position     \n"
                "    vec3  halfVector;  // direction of maximum highlights              \n"
                "                                                                       \n"
                "    // Compute vector from surface to light position                   \n"
                "    VP = vec3(gl_LightSource[i].position) - ecPosition3;               \n"
                "                                                                       \n"
                "    // Compute distance between surface and light position             \n"
                "    d = length(VP);                                                    \n"
                "                                                                       \n"
                "    // Normalize the vector from surface to light position             \n"
                "    VP = normalize(VP);                                                \n"
                "                                                                       \n"
                "    // Compute attenuation                                             \n"
                "    attenuation = 1.0 / (gl_LightSource[i].constantAttenuation +       \n"
                "                         gl_LightSource[i].linearAttenuation * d +     \n"
                "                         gl_LightSource[i].quadraticAttenuation * d*d);\n"
                "                                                                       \n"
                "    halfVector = normalize(VP + eye);                                  \n"
                "                                                                       \n"
                "    nDotVP = max(0.0, dot(normal, VP));                                \n"
                "    nDotHV = max(0.0, dot(normal, halfVector));                        \n"
                "                                                                       \n"
                "    if (nDotVP == 0.0)                                                 \n"
                "        pf = 0.0;                                                      \n"
                "    else                                                               \n"
                "        pf = pow(nDotHV, gl_FrontMaterial.shininess);                  \n"
                "                                                                       \n"
                "    ambient += gl_LightSource[i].ambient * attenuation;                \n"
                "    diffuse += gl_LightSource[i].diffuse * nDotVP * attenuation;       \n"
                "    specular += gl_LightSource[i].specular * pf * attenuation;         \n"
                "}                                                                      \n"
                "                                                                       \n"
                "void DirectionalLight(in int i,                                        \n"
                "                      in vec3 normal,                                  \n"
                "                      inout vec4 ambient,                              \n"
                "                      inout vec4 diffuse,                              \n"
                "                      inout vec4 specular)                             \n"
                "{                                                                      \n"
                "     float nDotVP;         // normal . light direction                 \n"
                "     float nDotHV;         // normal . light half vector               \n"
                "     float pf;             // power factor                             \n"
                "                                                                       \n"
                "     nDotVP = max(0.0, dot(normal,                                     \n"
                "                normalize(vec3(gl_LightSource[i].position))));         \n"
                "     nDotHV = max(0.0, dot(normal,                                     \n"
                "                      vec3(gl_LightSource[i].halfVector)));            \n"
                "                                                                       \n"
                "     if (nDotVP == 0.0)                                                \n"
                "         pf = 0.0;                                                     \n"
                "     else                                                              \n"
                "         pf = pow(nDotHV, gl_FrontMaterial.shininess);                 \n"
                "                                                                       \n"
                "     ambient  += gl_LightSource[i].ambient;                            \n"
                "     diffuse  += gl_LightSource[i].diffuse * nDotVP;                   \n"
                "     specular += gl_LightSource[i].specular * pf;                      \n"
                "}                                                                      \n"
                "                                                                       \n"
                "void main( )                                                           \n"
                "{                                                                      \n"
                "    // Transform vertex to clip space                                  \n"
                "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;            \n"
                "    vec3 normal = normalize( gl_NormalMatrix * gl_Normal );            \n"
                "                                                                       \n"
                "    vec4  ecPos  = gl_ModelViewMatrix * gl_Vertex;                     \n"
                "    float ecLen  = length( ecPos );                                    \n"
                "    vec3  ecPosition3 = ecPos.xyz / ecPos.w;                           \n"
                "                                                                       \n"
                "    vec3  eye = vec3( 0.0, 0.0, 1.0 );                                 \n"
                "    //vec3  eye = -normalize(ecPosition3);                             \n"
                "                                                                       \n"
                "    DynamicShadow( ecPos );                                            \n"
                "                                                                       \n"
                "     gl_TexCoord[0] = gl_TextureMatrix[0] * gl_MultiTexCoord0;         \n"
                "                                                                       \n"
                "    // Front Face lighting                                             \n"
                "                                                                       \n"
                "    // Clear the light intensity accumulators                          \n"
                "    vec4 amb  = vec4(0.0);                                             \n"
                "    vec4 diff = vec4(0.0);                                             \n"
                "    vec4 spec = vec4(0.0);                                             \n"
                "                                                                       \n"
                "    // Loop through enabled lights, compute contribution from each     \n"
                "    for (int i = 0; i < NumEnabledLights; i++)                         \n"
                "   {                                                                   \n"
                "       if (gl_LightSource[i].position.w == 0.0)                        \n"
                "           DirectionalLight(i, normal, amb, diff, spec);               \n"
                "       else if (gl_LightSource[i].spotCutoff == 180.0)                 \n"
                "           PointLight(i, eye, ecPosition3, normal, amb, diff, spec);   \n"
                "       else                                                            \n"
                "           SpotLight(i, eye, ecPosition3, normal, amb, diff, spec);    \n"
                "    }                                                                  \n"
                "                                                                       \n"
                "    colorAmbientEmissive = gl_FrontLightModelProduct.sceneColor +      \n"
                "                           amb * gl_FrontMaterial.ambient;             \n"
                "                                                                       \n"
                "    gl_FrontColor = colorAmbientEmissive +                             \n"
                "                    diff * gl_FrontMaterial.diffuse;                   \n"
                "                                                                       \n"
                "     gl_FrontSecondaryColor = vec4(spec*gl_FrontMaterial.specular);    \n"
                "                                                                       \n"
                "    gl_BackColor = gl_FrontColor;                                      \n"
                "    gl_BackSecondaryColor = gl_FrontSecondaryColor;                    \n"
                "                                                                       \n"
                "    gl_FogFragCoord = ecLen;                                           \n"
                "} \n" );


        // Simple shader to only visualize shadows
        osg::Shader * mainFragmentShader = new osg::Shader( osg::Shader::FRAGMENT,
                "float DynamicShadow( );                                                 \n"
                "                                                                        \n"
                "uniform sampler2D baseTexture;                                          \n"
                "                                                                        \n"
                "void main(void)                                                         \n"
                "{                                                                       \n"
                "  float shadow = DynamicShadow();                                       \n"
                "  vec4 color = vec4(shadow,shadow,shadow,1.0);	                        \n"
                "  gl_FragColor = color;                                                 \n"
                "} \n" );


        // Set shaders before setting Shadow Coord index
        // So that automatic search and replace on shadow coords work on new shaders
        msm->setShadowVertexShader( shadowVertexShader );
        msm->setShadowFragmentShader( shadowFragmentShader );
        msm->setMainVertexShader( mainVertexShader );
        msm->setMainFragmentShader( mainFragmentShader );

        msm->setMinLightMargin( minLightMargin );
        msm->setMaxFarPlane( maxFarPlane );
        msm->setTextureSize( osg::Vec2s( texSize, texSize ) );
        msm->setShadowTextureCoordIndex( shadowTexUnit );
        msm->setShadowTextureUnit( shadowTexUnit );
        msm->setBaseTextureCoordIndex( baseTexUnit );
        msm->setBaseTextureUnit( baseTexUnit );

        // Associate shadow with default viewer light
        // which will work as sun or moon
        msm->setLight( viewer.getLight() );
    }

    osg::ref_ptr<osg::Node> model = osgDB::readNodeFiles(arguments);
    if (model.valid()) {
        model->setNodeMask(CastsShadowTraversalMask | ReceivesShadowTraversalMask);
    } else {
        model = ModelFour::createModel(arguments);
        model->setNodeMask(CastsShadowTraversalMask | ReceivesShadowTraversalMask);
    }

    {
        // Set up camera to see shadows from above
        osg::ComputeBoundsVisitor cbv;
        model->accept( cbv );

        osg::BoundingBox bb = cbv.getBoundingBox();

        viewer.getCameraManipulator( )->
        setHomePosition( bb.center() + osg::Vec3( 2,2,0.75 ) * bb.radius(), bb.center(), osg::Z_AXIS );
    }


    shadowedScene->addChild(model.get());

    // Its important to make sure SilverLining drawables are not hooked up under ShadowedScene
    osg::Group * root = new osg::Group;
    viewer.setSceneData(root);

    root->addChild( shadowedScene.get() );

    integrateSilverLining(root, viewer);


#if 1
    // create the windows and run the threads.
    viewer.realize();

    while (!viewer.done()) {
#if 0
        osgShadow::MinimalShadowMap * msm = dynamic_cast<osgShadow::MinimalShadowMap*>( shadowedScene->getShadowTechnique() );

        if( msm ) {

            // If scene decorated by CoordinateSystemNode try to find localToWorld
            // and set modellingSpaceToWorld matrix to optimize scene bounds computation

            osg::NodePath np = viewer.getCoordinateSystemNodePath();
            if( !np.empty() ) {
                osg::CoordinateSystemNode * csn =
                    dynamic_cast<osg::CoordinateSystemNode *>( np.back() );

                if( csn ) {
                    osg::Vec3d pos =
                        viewer.getCameraManipulator()->getMatrix().getTrans();

                    msm->setModellingSpaceToWorldTransform
                    ( csn->computeLocalCoordinateFrame( pos ) );
                }
            }
        }

#endif
        viewer.frame();
    }
#else
    viewer.run();
#endif
    return 0;
}
