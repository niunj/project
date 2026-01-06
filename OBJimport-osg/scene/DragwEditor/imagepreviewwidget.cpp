#include "imagepreviewwidget.h"
#include <QVBoxLayout>
#include <QPainter>
#include <QUrl>
#include <QFileInfo>

#include "../Common/readwritefile.h"
#include "../Log/log_manager.h"
#include "../SceneEngine/SceneEngine.h"

ImagePreviewWidget::ImagePreviewWidget(QWidget *parent)
    : QWidget(parent), isDragging(false), isSelectedState(false), m_pSceneEngine(nullptr)
{
    // 创建图像标签
    imageLabel = new QLabel(this);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setScaledContents(true);
    imageLabel->setMinimumSize(196, 146); // 减小内部图片尺寸，为边框留出空间
    imageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 创建文件名标签
    fileNameLabel = new QLabel(this);
    fileNameLabel->setAlignment(Qt::AlignCenter);
    fileNameLabel->setFrameStyle(QFrame::StyledPanel);
    fileNameLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    
    QFont font("SimHei", 12);
    font.setBold(true);
    fileNameLabel->setFont(font);
    fileNameLabel->setWordWrap(true);
    fileNameLabel->setMinimumWidth(100); // 减小最小宽度，让弹簧控制居中

    // 创建水平布局器来承载文件名标签
    QHBoxLayout *fileNameLayout = new QHBoxLayout();
    fileNameLayout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Fixed));
    fileNameLayout->addWidget(fileNameLabel);
    fileNameLayout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Fixed));

    // 设置垂直布局
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2); // 为红色边框留出2像素的边距
    layout->addWidget(imageLabel);
    layout->addLayout(fileNameLayout);
    setLayout(layout);

    // 设置鼠标追踪
    setMouseTracking(true);
    imageLabel->setMouseTracking(true);

    // 设置窗口属性
    setMinimumSize(200, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    // 设置焦点策略，以便接收键盘事件
    setFocusPolicy(Qt::ClickFocus);
}

void ImagePreviewWidget::setImage(const QPixmap &pixmap)
{
    if (!pixmap.isNull()) {
        // 计算合适的显示大小
        QSize scaledSize = pixmap.size().scaled(200, 150, Qt::KeepAspectRatio);
        imageLabel->setPixmap(pixmap.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

// 设置SceneEngine
void ImagePreviewWidget::setSceneEngine(SceneEngine *engine)
{
    m_pSceneEngine = engine;
}


void ImagePreviewWidget::setFileName(const QString &fileName)
{
    currentFileName = fileName;
    // 更新文件名标签显示
    QFileInfo fileInfo(fileName);
    fileNameLabel->setText(fileInfo.baseName());
}

void ImagePreviewWidget::setCategoryName(const QString &cateName)
{
    currentCategoryName = cateName;
}

QString ImagePreviewWidget::getFileName() const
{
    return currentFileName;
}

QString ImagePreviewWidget::getCategoryName() const
{
    return currentCategoryName;
}

bool ImagePreviewWidget::isSelected() const
{
    return isSelectedState;
}

void ImagePreviewWidget::setSelected(bool selected)
{
    isSelectedState = selected;
    update(); // 触发重绘
    emit selectionChanged(selected);
}

void ImagePreviewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // 切换选中状态
        setSelected(!isSelectedState);

        // 记录原始鼠标样式
        originalCursor = cursor();

        // 开始拖动状态
        isDragging = true;

        // 记录当前文件名并发出信号
        emit fileDragStarted(currentFileName);
        LOG_DEBUG << "开始拖动文件: " << currentFileName;

        // 改变鼠标样式为图像
        if (!imageLabel->pixmap()->isNull()) {
            QPixmap cursorPixmap = imageLabel->pixmap()->scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            setCursor(QCursor(cursorPixmap, 0, 0));
        }

        event->accept();
    }

    QWidget::mousePressEvent(event);
}

// void ImagePreviewWidget::mouseDoubleClickEvent(QMouseEvent *event)
// {
//     if (event->button() == Qt::LeftButton) {
//         emit doubleClicked();
//         event->accept();
//     }

//     QWidget::mouseDoubleClickEvent(event);
// }

void ImagePreviewWidget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    
    // 如果被选中，绘制边框
    if (isSelectedState) {
        QPainter painter(this);
        painter.setPen(QPen(Qt::red, 2, Qt::SolidLine));
        painter.drawRect(rect().adjusted(1, 1, -1, -1));
    }
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
        LOG_DEBUG << "结束拖动文件";

        // 还原鼠标样式
        setCursor(originalCursor);

        // 将全局鼠标坐标转换到 osg viewer widget 坐标
        // QWidget* osgWidget = dynamic_cast<QWidget*>(osgContext::getInstance()->get3DViewer());
        // if (!osgWidget) {
        //     event->ignore();
        //     return;
        // }

        // //         dropEvent->globalPos() 是全局位置，转换为 osg widget 本地坐标
        // QPoint local = osgWidget->mapFromGlobal(event->globalPos());
        // float x = static_cast<float>(local.x());
        // float y = static_cast<float>(local.y());
        // // OSG 窗口的 y 原点通常在左下，Qt 在左上：做转换
        // int h = osgWidget->height();
        // float osgY = static_cast<float>(h - y);

        // // 拾取得到世界坐标
        // osg::Camera*  camera = osgContext::getInstance()->getCamera();
        // if (!camera) {
        //     event->ignore();
        //     return;
        // }

        // osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector =
        //         new osgUtil::LineSegmentIntersector(osgUtil::Intersector::WINDOW, x, osgY);

        // osgUtil::IntersectionVisitor iv(intersector.get());
        // camera->accept(iv);

        // if (!intersector->containsIntersections()) {
        //     // 无拾取点 -> 可选择使用相机射线与地表相交或直接使用当前相机焦点
        //     event->ignore();
        //     return;
        // }

        // const auto& hit = *intersector->getIntersections().begin();
        // osg::Vec3d worldPos = hit.getWorldIntersectPoint();


        // 根据文件类型调用不同接口
        QString suf = QFileInfo(currentFileName).suffix().toLower();
        QString fileName = QFileInfo(currentFileName).baseName();
        LOG_DEBUG << currentFileName << fileName << suf;

        if (suf == "obj"  ||suf == "flt" ) {
            // 构造平台结构并添加
            MtePlatformStru platform;
            platform.m_id     = m_pSceneEngine->getIndex();

            if(currentCategoryName == "Background") {
                platform.modelType = BACKGROUND; // 确保 OBJECT 在枚举中存在
            }
            else if (currentCategoryName == "Object"){
                platform.modelType = OBJECT; // 确保 OBJECT 在枚举中存在
            }
            else if(currentCategoryName == "Terrain") {
                platform.modelType = GEO; // 确保 OBJECT 在枚举中存在
            }

            platform.m_path   = currentFileName; // MtePlatformStru 使用 QString
            platform.m_name   = fileName;                             //模型名称


            //材质文件路径合法判断，没有需要提示 加 默认
            // 使用QFileInfo解析文件路径
            QFileInfo fileInfo(currentFileName);
            // 构造新的文件名（替换后缀为.mls）
            QString mlsFileName = fileInfo.baseName() + ".mls";

            // 组合完整路径


            //查找同名称的mls文件（区域边界信息）
            QString mlsPath =  fileInfo.path() + "/" + fileInfo.baseName() + ".mls";
            if (QFile::exists(mlsPath)) {
                platform.mlsPath = mlsPath;
            }

            //查找同名称的tif文件（纹理文件）
            QString tifPath =  fileInfo.path() + "/" + fileInfo.baseName() + ".tif";
            if (QFile::exists(tifPath)) {
                platform.textPath = tifPath;
            }

            //查找同名称的mcm文件（材质文件）
            QString mcmPath =  fileInfo.path() + "/" + fileInfo.baseName() + ".mcm";
            if (QFile::exists(mcmPath)) {
                platform.mcmPath = mcmPath;
            }

            //查找同名称的msh文件（网格文件）
            QString mshPath =  fileInfo.path() + "/" + fileInfo.baseName() + ".msh";
            if (QFile::exists(mshPath)) {
                platform.meshPath = mshPath;
            }

            //材质和网格
            LOG_DEBUG  << platform.mlsPath;
            LOG_DEBUG  << platform.textPath;
            LOG_DEBUG  << platform.mcmPath;
            LOG_DEBUG  << platform.meshPath;

            platform.m_attribute.m_x     = 0;
            platform.m_attribute.m_y     = 0;
            // 建议查询地形高度或设为 LLH.z()
            platform.m_attribute.m_z     = 0;
            platform.m_attribute.m_scale = 1.0;

            emit sigPlatformAdded(platform);

        }

        event->accept();
    }

    QWidget::mouseReleaseEvent(event);
}
