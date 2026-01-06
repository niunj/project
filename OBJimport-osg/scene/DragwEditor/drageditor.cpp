#include "drageditor.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QWidget>
#include <QTextCodec>
#include "imagepreviewwidget.h"
// #include "addmodeldialog.h"
#include "../SceneEngine/SceneEngine.h"

#include <QPainter>
#include <QPixmap>

#include <QToolBox>
#include "../Log/log_manager.h"

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

// 设置SceneEngine
void DragEditor::setSceneEngine(SceneEngine *engine)
{
    m_pSceneEngine = engine;
}


// 遍历Data目录，返回Background和Object目录下的OBJ文件列表
QMap<QString, QStringList> traverseDirectory(const QString &path) {
    QMap<QString, QStringList> resultMap;
    QDir dataDir(path);

    // 检查Data目录是否存在
    if (!dataDir.exists()) {
        LOG_DEBUG << "错误: Data目录不存在: " << path;
        return resultMap;
    }

    // 定义要查找的目录
    QStringList targetDirs = {"Background", "Object", "Terrain"};

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
            LOG_DEBUG << targetDir << "目录不存在";
        }
    }

    return resultMap;
}

// 打印特定目录下的OBJ文件
//void printDirectoryObjFiles(const QString &directoryName, const QStringList &objFiles) {
//    if (!objFiles.isEmpty()) {
//        LOG_DEBUG << "\n" << directoryName << "目录中的OBJ文件：";
//        foreach (const QString &fileName, objFiles) {
//            LOG_DEBUG << "- " << fileName;
//        }
//    }
//}

// 专门用于输出Background和Object目录中的OBJ文件
//void printObjFilesInDirectories(const QMap<QString, QStringList> &dataStructure) {
//    // 遍历结果中的所有目录
//    foreach (const QString &directoryName, dataStructure.keys()) {
//        printDirectoryObjFiles(directoryName, dataStructure[directoryName]);
//    }
//}


DragEditor::DragEditor(QWidget *parent)
    : QWidget(parent), selectedPreviewWidget(nullptr), m_pSceneEngine(nullptr)
{
    // 在构造函数最前面设置本地编码（适用于源文件以本地编码保存的情况），
    // 这样可以避免中文字符串显示异常（在 Windows 中文系统上通常为 GBK）。

    setWindowTitle("OBJ文件浏览器");

    resize(900, 600);

    // 创建主布局
    QVBoxLayout *mainLayout = new QVBoxLayout();

    // 创建工具箱来显示分类
    toolBox = new QToolBox();
    logLabel = new QLabel;
    
    // 设置toolBox的样式表，使非选中状态的页面显示更浅
    toolBox->setStyleSheet(
        "QToolBox::tab {\n"                    
        "    background-color: #e6f0ff;\n"   
        "    border: 1px solid #b3d9ff;\n"      
        "    padding: 0px;\n"
        "}\n"                                 
        "QToolBox::tab:selected {\n"        
        "    background-color: #4da6ff;\n"   
        "    color: white;\n"                
        "    font-weight: bold;\n"           
        "}\n"                                 
        "QToolBox::tab:!selected {\n"        
        "    background-color: #f0f7ff;\n"   
        "    color: #666666;\n"              
        "}\n"                                 
        "QToolBox {\n"                       
        "    border: 1px solid #b3d9ff;\n"      
        "    border-radius: 4px;\n"          
        "    padding: 0px;\n"
        "}"
    );

    mainLayout->addWidget(toolBox);
    mainLayout->addWidget(logLabel);

    mainLayout->setContentsMargins(0,0,0,0);
    setLayout(mainLayout);
}

DragEditor::~DragEditor()
{
//    delete ui;
}


// 添加工具箱页面
void DragEditor::addToolBoxPage(const QString &category, const QStringList &files) {
   // 创建滚动区域
   QScrollArea *scrollArea = new QScrollArea();
   scrollArea->setWidgetResizable(true);

   // 创建内容窗口
   QWidget *contentWidget = new QWidget();
   QGridLayout *gridLayout = new QGridLayout(contentWidget);
   
   // 将gridLayout存储到映射中
   categoryLayouts[category] = gridLayout;

   int row = 0, col = 0;

   // 添加文件到布局
   for (const QString &fileName : files) {

       // 使用QFileInfo解析文件路径
       QFileInfo fileInfo(fileName);

       // 构造新的文件名（替换后缀为.jpg）
       QString jpgFileName = fileInfo.baseName() + ".jpg";

        // 组合完整路径
       QString imageFilename = fileInfo.path() + "/" + jpgFileName;

       // 创建自定义图像预览部件
       ImagePreviewWidget *previewWidget = new ImagePreviewWidget();
       QString modelName = fileInfo.baseName();
       previewWidget->setObjectName("PreviewWidget_" + modelName);
       previewWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
       
       // 将预览部件添加到列表中
       m_previewWidgets.append(previewWidget);
       
       // 如果SceneEngine已经设置，传递给预览部件
       if (m_pSceneEngine) {
           previewWidget->setSceneEngine(m_pSceneEngine);
       }

       connect(previewWidget, &ImagePreviewWidget::notifyError, this, &DragEditor::slotNotofyLog);

       connect(previewWidget, &ImagePreviewWidget::sigPlatformAdded, this, &DragEditor::sigPlatformAdded);

       LOG_DEBUG<< imageFilename;

       // 加载并设置图片
       QPixmap pixmap;
       if (!pixmap.load(imageFilename)) {
           LOG_DEBUG << "无法加载图片: " << imageFilename;
           // 如果图片加载失败，创建一个占位符图像
           pixmap = QPixmap(200, 150);
           pixmap.fill(Qt::lightGray);
       }

       previewWidget->setImage(pixmap);
       previewWidget->setFileName(fileName); // 设置原始OBJ文件路径
       previewWidget->setCategoryName(category);

       previewWidget->setMinimumSize(220, 180);

       // 连接信号槽来处理拖动事件
       connect(previewWidget, &ImagePreviewWidget::fileDragStarted, [=](const QString &file) {
           LOG_DEBUG << "拖动开始: " << file;
       });

       connect(previewWidget, &ImagePreviewWidget::fileDragEnded, [=]() {
           LOG_DEBUG << "拖动结束";
       });

       // 连接选择信号
       connect(previewWidget, &ImagePreviewWidget::selectionChanged, [=](bool selected) {
           if (selected) {
               // 如果当前有其他选中的widget，取消其选中状态
               if (selectedPreviewWidget && selectedPreviewWidget != previewWidget) {
                   selectedPreviewWidget->setSelected(false);
               }
               // 设置当前选中的widget
               selectedPreviewWidget = previewWidget;
           } else {
               // 如果取消选中的是当前选中的widget，清空选中状态
               if (selectedPreviewWidget == previewWidget) {
                   selectedPreviewWidget = nullptr;
               }
           }
       });

       // 添加到网格布局
       gridLayout->addWidget(previewWidget, row, col);

       // 更新行列索引
       col++;
       if (col == 2) { // 每行显示3个
           col = 0;
           row++;
       }
   }

   // 替换addPreviewWidget为ClickablePreviewWidget
   ClickablePreviewWidget *clickablePreview = new ClickablePreviewWidget(category);
   clickablePreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
   clickablePreview->setStyleSheet("QWidget { background-color: #F0F0F0; border: 2px dashed #cccccc; border-radius: 4px; } QWidget:hover { background-color: #e0e0e0; border-color: #999999; cursor: pointer; }");
   
   // 设置大小
   clickablePreview->setMinimumSize(220, 180);
   
   // 连接点击信号
   // connect(clickablePreview, &ClickablePreviewWidget::clicked, [=]() {
   //     // 弹出添加模型对话框
   //     AddModelDialog dialog(category, this);
   //     if (dialog.exec() == QDialog::Accepted) {
   //         // 获取对话框中的信息
   //         QString modelName = dialog.getModelName();
   //         QString category = dialog.getCategory();
   //         QString jpgPath  = dialog.getJpgPath();
   //         QString modelPath = dialog.getModelPath();
   //         QString texturePath = dialog.getTexturePath();
   //         QString materialPath = dialog.getMaterialPath();
   //         QString meshPath = dialog.getMeshPath();
           
   //         // 创建目标目录
   //         QString modelDir = "data/model/" + category + "/" + modelName;
   //         QDir dir;
   //         if (!dir.exists(modelDir)) {
   //             dir.mkpath(modelDir);
   //         }
           
   //         // 拷贝模型文件
   //         if (!jpgPath.isEmpty()) {
   //             QFileInfo fileInfo(jpgPath);
   //             QString extension = fileInfo.suffix();
   //             QString destPath = modelDir + "/" + modelName + "." + extension;
   //             QFile::copy(jpgPath, destPath);
   //         }


   //         // 拷贝模型文件
   //         if (!modelPath.isEmpty()) {
   //             QFileInfo fileInfo(modelPath);
   //             QString extension = fileInfo.suffix();
   //             QString destPath = modelDir + "/" + modelName + "." + extension;
   //             QFile::copy(modelPath, destPath);
   //         }
           
   //         // 拷贝纹理文件
   //         if (!texturePath.isEmpty()) {
   //             QFileInfo fileInfo(texturePath);
   //             QString extension = fileInfo.suffix();
   //             QString destPath = modelDir + "/" + modelName + "." + extension;
   //             QFile::copy(texturePath, destPath);
   //         }
           
   //         // 拷贝材质文件
   //         if (!materialPath.isEmpty()) {
   //             QFileInfo fileInfo(materialPath);
   //             QString extension = fileInfo.suffix();
   //             QString destPath = modelDir + "/" + modelName + "." + extension;
   //             QFile::copy(materialPath, destPath);
   //         }
           
   //         // 拷贝网格文件
   //         if (!meshPath.isEmpty()) {
   //             QFileInfo fileInfo(meshPath);
   //             QString extension = fileInfo.suffix();
   //             QString destPath = modelDir + "/" + modelName + "." + extension;
   //             QFile::copy(meshPath, destPath);
   //         }
          
   //         // 追加新模型到界面
   //         addModelToCategory(category, modelName);

   //     }
   // });
   
   // 将ClickablePreviewWidget添加到gridLayout的最后一个位置
   gridLayout->addWidget(clickablePreview, row, col);
   
   // 设置滚动区域的内容
   scrollArea->setWidget(contentWidget);
   
   // 添加到工具箱
   if(category == "Background") {
       toolBox->addItem(scrollArea, "背景模型");
   }
   else if(category == "Object") {
       toolBox->addItem(scrollArea, "目标模型");
   }
   else if(category == "Terrain") {
       toolBox->addItem(scrollArea, "地形模型");
   }

}


void DragEditor::slotNotofyLog(QString log)
{
    logLabel->setText(log);
}


// 初始化工具箱
void DragEditor::initializeToolBox() {
  // 清空现有页面
  while (toolBox->count() > 0) {
      QWidget *widget = toolBox->widget(0);
      toolBox->removeItem(0);
      delete widget;
  }

  // 为每个分类创建一个页面
  QStringList categories = {"Background", "Object", "Terrain"};
  foreach (const QString &category, categories) {
      if (dataStructure.contains(category)) {
          addToolBoxPage(category, dataStructure[category]);
      }
  }

}


// 设置数据
void DragEditor::setData(const QString &data) {
    dataStructure = traverseDirectory(data);
    initializeToolBox();
}

// 获取分类的显示名称
QString DragEditor::getCategoryDisplayName(const QString &category) {
    if (category == "Background") {
        return "背景模型";
    } else if (category == "Object") {
        return "目标模型";
    } else if (category == "Terrain") {
        return "地形模型";
    }
    return category;
}

// 辅助函数：获取指定分类的ClickablePreviewWidget位置
QPair<int, int> DragEditor::getClickablePreviewPosition(const QString &category)
{
    if (!categoryLayouts.contains(category)) {
        return QPair<int, int>(-1, -1);
    }
    
    QGridLayout* gridLayout = categoryLayouts[category];
    for (int row = 0; row < gridLayout->rowCount(); ++row) {
        for (int col = 0; col < gridLayout->columnCount(); ++col) {
            QLayoutItem* item = gridLayout->itemAtPosition(row, col);
            if (item && item->widget()) {
                ClickablePreviewWidget* clickableWidget = dynamic_cast<ClickablePreviewWidget*>(item->widget());
                if (clickableWidget) {
                    return QPair<int, int>(row, col);
                }
            }
        }
    }
    return QPair<int, int>(-1, -1);
}

// 辅助函数：重新排列网格布局中的widget
void DragEditor::rearrangeGridLayout(const QString &category)
{
    if (!categoryLayouts.contains(category)) {
        return;
    }
    
    QGridLayout* gridLayout = categoryLayouts[category];
    QList<ImagePreviewWidget*> imageWidgets;
    ClickablePreviewWidget* clickableWidget = nullptr;
    
    // 收集所有widget
    QList<QLayoutItem*> itemsToRemove;
    for (int i = 0; i < gridLayout->count(); ++i) {
        QLayoutItem* item = gridLayout->itemAt(i);
        if (item && item->widget()) {
            itemsToRemove.append(item);
        }
    }
    
    // 移除并分类所有widget
    for (QLayoutItem* item : itemsToRemove) {
        if (ImagePreviewWidget* imageWidget = qobject_cast<ImagePreviewWidget*>(item->widget())) {
            imageWidgets.append(imageWidget);
        } else if (ClickablePreviewWidget* clickable = qobject_cast<ClickablePreviewWidget*>(item->widget())) {
            clickableWidget = clickable;
        }
        gridLayout->removeItem(item);
    }
    
    // 重新添加所有模型widget
    for (int i = 0; i < imageWidgets.size(); ++i) {
        int row = i / 2;
        int col = i % 2;
        gridLayout->addWidget(imageWidgets[i], row, col);
    }
    
    // 最后添加ClickablePreviewWidget
    if (clickableWidget) {
        int clickableRow = imageWidgets.isEmpty() ? 0 : (imageWidgets.size() + 1) / 2;
        int clickableCol = imageWidgets.size() % 2;
        gridLayout->addWidget(clickableWidget, clickableRow, clickableCol);
    }
    
    // 刷新布局
    gridLayout->invalidate();
    gridLayout->update();
    
    // 获取父窗口并刷新
    if (gridLayout->parentWidget()) {
        gridLayout->parentWidget()->update();
        gridLayout->parentWidget()->repaint();
    }
}

// 处理键盘事件
void DragEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete) {
        if (selectedPreviewWidget) {
            QString category = selectedPreviewWidget->getCategoryName();
            QFileInfo fileInfo(selectedPreviewWidget->getFileName());
            QString modelName = fileInfo.baseName();
            
            // 确认删除
            if (QMessageBox::question(this, "删除确认", QString("确定要删除模型 '%1' 及其目录吗？").arg(modelName), 
                                     QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                // 删除模型文件目录
                deleteModelDirectory(selectedPreviewWidget->getFileName());
                
                // 提示删除成功和目录路径
                QString deletedDir = QFileInfo(selectedPreviewWidget->getFileName()).path();
                QMessageBox::information(this, "删除成功", QString("已成功删除模型目录：%1").arg(deletedDir));
                
                // 从分类中删除模型
                removeModelFromCategory(category, modelName);
                
                // 清空选中状态
                selectedPreviewWidget = nullptr;
            }
        }
    }
    
    QWidget::keyPressEvent(event);
}

// 删除模型对应的目录
void DragEditor::deleteModelDirectory(const QString &modelPath)
{
    QFileInfo fileInfo(modelPath);
    QString directoryPath = fileInfo.path();
    
    QDir dir(directoryPath);
    if (dir.exists()) {
        if (!dir.removeRecursively()) {
            LOG_DEBUG << "无法删除目录: " << directoryPath;
            QMessageBox::warning(this, "删除失败", "无法删除模型目录，请检查权限或文件是否被占用");
        } else {
            LOG_DEBUG << "成功删除目录: " << directoryPath;
        }
    }
}

// 从指定分类中删除模型
void DragEditor::removeModelFromCategory(const QString &category, const QString &modelName)
{
    if (!categoryLayouts.contains(category) || !dataStructure.contains(category)) {
        return;
    }
    
    QGridLayout* gridLayout = categoryLayouts[category];
    QString widgetName = "PreviewWidget_" + modelName;
    QWidget* targetWidget = nullptr;
    
    // 查找目标widget
    for (int row = 0; row < gridLayout->rowCount(); ++row) {
        for (int col = 0; col < gridLayout->columnCount(); ++col) {
            QLayoutItem* item = gridLayout->itemAtPosition(row, col);
            if (item && item->widget() && item->widget()->objectName() == widgetName) {
                targetWidget = item->widget();
                break;
            }
        }
        if (targetWidget) {
            break;
        }
    }
    
    // 删除widget
    if (targetWidget) {
        // 保存widget的父窗口
        QWidget* parentWidget = gridLayout->parentWidget();
        
        gridLayout->removeWidget(targetWidget);
        targetWidget->deleteLater();
        
        // 从dataStructure中移除
        dataStructure[category].removeAll(modelName);
        
        // 重新排列布局
        rearrangeGridLayout(category);
        
        // 强制刷新布局
        gridLayout->invalidate();
        gridLayout->update();
        
        // 刷新父窗口
        if (parentWidget) {
            parentWidget->update();
            parentWidget->repaint();
        }
        
        // 刷新滚动区域（如果有的话）
        if (QScrollArea* scrollArea = parentWidget->findChild<QScrollArea*>()) {
            scrollArea->update();
            scrollArea->repaint();
        }
    }
}

// 追加新模型到指定分类
void DragEditor::addModelToCategory(const QString &category, const QString &modelName)
{
    // 检查categoryLayouts中是否包含该分类的布局
    if (!categoryLayouts.contains(category)) {
        QMessageBox::warning(this, "警告", "未找到该分类的布局");
        return;
    }
    
    // 构造模型文件路径
    QString modelDir =  "data/model/" + category + "/" + modelName;
    QString modelFile = modelDir + "/" + modelName + ".obj";
    
    // 检查OBJ文件是否存在，如果不存在检查FLT文件
    if (!QFile::exists(modelFile)) {
        modelFile = modelDir + "/" + modelName + ".flt";
        if (!QFile::exists(modelFile)) {
            QMessageBox::warning(this, "警告", "无法找到模型文件");
            return;
        }
    }
    
    // 获取预览图片路径
    QString previewImage = modelDir + "/" + modelName + ".jpg";
    
    // 创建新的ImagePreviewWidget
    ImagePreviewWidget* previewWidget = new ImagePreviewWidget();
    previewWidget->setObjectName("PreviewWidget_" + modelName);
    previewWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    // 连接信号槽
    connect(previewWidget, &ImagePreviewWidget::notifyError, this, &DragEditor::slotNotofyLog);
    connect(previewWidget, &ImagePreviewWidget::sigPlatformAdded, this, &DragEditor::sigPlatformAdded);
    
    // 加载并设置图片
    QPixmap pixmap;
    if (!pixmap.load(previewImage)) {
        LOG_DEBUG << "无法加载图片: " << previewImage;
        // 如果图片加载失败，创建一个占位符图像
        pixmap = QPixmap(200, 150);
        pixmap.fill(Qt::lightGray);
    }
    
    previewWidget->setImage(pixmap);
    previewWidget->setFileName(modelFile); // 设置原始OBJ文件路径
    previewWidget->setCategoryName(category);
    previewWidget->setMinimumSize(220, 180);
    
    // 连接拖动事件
    connect(previewWidget, &ImagePreviewWidget::fileDragStarted, [=](const QString &file) {
        LOG_DEBUG << "拖动开始: " << file;
    });
    
    connect(previewWidget, &ImagePreviewWidget::fileDragEnded, [=]() {
        LOG_DEBUG << "拖动结束";
    });
    
    // 连接选择信号
    connect(previewWidget, &ImagePreviewWidget::selectionChanged, [=](bool selected) {
        if (selected) {
            // 如果当前有其他选中的widget，取消其选中状态
            if (selectedPreviewWidget && selectedPreviewWidget != previewWidget) {
                selectedPreviewWidget->setSelected(false);
            }
            // 设置当前选中的widget
            selectedPreviewWidget = previewWidget;
        } else {
            // 如果取消选中的是当前选中的widget，清空选中状态
            if (selectedPreviewWidget == previewWidget) {
                selectedPreviewWidget = nullptr;
            }
        }
    });
    
    // 添加到dataStructure
    if (!dataStructure.contains(category)) {
        dataStructure[category] = QList<QString>();
    }
    dataStructure[category].append(modelName);
    
    // 重新排列布局以插入新模型
    QGridLayout* gridLayout = categoryLayouts[category];
    
    // 先获取ClickablePreviewWidget
    ClickablePreviewWidget* clickableWidget = nullptr;
    QPair<int, int> clickablePos = getClickablePreviewPosition(category);
    if (clickablePos.first != -1 && clickablePos.second != -1) {
        QLayoutItem* item = gridLayout->itemAtPosition(clickablePos.first, clickablePos.second);
        if (item && item->widget()) {
            clickableWidget = static_cast<ClickablePreviewWidget*>(item->widget());
            gridLayout->removeWidget(clickableWidget);
        }
    }
    
    // 计算新模型的位置
    int modelCount = dataStructure[category].size();
    int insertRow = (modelCount - 1) / 2;
    int insertCol = (modelCount - 1) % 2;
    
    // 添加新模型
    gridLayout->addWidget(previewWidget, insertRow, insertCol);
    
    // 重新添加ClickablePreviewWidget
    if (clickableWidget) {
        if(insertCol == 0) {
            int clickableRow = insertRow;
            int clickableCol = (insertCol+1);
            gridLayout->addWidget(clickableWidget, clickableRow, clickableCol);
        }
        else{
            int clickableRow = insertRow+1;
            int clickableCol = 0;
            gridLayout->addWidget(clickableWidget, clickableRow, clickableCol);
        }
    }
}
