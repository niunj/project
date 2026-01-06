#ifndef MTE3DSERVICENEW_H
#define MTE3DSERVICENEW_H

#include <QObject>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QMap>
#include <QVector3D>

#include <osg/Node>
#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/Geode>
#include <osg/ShapeDrawable>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgUtil/Optimizer>
#include <osg/ComputeBoundsVisitor>
#include <osgGA/EventHandler>
#include <osgGA/GUIEventHandler>
#include <osgGA/GUIEventAdapter>
#include <osg/Image>
#include <osg/Vec3d>
#include <osg/Matrix>
#include <osg/Quat>
#include <locale>

class LocaleGuard
{
public:
    LocaleGuard();
    ~LocaleGuard();

private:
    std::locale savedLocale;
};

class Mte3DServiceNew : public QObject
{
    Q_OBJECT

public:
    explicit Mte3DServiceNew(QObject* parent = nullptr);
    ~Mte3DServiceNew();

    osg::ref_ptr<osg::Group> getSceneRoot() const { return _sceneRoot; }

    void setModelSpacing(float spacing) { _modelSpacing = spacing; }
    float getModelSpacing() const { return _modelSpacing; }

    void clearScene();

signals:
    void loadProgress(int current, int total, const QString& fileName);
    void loadCompleted(int count);
    void loadFailed(const QString& fileName, const QString& error);
    void saveCompleted(const QString& filePath, int modelCount);
    void saveFailed(const QString& filePath, const QString& error);

private:
    osg::ref_ptr<osg::Group> _sceneRoot;
    std::vector<osg::ref_ptr<osg::MatrixTransform>>  models;
    float _modelSpacing;
    float _currentOffset;
    float _groundSize;
    float _gridSize;
    float _axisSize;

    void addDefaultGeometry();

public:
    int addPlatform(const QString& modelPath, QVector3D position, QVector3D posture, float scale);
    int addPlatform(const QString& modelPath, QVector3D position, QVector3D posture, float scale, bool fast);
    void setPlatformMatrix(int platformID, QVector3D scale, QVector3D posture, QVector3D translation);
    void prepareSceneRoots();
    bool saveSceneToObj(const QString& filePath);

private:
    QMap<int, osg::ref_ptr<osg::MatrixTransform>> platformMatrixMap;
    QMap<int, osg::ref_ptr<osg::Node>> platformModelMap;
    QMap<int, osg::Vec3d> platformCenterMap;
    QMap<int, osg::Vec3d> platformScaleMap;
    QMap<int, osg::Vec3d> platformPostureMap;
    QMap<int, osg::Vec3d> platformTransMap;
    QMap<int, QString> platformPathMap;
    int _nextPlatformID;

    osg::Quat eulerHPRAnglesToQuat(double headingDeg, double pitchDeg, double rollDeg);
};

#endif // MTE3DSERVICENEW_H
