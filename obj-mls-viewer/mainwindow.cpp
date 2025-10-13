#include "mainwindow.h"
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
#include <QDebug>
#include <QTextCodec>

#include <QPainter>
#include <QPixmap>

#include <QToolBox>

// 假设我们有一个用于显示OBJ文件的自定义部件
#include "objviewerwidget.h"
#include "imagepreviewwidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    // 在构造函数最前面设置本地编码（适用于源文件以本地编码保存的情况），
    // 这样可以避免中文字符串显示异常（在 Windows 中文系统上通常为 GBK）。

    setWindowTitle("OBJ文件浏览器");

    resize(900, 600);

    // 创建主布局
    QVBoxLayout *mainLayout = new QVBoxLayout();

    // 创建工具箱来显示分类
    toolBox = new QToolBox();

    mainLayout->addWidget(toolBox);

    setLayout(mainLayout);
}

MainWindow::~MainWindow()
{
//    delete ui;
}


// 添加工具箱页面
void MainWindow::addToolBoxPage(const QString &category, const QStringList &files) {
   // 创建滚动区域
   QScrollArea *scrollArea = new QScrollArea();
   scrollArea->setWidgetResizable(true);

   // 创建内容窗口
   QWidget *contentWidget = new QWidget();
   QGridLayout *gridLayout = new QGridLayout(contentWidget);

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
       previewWidget->setObjectName("PreviewWidget_");
       previewWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

       // 加载并设置图片
       QPixmap pixmap;
       if (!pixmap.load(imageFilename)) {
           qDebug() << "无法加载图片: " << imageFilename;
           // 如果图片加载失败，创建一个占位符图像
           pixmap = QPixmap(200, 150);
           pixmap.fill(Qt::lightGray);
//           QPainter painter(&pixmap);
//           painter.drawText(pixmap.rect(), Qt::AlignCenter, "无预览图");
       }
       previewWidget->setImage(pixmap);
       previewWidget->setFileName(fileName); // 设置原始OBJ文件路径

       // 连接信号槽来处理拖动事件
       connect(previewWidget, &ImagePreviewWidget::fileDragStarted, [=](const QString &file) {
           qDebug() << "拖动开始: " << file;
           // 在这里可以添加处理文件拖动开始的逻辑
       });

       connect(previewWidget, &ImagePreviewWidget::fileDragEnded, [=]() {
           qDebug() << "拖动结束";
           // 在这里可以添加处理文件拖动结束的逻辑
       });

       // 创建显示文件信息的标签
       QLabel *fileLabel = new QLabel(imageFilename);
       fileLabel->setAlignment(Qt::AlignCenter);
       fileLabel->setFrameStyle(QFrame::StyledPanel);
       fileLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

       // 创建垂直布局来放置图像和文件名
       QVBoxLayout *itemLayout = new QVBoxLayout();
       itemLayout->addWidget(previewWidget);
       itemLayout->addWidget(fileLabel);

       // 创建容器来放置这个布局
       QWidget *itemWidget = new QWidget();
       itemWidget->setLayout(itemLayout);
       itemWidget->setMinimumSize(220, 180);
       itemWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

       // 添加到网格布局
       gridLayout->addWidget(itemWidget, row, col);

       // 更新行列索引
       col++;
       if (col == 3) { // 每行显示3个
           col = 0;
           row++;
       }

   }

   // 设置滚动区域的内容
   scrollArea->setWidget(contentWidget);

   // 添加到工具箱
   toolBox->addItem(scrollArea, category);
}

// 初始化工具箱
void MainWindow::initializeToolBox() {
  // 清空现有页面
  while (toolBox->count() > 0) {
      QWidget *widget = toolBox->widget(0);
      toolBox->removeItem(0);
      delete widget;
  }

  // 为每个分类创建一个页面
  QStringList categories = {"Background", "Object"};
  foreach (const QString &category, categories) {
      if (dataStructure.contains(category)) {
          addToolBoxPage(category, dataStructure[category]);
      }
  }

}


// 设置数据
void MainWindow::setData(const QMap<QString, QStringList> &data) {
    dataStructure = data;

    initializeToolBox();
}


