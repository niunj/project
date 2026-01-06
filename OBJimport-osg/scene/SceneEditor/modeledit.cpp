#include "modeledit.h"
#include "ui_modeledit.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include <QDir>


//#include "ThermalCalculate/mtethermalcalculate.h"
#include "../Log/log_manager.h"

ModelEdit::ModelEdit(QWidget* parent) :
	QWidget(parent),
	ui(new Ui::ModelEdit),
	m_isEditMode(false),
	m_modelId(-1)
{
	ui->setupUi(this);

	setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
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

	connect(ui->Btn_model, &QPushButton::clicked, this, &ModelEdit::loadModel);
	connect(ui->pushButton_OK, &QPushButton::clicked, this, &ModelEdit::setModel);
	connect(ui->pushButton_Cancel, &QPushButton::clicked, [=]() {this->close(); });
	connect(this, &ModelEdit::sig_loadedFinish, this, &ModelEdit::hasAdded);

	connect(ui->checkBox_vehicleExhaust, &QCheckBox::stateChanged, this, &ModelEdit::checkStateChange);

}

// 检查并显示相关文件的存在情况
void ModelEdit::checkAndDisplayFileInfo(const QString& modelPath)
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

void ModelEdit::setEditMode(bool mode)
{
    m_isEditMode = mode;
}

void ModelEdit::setEditParam(const MtePlatformStru& params)
{
    m_isEditMode = true;
    m_modelId = params.m_id;
    
    // 设置窗口标题
    setWindowTitle("修改模型");
    
    // 设置模型名称
    ui->lineEdit_name->setText(params.m_name);
    
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
    
    // 设置排气口参数
    // ui->checkBox_vehicleExhaust->setChecked(params.isExhaust);
    // checkStateChange(params.isExhaust ? Qt::Checked : Qt::Unchecked);
}

ModelEdit::~ModelEdit()
{
	delete ui;
}

void ModelEdit::loadModel()
{
    QString strModel = QFileDialog::getOpenFileName(this, tr("model file"), tr("data/model/"), "(*.obj);;AllFiles(*.*)");
	ui->lineEdit_model->setText(strModel);
	QFileInfo fileInfo(strModel);
	QString name = fileInfo.completeBaseName();
	ui->lineEdit_name->setText(name);

	// 检查并显示相关文件的存在情况
	checkAndDisplayFileInfo(strModel);
}


void ModelEdit::addModel()
{
	QString m_lon = ui->lineEdit_X->text();
	QString m_lat = ui->lineEdit_Y->text();
	QString m_alt = ui->lineEdit_Z->text();

	if (m_lon.isEmpty())
	{
		ui->lineEdit_X->setPlaceholderText("请输入经度");
		return;
	}
	if (m_lat.isEmpty())
	{
		ui->lineEdit_Y->setPlaceholderText("请输入纬度");
		return;
	}
	if (m_alt.isEmpty())
	{
		ui->lineEdit_Z->setPlaceholderText("请输入高度");
		return;
	}

	//模型信息
	QString strName = ui->lineEdit_name->text();
	QString strModel = ui->lineEdit_model->text();

	MtePlatformStru m_platform;
	m_platform.m_name = strName;
	if (m_isEditMode) {
		// 编辑模式下使用原ID
		m_platform.m_id = m_modelId;
	} else {
		// 新建模式下生成新ID
        m_platform.m_id = m_sceneEngine->getIndex();
	}

    m_platform.modelType = OBJECT;
    m_platform.m_path    = strModel;

	//根据模型路径自动查找相关文件
	QFileInfo modelFileInfo(strModel);
	QString modelName = modelFileInfo.completeBaseName();
	QString modelDir = modelFileInfo.path();

	//查找同名称的mls文件（区域边界信息）
	QString mlsPath = modelDir + "/" + modelName + ".mls";
	if (QFile::exists(mlsPath)) {
		m_platform.mlsPath = mlsPath;
	}

	//查找同名称的tif文件（纹理文件）
	QString tifPath = modelDir + "/" + modelName + ".tif";
	if (QFile::exists(tifPath)) {
		m_platform.textPath = tifPath;
	}

	//查找同名称的mcm文件（材质文件）
	QString mcmPath = modelDir + "/" + modelName + ".mcm";
	if (QFile::exists(mcmPath)) {
		m_platform.mcmPath = mcmPath;
	}

	//查找同名称的msh文件（网格文件）
	QString mshPath = modelDir + "/" + modelName + ".msh";
	if (QFile::exists(mshPath)) {
		m_platform.meshPath = mshPath;
	}

    m_platform.m_attribute.m_x   = ui->lineEdit_X->text().toDouble();
    m_platform.m_attribute.m_y   = ui->lineEdit_Y->text().toDouble();
    m_platform.m_attribute.m_z   = ui->lineEdit_Z->text().toDouble();
    m_platform.m_attribute.m_h     = ui->lineEdit_Az->text().toDouble();
    m_platform.m_attribute.m_p     = ui->lineEdit_El->text().toDouble();
    m_platform.m_attribute.m_r     = ui->lineEdit_Roll->text().toDouble();
	m_platform.m_attribute.m_scale = ui->lineEdit_scale->text().toDouble();


	m_platform.m_attribute.m_scale = 1;

	if (m_isEditMode) {
		// 编辑模式下更新平台
		emit sig_modifyModel(m_platform);
	} else {
		// 新建模式下添加平台
		emit sig_addModel(m_platform);
	}

}

void ModelEdit::setModel()
{
	addModel();

    // 直接关闭界面
    this->close();
}

void ModelEdit::hasAdded()
{
	movie->stop();
	label->setHidden(true);
	this->close();
}

void ModelEdit::checkStateChange(int state)
{
	if (ui->checkBox_vehicleExhaust->isChecked())
	{
		isExhus = true;	
		ui->lineEdit_offsetX->setEnabled(true);
		ui->lineEdit_offsetY->setEnabled(true);
		ui->lineEdit_offsetZ->setEnabled(true);
	}
	else
	{
		isExhus = false;
		ui->lineEdit_offsetX->setEnabled(false);
		ui->lineEdit_offsetY->setEnabled(false);
		ui->lineEdit_offsetZ->setEnabled(false);
	}
}
void ModelEdit::setSceneEngine(SceneEngine *engine)
{
    m_sceneEngine = engine;
}

void ModelEdit::setModelPos(double mLon, double mLat, double mAlt)
{
	if (ui->checkBox_mouse->isChecked())
	{
		ui->lineEdit_X->setText(QString::number(mLon, 'f', 6));
		ui->lineEdit_Y->setText(QString::number(mLat, 'f', 6));
		ui->lineEdit_Z->setText(QString::number(mAlt, 'f', 6));
	}
}
