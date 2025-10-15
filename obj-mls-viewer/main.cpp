#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QStringList>
#include <QVBoxLayout>

// 添加ObjViewerWidget头文件
#include "objviewerwidget.h"
#include "mainwindow.h"


// 收集指定目录及其子目录下的所有OBJ文件
void collectObjFiles(const QString &path, QMap<QString, QString> &objFilesMap) {
    QDir dir(path);

    // 获取当前目录下的所有文件
    QFileInfoList fileList = dir.entryInfoList(QDir::Files | QDir::Readable);
    foreach (const QFileInfo &fileInfo, fileList) {
        if (fileInfo.suffix().toLower() == "obj") {
            QString fileName = fileInfo.fileName();
            QString absolutePath = fileInfo.absoluteFilePath();
            objFilesMap[fileName] = absolutePath;
        }
    }

    // 递归处理子目录
    QFileInfoList dirList = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    foreach (const QFileInfo &dirInfo, dirList) {
        collectObjFiles(dirInfo.absoluteFilePath(), objFilesMap);
    }
}


// 将OBJ文件转换为JPG文件
//void convertObjToJpg(const QString &objFilePath) {
//    // 获取文件名（不含扩展名）
//    QFileInfo fileInfo(objFilePath);
//    QString baseName = fileInfo.completeBaseName();
//    QString directory = fileInfo.absolutePath();
//    QString jpgFilePath = directory + "/" + baseName + ".jpg";

//    // 检查JPG文件是否已存在
////    if (QFile::exists(jpgFilePath)) {
////        qDebug() << "JPG文件已存在，跳过转换: " << jpgFilePath;
////        return;
////    }

//    try {
//        // 创建OSG查看器
//        osg::ref_ptr<osgViewer::Viewer> viewer = new osgViewer::Viewer();
//        // 设置查看器窗口为离屏渲染模式
//        viewer->setUpViewInWindow(0, 0, 200, 150, 0);

//        // 读取OBJ文件
//        osg::ref_ptr<osg::Node> rootNode = osgDB::readNodeFile(objFilePath.toStdString());
//        if (!rootNode) {
//            qDebug() << "无法加载OBJ文件: " << objFilePath;
//            // 如果OSG加载失败，回退到简单的占位图像
//            QImage image(200, 150, QImage::Format_RGB32);
//            image.fill(Qt::white);
//            if (image.save(jpgFilePath, "JPG")) {
//                qDebug() << "成功创建占位JPG文件: " << jpgFilePath;
//            } else {
//                qDebug() << "保存占位JPG文件失败: " << jpgFilePath;
//            }

//            return;
//        }

//        // 优化场景图
//        osgUtil::Optimizer optimizer;
//        optimizer.optimize(rootNode.get());

//        // 设置场景数据
//        viewer->setSceneData(rootNode);
//        // 设置相机操纵器
//        viewer->setCameraManipulator(new osgGA::TrackballManipulator());
//        // 添加常用的事件处理器
//        viewer->addEventHandler(new osgViewer::StatsHandler());
//        viewer->addEventHandler(new osgViewer::WindowSizeHandler());

//        // 渲染一帧
//        viewer->frame();
//        // 创建一个图像来存储截取的内容
//        osg::ref_ptr<osg::Image> image = new osg::Image;
//        image->readPixels(0, 0, 200, 150, GL_RGB, GL_UNSIGNED_BYTE);

//        // 将OSG图像转换为QImage
//        QImage qImage(200, 150, QImage::Format_RGB888);
//        unsigned char* data = image->data();

//        for (int y = 0; y < 150; ++y) {
//            for (int x = 0; x < 200; ++x) {
//                int index = (y * 200 + x) * 3;
//                // 注意OSG图像的颜色通道顺序可能需要调整
//                qImage.setPixel(x, 149 - y, qRgb(data[index], data[index + 1], data[index + 2]));
//            }
//        }

//        // 保存为JPG文件
//        if (qImage.save(jpgFilePath, "JPG")) {
//            qDebug() << "成功创建JPG文件: " << jpgFilePath;
//        } else {
//            qDebug() << "保存JPG文件失败: " << jpgFilePath;
//        }
//    } catch (const std::exception& e) {
//        qDebug() << "OSG处理过程中发生错误: " << e.what();
//    }
//}

// 遍历Data目录，返回Background和Object目录下的OBJ文件列表
QMap<QString, QStringList> traverseDirectory(const QString &path) {
    QMap<QString, QStringList> resultMap;
    QDir dataDir(path);

    // 检查Data目录是否存在
    if (!dataDir.exists()) {
        qDebug() << "错误: Data目录不存在: " << path;
        return resultMap;
    }

    // 定义要查找的目录
    QStringList targetDirs = {"Background", "Object"};

    // 遍历目标目录
    foreach (const QString &targetDir, targetDirs) {
        QString targetPath = path + "/" + targetDir;
        QDir target(targetPath);

        if (target.exists()) {
            QMap<QString, QString> objFilesMap;
            collectObjFiles(targetPath, objFilesMap);

            // 只在有OBJ文件时添加到结果中
            if (!objFilesMap.isEmpty()) {
                // 将文件名添加到结果列表
                QStringList fileNames = objFilesMap.values();
                resultMap[targetDir] = fileNames;

                // 为每个OBJ文件创建对应的JPG文件
//                foreach (const QString &objFilePath, objFilesMap.values()) {
//                    convertObjToJpg(objFilePath);
//                }
            }
        } else {
            qDebug() << targetDir << "目录不存在";
        }
    }

    return resultMap;
}

// 打印特定目录下的OBJ文件
void printDirectoryObjFiles(const QString &directoryName, const QStringList &objFiles) {
    if (!objFiles.isEmpty()) {
        qDebug() << "\n" << directoryName << "目录中的OBJ文件：";
        foreach (const QString &fileName, objFiles) {
            qDebug() << "- " << fileName;
        }
    }
}

// 专门用于输出Background和Object目录中的OBJ文件
void printObjFilesInDirectories(const QMap<QString, QStringList> &dataStructure) {
    // 遍历结果中的所有目录
    foreach (const QString &directoryName, dataStructure.keys()) {
        printDirectoryObjFiles(directoryName, dataStructure[directoryName]);
    }
}


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    // 确保支持高DPI显示
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    
    // 创建主窗口
    MainWindow mainWindow;

    mainWindow.setData(traverseDirectory("d:/Data1"));

    // 显示主窗口
    mainWindow.resize(800, 600);
    mainWindow.show();
    
    return a.exec();
}

