#pragma once

#pragma warning(push)
#pragma warning(disable: 4100) // 屏蔽当前作用域的 C4100 警告


#include <osg/Geode>
#include <osg/MatrixTransform>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Light>
#include <osg/LightSource>
#include <osg/PositionAttitudeTransform>
#include <osg/Texture2D>
#include <osg/ShapeDrawable>
#include <osg/TexEnv>
#include <osg/MatrixTransform>
#include <osg/Projection>
#include <osg/ShapeDrawable>
#include <osg/Array>
#include <osg/AnimationPath>
#include <osg/Point>
#include <osg/Object>
#include <osg/Notify>
#include <osg/CullFace>
#include <osg/BlendColor>
#include <osg/BlendFunc>
#include <osg/LineWidth>
#include <osg/LineStipple>
#include <osg/Transform>
#include <osg/io_utils>
#include <osg/Vec3>
#include <osg/Vec3d>
#include <osg/Vec4>
#include <osg/Matrixd>
#include <osg/Matrix>
#include <osg/Depth>
#include <osg/CameraView>

#include <osg/Image>
#include <osg/ComputeBoundsVisitor>


#include <osgDB/ReadFile>
#include <osgDB/WriteFile>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#include <osgText/Text>
#include <osgText/Font>

#include <osgFX/Scribe>


#include <osgShadow/ShadowedScene>

#include <osgUtil/Optimizer>

#include <osgUtil/IntersectionVisitor>

#include <osgGA/CameraManipulator>
#include <osgGA/StateSetManipulator>
#include <osgGA/AnimationPathManipulator>
#include <osgGA/GUIEventAdapter>
#include <osgGA/GUIActionAdapter>


#include <osgParticle/PrecipitationEffect>
#include <osgParticle/Particle>
#include <osgParticle/ParticleSystem>
#include <osgParticle/ParticleSystemUpdater>
#include <osgParticle/ModularEmitter>
#include <osgParticle/ModularProgram>
#include <osgParticle/RandomRateCounter>
#include <osgParticle/RadialShooter>
#include <osgParticle/AccelOperator>
#include <osgParticle/LinearInterpolator>
#include <osgParticle/SmokeEffect>
#include <osgParticle/FireEffect>

#include <osgWidget/Box>
#include <osgWidget/Label>


#include <osgViewer/Renderer>

#pragma warning(pop) // 恢复之前的警告设置

//#ifdef _DEBUG
//#pragma comment(lib,"osgEarthd.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "opengl32.lib")
//#else
//#pragma comment(lib,"osgEarth.lib")
//#pragma comment(lib, "user32.lib")
//#pragma comment(lib, "opengl32.lib")
//#endif //_debug

