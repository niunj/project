#include "backgroundedit.h"
#include "ui_backgroundedit.h"
#include <QFileDialog>
#include <QDir>

#include <QtConcurrent/QtConcurrent>

#include "../Common/readwritefile.h"


BackGroundEdit::BackGroundEdit(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::BackGroundEdit),
    m_isEditMode(false),
    m_backgroundId(-1)
{
    ui->setupUi(this);

	// 初始化GIF动画
	movie = new QMovie("data/icons/wait/loading.gif");
	label = new QLabel(this);
	label->setFixedSize(QSize(180, 90));
	label->setMovie(movie);
	label->move(this->width() / 2, this->height() / 2);

	// 初始化文件信息提示标签
	m_fileInfoLabel = new QLabel(this);
	m_fileInfoLabel->setWordWrap(true);
	m_fileInfoLabel->setGeometry(10, this->height() - 60, this->width() - 20, 50);
	m_fileInfoLabel->setStyleSheet("QLabel { font-size: 12px; color: #555555; }");

    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
	connect(&Mte3DService::getInstance(), SIGNAL(sig_mousePos(double, double, double)), this, SLOT(setModelPos(double, double, double)));
    connect(this, &BackGroundEdit::sig_loadedFinish, this, &BackGroundEdit::hasAdded);

	// 监听窗口大小变化，调整提示标签位置
    // connect(this, &QWidget::resize, [=]() {
    // 	m_fileInfoLabel->setGeometry(10, this->height() - 60, this->width() - 20, 50);
    // });
}

// 检查并显示相关文件的存在情况
void BackGroundEdit::checkAndDisplayFileInfo(const QString& modelPath)
{
	if (modelPath.isEmpty()) {
		m_fileInfoLabel->clear();
		return;
	}

	QFileInfo modelFileInfo(modelPath);
	QString modelName = modelFileInfo.completeBaseName();
	QString modelDir = modelFileInfo.path();

	// 检查各种文件的存在情况
	bool mlsExists = QFile::exists(modelDir + "/" + modelName + ".mls");
	bool mcmExists = QFile::exists(modelDir + "/" + modelName + ".mcm");
	bool tifExists = QFile::exists(modelDir + "/" + modelName + ".tif");
	bool mshExists = QFile::exists(modelDir + "/" + modelName + ".msh");

	// 构造提示信息
    QString infoText = "相关文件存在情况:\n";
    infoText += "MLS(区域边界): ";
    infoText += mlsExists ? "存在" : "不存在";
    infoText += "\n";

    infoText += "MCM(材质文件): ";
    infoText += mcmExists ? "存在" : "不存在";
    infoText += "\n";

    infoText += "TIF(纹理文件): ";
    infoText += tifExists ? "存在" : "不存在";
    infoText += "\n";

    infoText += "MSH(网格文件): ";
    infoText += mshExists ? "存在" : "不存在";
    infoText += "\n";
	m_fileInfoLabel->setText(infoText);
}

void BackGroundEdit::setEditMode(bool mode)
{
    m_isEditMode = mode;
}
void BackGroundEdit::setSceneEngine(SceneEngine *engine)
{
    m_sceneEngine = engine;
}
void BackGroundEdit::setEditParam(const MtePlatformStru& params)
{
    m_isEditMode = true;
    m_backgroundId = params.m_id;
    
    // 设置窗口标题
    setWindowTitle("修改背景");
    
    // 设置模型路径
    ui->lineEdit_model->setText(params.m_path);
    
    // 设置位置参数
    ui->lineEdit_X->setText(QString::number(params.m_attribute.m_x));
    ui->lineEdit_Y->setText(QString::number(params.m_attribute.m_y));
    ui->lineEdit_Z->setText(QString::number(params.m_attribute.m_z));
    
    // 设置姿态参数
    ui->lineEdit_Az->setText(QString::number(params.m_attribute.m_h));
    ui->lineEdit_El->setText(QString::number(params.m_attribute.m_p));
    ui->lineEdit_Roll->setText(QString::number(params.m_attribute.m_r));
    
    // 设置缩放参数
    ui->lineEdit_scale->setText(QString::number(params.m_attribute.m_scale));
}

BackGroundEdit::~BackGroundEdit()
{
    delete ui;
}

void BackGroundEdit::addBackGround()
{
	QString strModel = ui->lineEdit_model->text();

	MtePlatformStru m_back;
	if (m_isEditMode) {
		// 编辑模式下使用原ID
		m_back.m_id = m_backgroundId;
	} else {
		// 新建模式下生成新ID
        m_back.m_id = m_sceneEngine->getIndex();
	}

    m_back.modelType = BACKGROUND;
    m_back.m_path = strModel;

    m_back.m_attribute.m_x = ui->lineEdit_X->text().toDouble();
    m_back.m_attribute.m_y = ui->lineEdit_Y->text().toDouble();
    m_back.m_attribute.m_z = ui->lineEdit_Z->text().toDouble();
	m_back.m_attribute.m_h = ui->lineEdit_Az->text().toDouble();
	m_back.m_attribute.m_p = ui->lineEdit_El->text().toDouble();
	m_back.m_attribute.m_r = ui->lineEdit_Roll->text().toDouble();
	m_back.modelType = BACKGROUND;
    m_back.m_attribute.m_scale = ui->lineEdit_scale->text().toDouble();

	//根据模型路径自动查找相关文件
	QFileInfo modelFileInfo(strModel);
	QString modelName = modelFileInfo.completeBaseName();
	QString modelDir = modelFileInfo.path();

	//查找同名称的mls文件（区域边界信息）
	QString mlsPath = modelDir + "/" + modelName + ".mls";
	if (QFile::exists(mlsPath)) {
		m_back.mlsPath = mlsPath;
	}

	//查找同名称的tif文件（纹理文件）
	QString tifPath = modelDir + "/" + modelName + ".tif";
	if (QFile::exists(tifPath)) {
		m_back.textPath = tifPath;
	}

	//查找同名称的mcm文件（材质文件）
	QString mcmPath = modelDir + "/" + modelName + ".mcm";
	if (QFile::exists(mcmPath)) {
		m_back.mcmPath = mcmPath;
	}

	//查找同名称的msh文件（网格文件）
	QString mshPath = modelDir + "/" + modelName + ".msh";
	if (QFile::exists(mshPath)) {
		m_back.meshPath = mshPath;
	}


	QFileInfo fileInfo(strModel);
	QString name = fileInfo.completeBaseName();
	m_back.m_name = name;

	if (m_isEditMode) {
        // 编辑模式下更新平台
        emit sig_modifyBackground(m_back);
    } else {
        // 新建模式下添加平台
        emit sig_addBackground(m_back);
    }

}

void BackGroundEdit::hasAdded()
{
	movie->stop();
    label->setHidden(true);
    this->close();
}

void BackGroundEdit::on_Btn_model_clicked()
{
    QString strModel = QFileDialog::getOpenFileName(this, tr("model file"), tr("data/model/"), "(*.obj);;AllFiles(*.*)");
    ui->lineEdit_model->setText(strModel);

    // 检查并显示相关文件的存在情况
    checkAndDisplayFileInfo(strModel);
}


void BackGroundEdit::on_pushButton_OK_clicked()
{
    // label->setHidden(false);
    // movie->start();
    // QtConcurrent::run(this, &BackGroundEdit::addBackGround);

    addBackGround();

    this->close();
}

void BackGroundEdit::on_pushButton_Cancel_clicked()
{
    this->close();
}

void BackGroundEdit::on_checkBox_select_stateChanged(int arg1)
{

}

void BackGroundEdit::setModelPos(double mLon, double mLat, double mAlt)
{
    if(ui->checkBox_select->isChecked())
    {
        ui->lineEdit_X->setText(QString::number(mLon));
        ui->lineEdit_Y->setText(QString::number(mLat));
        ui->lineEdit_Z->setText(QString::number(mAlt));
    }
}
