#ifndef READWRITEFILE_H
#define READWRITEFILE_H

#include <QString>
#include <QFile>
#include <QVector3D>
#include <QVector>
#include <QMap>
#include <map>
#include <QTextCodec>
#include <set>
#include "MteStructDef.h"


//struct SceneSaveData
//{
//    QString scenePath;
//    SceneInfoStru sceneInfo;
//    QVector<double> weatherData;
//    QMap<SensorType, InfraredParameters> sensorInfoMap;
//    int timeValue;
//    osg::Vec3d cameraPos;
//    osg::Vec3d cameraAngle;
//    int sensorGain;
//    QVector<double> opticalData;
//    QVector<double> detectorData;
//    QVector<double> electricData;
//    SceneSaveData() = default;
//};

class ReadWriteFile
{
private:
    ReadWriteFile();
    // 禁用拷贝构造函数
    ReadWriteFile(const ReadWriteFile&) = delete;
    // 禁用赋值运算符
    ReadWriteFile& operator=(const ReadWriteFile&) = delete;
public://读取模型
    static ReadWriteFile& getInstance();
    // void readModel(const QString& strModel);

public://读取着色器
    std::string       loadShader(const std::string& filePath);


public://读取轨迹文件
    QVector<ModelTrackData> readTrackFile(QString strTrackFile);
    void createMultiCSVTrack(QString strPath,QVector<QVector<ModelTrackData>>trackData);

public://读取配置文库
    QString readConfigPath(const QString& strPath);

public://材料温度曲线
    std::map<int, std::vector<double>> getAllMaterials();

};

#endif // READWRITEFILE_H
