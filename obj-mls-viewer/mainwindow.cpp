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

// 假设我们有一个用于显示OBJ文件的自定义部件
#include "objviewerwidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // 在构造函数最前面设置本地编码（适用于源文件以本地编码保存的情况），
    // 这样可以避免中文字符串显示异常（在 Windows 中文系统上通常为 GBK）。

    setWindowTitle(("OBJ/MLS文件查看器"));
    // 顶部工具栏：目录输入、浏览和扫描按钮
    QWidget *central = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);

    QHBoxLayout *topLayout = new QHBoxLayout();
    directoryLineEdit = new QLineEdit(this);
    directoryLineEdit->setPlaceholderText(("选择或输入目录路径"));
    browseBtn = new QPushButton(("浏览..."), this);
    scanBtn = new QPushButton(("扫描"), this);
    topLayout->addWidget(directoryLineEdit);
    topLayout->addWidget(browseBtn);
    topLayout->addWidget(scanBtn);

    mainLayout->addLayout(topLayout);

    // 滚动区，用于承载文件网格
    scrollArea = new QScrollArea(this);
    filesWidget = new QWidget(scrollArea);
    fileGridLayout = new QGridLayout(filesWidget);
    fileGridLayout->setSpacing(20);
    fileGridLayout->setContentsMargins(20, 20, 20, 20);
    filesWidget->setLayout(fileGridLayout);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(filesWidget);
    mainLayout->addWidget(scrollArea);

    setCentralWidget(central);

    // 连接信号
    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::on_browseBtn_clicked);
    connect(scanBtn, &QPushButton::clicked, this, &MainWindow::on_scanBtn_clicked);
}

MainWindow::~MainWindow()
{
//    delete ui;
}

void MainWindow::on_browseBtn_clicked()
{
    // 打开目录选择对话框
    QString directory = QFileDialog::getExistingDirectory(
        this,
        tr("选择包含OBJ或MLS文件的目录"),
        QDir::homePath()
    );

    if (directory.isEmpty()) return;
    directoryLineEdit->setText(directory);
}

void MainWindow::on_scanBtn_clicked()
{
    QString directory = directoryLineEdit->text().trimmed();
    if (directory.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先选择或输入目录路径"));
        return;
    }

    QDir dir(directory);
    if (!dir.exists()) {
        QMessageBox::warning(this, tr("错误"), tr("目录不存在：%1").arg(directory));
        return;
    }

    // 清除现有内容
    clearLayout(fileGridLayout);

    // 扫描目录中的OBJ文件
    QFileInfoList objFiles = scanObjFiles(directory);

    if (objFiles.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("所选目录中未找到OBJ文件"));
        return;
    }

    // 添加文件显示控件
    addFileWidgets(objFiles);
}

QFileInfoList MainWindow::scanObjFiles(const QString &directory)
{
    QDir dir(directory);
    if (!dir.exists()) {
        return QFileInfoList();
    }
    
    // 过滤出所有OBJ文件
    dir.setNameFilters(QStringList() << "*.obj" << "*.OBJ");
    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    
    return dir.entryInfoList();
}

void MainWindow::clearLayout(QLayout *layout)
{
    if (layout == nullptr) return;
    
    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        if (item->layout()) {
            clearLayout(item->layout());
        }
        delete item;
    }
}

void MainWindow::addFileWidgets(const QFileInfoList &objFiles)
{
    int count = objFiles.count();
    int itemsPerRow = 3;
    
    for (int i = 0; i < count; ++i) {
        QFileInfo objFile = objFiles[i];
        
        // 检查是否存在对应的MLS文件
        QString mlsFileName = objFile.fileName().replace(objFile.suffix(), "obj");
        QFileInfo mlsFile(objFile.dir(), mlsFileName);
        bool hasMlsFile = mlsFile.exists() && mlsFile.isFile();
        
        // 创建一个组框来包含文件信息和预览
        QGroupBox *groupBox = new QGroupBox();
        groupBox->setTitle(objFile.fileName());
        
        // 创建垂直布局
        QVBoxLayout *vLayout = new QVBoxLayout(groupBox);
        
        // 添加OBJ文件预览
        ObjViewerWidget *viewerWidget = new ObjViewerWidget();
        viewerWidget->loadObjFile(objFile.filePath());
        viewerWidget->setMinimumHeight(200);
        vLayout->addWidget(viewerWidget);
        
        // 计算行列位置，每行显示3个
        int row = i / itemsPerRow;
        int col = i % itemsPerRow;
        
        // 添加到网格布局
        fileGridLayout->addWidget(groupBox, row, col);
    }
}
