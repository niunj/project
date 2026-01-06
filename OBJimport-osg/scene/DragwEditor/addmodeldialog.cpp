#include "addmodeldialog.h"
#include <QFileInfo>
#include <QStandardPaths>
#include <QCoreApplication>

AddModelDialog::AddModelDialog(const QString &category, QWidget *parent)
    : QDialog(parent), m_category(category)
{
    setWindowTitle(QString("添加%1模型").arg(category));
    setMinimumWidth(400);

    // 创建布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // 名称输入
    QHBoxLayout *nameLayout = new QHBoxLayout();
    QLabel *nameLabel = new QLabel("名称:");
    m_nameLineEdit = new QLineEdit();
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(m_nameLineEdit);
    mainLayout->addLayout(nameLayout);

    // 图片文件输入
    QHBoxLayout *jpgLayout = new QHBoxLayout();
    QLabel *jpgLabel = new QLabel("图片文件:");
    m_jpgLineEdit = new QLineEdit();
    m_jpgLineEdit->setReadOnly(true);
    m_jpgButton = new QPushButton("浏览");
    jpgLayout->addWidget(jpgLabel);
    jpgLayout->addWidget(m_jpgLineEdit);
    jpgLayout->addWidget(m_jpgButton);
    mainLayout->addLayout(jpgLayout);

    // 模型文件输入
    QHBoxLayout *modelLayout = new QHBoxLayout();
    QLabel *modelLabel = new QLabel("模型文件:");
    m_modelLineEdit = new QLineEdit();
    m_modelLineEdit->setReadOnly(true);
    m_modelButton = new QPushButton("浏览");
    modelLayout->addWidget(modelLabel);
    modelLayout->addWidget(m_modelLineEdit);
    modelLayout->addWidget(m_modelButton);
    mainLayout->addLayout(modelLayout);

    // 纹理文件输入
    QHBoxLayout *textureLayout = new QHBoxLayout();
    QLabel *textureLabel = new QLabel("纹理文件:");
    m_textureLineEdit = new QLineEdit();
    m_textureLineEdit->setReadOnly(true);
    m_textureButton = new QPushButton("浏览");
    textureLayout->addWidget(textureLabel);
    textureLayout->addWidget(m_textureLineEdit);
    textureLayout->addWidget(m_textureButton);
    mainLayout->addLayout(textureLayout);

    // 材质文件输入
    QHBoxLayout *materialLayout = new QHBoxLayout();
    QLabel *materialLabel = new QLabel("材质文件:");
    m_materialLineEdit = new QLineEdit();
    m_materialLineEdit->setReadOnly(true);
    m_materialButton = new QPushButton("浏览");
    materialLayout->addWidget(materialLabel);
    materialLayout->addWidget(m_materialLineEdit);
    materialLayout->addWidget(m_materialButton);
    mainLayout->addLayout(materialLayout);

    // 网格文件输入
    QHBoxLayout *meshLayout = new QHBoxLayout();
    QLabel *meshLabel = new QLabel("网格文件:");
    m_meshLineEdit = new QLineEdit();
    m_meshLineEdit->setReadOnly(true);
    m_meshButton = new QPushButton("浏览");
    meshLayout->addWidget(meshLabel);
    meshLayout->addWidget(m_meshLineEdit);
    meshLayout->addWidget(m_meshButton);
    mainLayout->addLayout(meshLayout);

    // 按钮布局
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_okButton = new QPushButton("确定");
    m_cancelButton = new QPushButton("取消");
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(buttonLayout);

    // 连接信号槽
    connect(m_modelButton, &QPushButton::clicked, this, &AddModelDialog::onModelButtonClicked);
    connect(m_textureButton, &QPushButton::clicked, this, &AddModelDialog::onTextureButtonClicked);
    connect(m_materialButton, &QPushButton::clicked, this, &AddModelDialog::onMaterialButtonClicked);
    connect(m_meshButton, &QPushButton::clicked, this, &AddModelDialog::onMeshButtonClicked);
    connect(m_okButton, &QPushButton::clicked, this, &AddModelDialog::onOkButtonClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &AddModelDialog::onCancelButtonClicked);
}

AddModelDialog::~AddModelDialog()
{
}

QString AddModelDialog::getModelName() const
{
    return m_nameLineEdit->text().trimmed();
}

QString AddModelDialog::getJpgPath() const
{
    return m_jpgLineEdit->text().trimmed();
}

QString AddModelDialog::getModelPath() const
{
    return m_modelLineEdit->text().trimmed();
}

QString AddModelDialog::getTexturePath() const
{
    return m_textureLineEdit->text().trimmed();
}

QString AddModelDialog::getMaterialPath() const
{
    return m_materialLineEdit->text().trimmed();
}

QString AddModelDialog::getMeshPath() const
{
    return m_meshLineEdit->text().trimmed();
}

QString AddModelDialog::getCategory() const
{
    return m_category;

}


void AddModelDialog::onJpgButtonClicked()
{
    QString fileName = QFileDialog::getOpenFileName(
        this, "选择图片文件", "", "图片文件 (*.jgp)");
    if (!fileName.isEmpty()) {
        m_jpgLineEdit->setText(fileName);
    }
}

void AddModelDialog::onModelButtonClicked()
{
    QString fileName = QFileDialog::getOpenFileName(
        this, "选择模型文件", "", "模型文件 (*.obj *.flt)");
    if (!fileName.isEmpty()) {
        m_modelLineEdit->setText(fileName);
    }
}

void AddModelDialog::onTextureButtonClicked()
{
    QString fileName = QFileDialog::getOpenFileName(
        this, "选择纹理文件", "", "纹理文件 (*.mls)");
    if (!fileName.isEmpty()) {
        m_textureLineEdit->setText(fileName);
    }
}

void AddModelDialog::onMaterialButtonClicked()
{
    QString fileName = QFileDialog::getOpenFileName(
        this, "选择材质文件", "", "材质文件 (*.mcm)");
    if (!fileName.isEmpty()) {
        m_materialLineEdit->setText(fileName);
    }
}

void AddModelDialog::onMeshButtonClicked()
{
    QString fileName = QFileDialog::getOpenFileName(
        this, "选择网格文件", "", "网格文件 (*.msh)");
    if (!fileName.isEmpty()) {
        m_meshLineEdit->setText(fileName);
    }
}

void AddModelDialog::onOkButtonClicked()
{
    QString modelName = getModelName();
    QString modelPath = getModelPath();

    // 验证名称和模型文件
    if (modelName.isEmpty()) {
        QMessageBox::warning(this, "错误", "请输入模型名称");
        return;
    }

    if (modelPath.isEmpty()) {
        QMessageBox::warning(this, "错误", "请选择模型文件");
        return;
    }

    // 验证模型名称是否合法
    if (!QRegExp("^[a-zA-Z0-9_]+$").exactMatch(modelName)) {
        QMessageBox::warning(this, "错误", "模型名称只能包含字母、数字和下划线");
        return;
    }

    // 构造目标目录
    QString baseDir = QCoreApplication::applicationDirPath() + "/data/model/" + m_category;
    QString targetDir = baseDir + "/" + modelName;

    // 创建目标目录
    QDir dir;
    if (!dir.mkpath(targetDir)) {
        QMessageBox::critical(this, "错误", "无法创建目标目录");
        return;
    }

    // 复制文件的辅助函数
    // auto copyFile = [&](const QString &sourcePath, const QString &newExtension) {
    //     if (sourcePath.isEmpty()) return;
        
    //     QString newFileName = targetDir + "/" + modelName + "." + newExtension;
    //     if (!QFile::copy(sourcePath, newFileName)) {
    //         QMessageBox::warning(this, "警告", "无法复制文件: " + QFileInfo(sourcePath).fileName());
    //     }
    // };

    // 复制文件
    // copyFile(modelPath, QFileInfo(modelPath).suffix());
    // copyFile(getJpgPath(), "jpg");
    // copyFile(getTexturePath(), "mls");
    // copyFile(getMaterialPath(), "mcm");
    // copyFile(getMeshPath(), "msh");

    // 创建预览图片（如果需要）
    // TODO: 可以在这里添加生成预览图片的逻辑

    accept();
}

void AddModelDialog::onCancelButtonClicked()
{
    reject();
}
