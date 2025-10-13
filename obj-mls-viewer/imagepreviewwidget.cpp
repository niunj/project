#include "imagepreviewwidget.h"
#include <QVBoxLayout>
#include <QDebug>
#include <QPainter>

ImagePreviewWidget::ImagePreviewWidget(QWidget *parent)
    : QWidget(parent), isDragging(false)
{
    // 创建图像标签
    imageLabel = new QLabel(this);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setScaledContents(true);
    imageLabel->setMinimumSize(200, 150);
    imageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 设置布局
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(imageLabel);
    setLayout(layout);

    // 设置鼠标追踪
    setMouseTracking(true);
    imageLabel->setMouseTracking(true);

    // 设置窗口属性
    setMinimumSize(200, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ImagePreviewWidget::setImage(const QPixmap &pixmap)
{
    if (!pixmap.isNull()) {
        // 计算合适的显示大小
        QSize scaledSize = pixmap.size().scaled(200, 150, Qt::KeepAspectRatio);
        imageLabel->setPixmap(pixmap.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

void ImagePreviewWidget::setFileName(const QString &fileName)
{
    currentFileName = fileName;
}

QString ImagePreviewWidget::getFileName() const
{
    return currentFileName;
}

void ImagePreviewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // 记录原始鼠标样式
        originalCursor = cursor();

        // 开始拖动状态
        isDragging = true;

        // 记录当前文件名并发出信号
        emit fileDragStarted(currentFileName);
        qDebug() << "开始拖动文件: " << currentFileName;

        // 改变鼠标样式为图像
        if (!imageLabel->pixmap()->isNull()) {
            QPixmap cursorPixmap = imageLabel->pixmap()->scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            setCursor(QCursor(cursorPixmap, 0, 0));
        }

        event->accept();
    }

    QWidget::mousePressEvent(event);
}

void ImagePreviewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (isDragging) {
        // 拖动过程中可以添加额外的处理逻辑
        event->accept();
    }
    QWidget::mouseMoveEvent(event);
}

void ImagePreviewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isDragging) {
        // 结束拖动状态
        isDragging = false;

        // 发出拖动结束信号
        emit fileDragEnded();
        qDebug() << "结束拖动文件";

        // 还原鼠标样式
        setCursor(originalCursor);


        // dropEvent->globalPos() 是全局位置，转换为 osg widget 本地坐标
//        QPoint local = osgWidget->mapFromGlobal(event->globalPos());
//        float x = static_cast<float>(local.x());
//        float y = static_cast<float>(local.y());
//        // OSG 窗口的 y 原点通常在左下，Qt 在左上：做转换
//        int h = osgWidget->height();
//        float osgY = static_cast<float>(h - y);

//        // 拾取得到世界坐标
//        osgViewer::Viewer* viewer = osgContext::getInstance()->get3DViewer()->getViewer();
//        if (!viewer) {
//            event->ignore();
//            return;
//        }

//        const auto& hit = *intersector->getIntersections().begin();
//        osg::Vec3d worldPos = hit.getWorldIntersectPoint();

//        // 将世界坐标转换为经纬高（项目已有工具）
//        osg::Vec3d LLH;
//        CoordConvert::getInstance().XYZ2DegreeLLH(worldPos, LLH);

//        // 根据文件类型调用不同接口
//        QString suf = QFileInfo(localPath).suffix().toLower();

//        if (suf == "obj" || suf == "dat") {
//            // 构造平台结构并添加
//            MtePlatformStru platform;
//            platform.m_id = CommonFunction::getInstance().getIndex();
//            platform.m_path = localPath; // MtePlatformStru 使用 QString
//            platform.modelType = OBJECT; // 确保 OBJECT 在枚举中存在
//            platform.m_attribute.m_lon = LLH.x();
//            platform.m_attribute.m_lat = LLH.y();
//            // 建议查询地形高度或设为 LLH.z()
//            platform.m_attribute.m_alt = LLH.z();
//            platform.m_attribute.m_scale = 1.0;
//            Mte3DService::getInstance().addPlatform(platform);
//        } else if (suf == "tif" || suf == "png" || suf == "jpg") {
//            // 影像层或贴图
//            Mte3DService::getInstance().addImageLayer(localPath.toStdString());
//        } else {
//            // 其他类型：可以通过 addObjNode 作为简单节点放置
//            osg::Vec3d pos(worldPos);
//            Mte3DService::getInstance().addObjNode(CommonFunction::getInstance().getIndex(), localPath.toStdString(), pos);
//        }

        event->accept();
    }

    QWidget::mouseReleaseEvent(event);
}
