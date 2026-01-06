#include "terrainedit.h"
#include "ui_terrainedit.h"
#include <QtConcurrent/QtConcurrent>
#include <QFileInfo>

#include "../Common/readwritefile.h"
#include "../Log/log_manager.h"


TerrainEdit::TerrainEdit(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TerrainEdit)
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
	
    connect(&Mte3DService::getInstance(), SIGNAL(sig_mousePos(double, double, double)), this, SLOT(setModelPos(double, double, double)));
	connect(this, &TerrainEdit::sig_loadedFinish, this, &TerrainEdit::hasAdded);

	// 监听窗口大小变化，调整提示标签位置
    // connect(this, &QWidget::resize, [=]() {
    // 	m_fileInfoLabel->setGeometry(10, this->height() - 60, this->width() - 20, 50);
    // });
}

// 检查并显示相关文件的存在情况
void TerrainEdit::checkAndDisplayFileInfo(const QString& modelPath)
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

void TerrainEdit::setEditMode(bool mode)
{
    m_bEdit = mode;
}

void TerrainEdit::setEditParam(const MtePlatformStru& params)
{
    m_bEdit = true;
    m_terrainId = params.m_id;
    
    // 设置窗口标题
    setWindowTitle("修改地形");
    
    // 设置模型路径
    ui->lineEdit_model->setText(params.m_path);
    
    // 设置位置参数
    ui->lineEdit_terrainLon->setText(QString::number(params.m_attribute.m_x));
    ui->lineEdit_terrrainLat->setText(QString::number(params.m_attribute.m_y));
    ui->lineEdit_terrainAlt->setText(QString::number(params.m_attribute.m_z));
    
    // 设置姿态参数
    ui->lineEdit_terrainAz->setText(QString::number(params.m_attribute.m_h));
    ui->lineEdit_terrrainEl->setText(QString::number(params.m_attribute.m_p));
    ui->lineEdit_terrainRoll->setText(QString::number(params.m_attribute.m_r));
}



TerrainEdit::~TerrainEdit()
{
    delete ui;
}

void TerrainEdit::setModelPos(double mLon, double mLat, double mAlt)
{
    if(ui->checkBox->isChecked())
    {
        ui->lineEdit_terrainLon->setText(QString::number(mLon));
        ui->lineEdit_terrrainLat->setText(QString::number(mLat));
        ui->lineEdit_terrainAlt->setText(QString::number(mAlt));
    }
}

void TerrainEdit::hasAdded()
{
	movie->stop();
	label->setHidden(true);
	this->close();
}

void TerrainEdit::on_Btn_model_clicked()
{
    QString strTerrain=QFileDialog::getOpenFileName(this,"open terrain","data/model/Terrain/", "(*.obj);;AllFiles(*.*)");

    if(!strTerrain.isEmpty()) {
       ui->lineEdit_model->setText(strTerrain);
       checkAndDisplayFileInfo(strTerrain);
    }

}


void TerrainEdit::on_pushButton_OK_clicked()
{
    // label->setHidden(false);
    // movie->start();
    // QtConcurrent::run(this, &TerrainEdit::addTerrain);

    addTerrain();
    this->close();
}

void TerrainEdit::on_pushButton_Cancel_clicked()
{
    this->close();
}

void TerrainEdit::addTerrain()
{
	//模型信息
	QString strModel = ui->lineEdit_model->text();

	QFileInfo fileInfo(strModel);
	QString name = fileInfo.baseName();

	MtePlatformStru m_platform;
	if (m_bEdit) {
		// 编辑模式下使用原ID
		m_platform.m_id = m_terrainId;
	} else {
		// 新建模式下生成新ID
        m_platform.m_id = m_sceneEngine->getIndex();
	}
	m_platform.m_name = name;

    m_platform.modelType = GEO;
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

    m_platform.m_attribute.m_x = ui->lineEdit_terrainLon->text().toDouble();
    m_platform.m_attribute.m_y = ui->lineEdit_terrrainLat->text().toDouble();
    m_platform.m_attribute.m_z = ui->lineEdit_terrainAlt->text().toDouble();

    m_platform.m_attribute.m_h   = ui->lineEdit_terrainAz->text().toDouble();
    m_platform.m_attribute.m_p   = ui->lineEdit_terrrainEl->text().toDouble();
    m_platform.m_attribute.m_r   = ui->lineEdit_terrainRoll->text().toDouble();

	m_platform.m_attribute.m_scale = 1;


	if (m_bEdit) {
        // 编辑模式下更新平台
        // Mte3DService::getInstance().updatePlatform(m_platform);
        // Mte3DService::getInstance().generateVisualTexture(m_platform.m_id, strMesh.toStdString());
        
        // CommonFunction::getInstance().updateSceneModels(m_platform);
        emit sig_modifyTerrain(m_platform);
    } else {
        // 新建模式下添加平台
        // Mte3DService::getInstance().addPlatform(m_platform);
        // Mte3DService::getInstance().generateVisualTexture(m_platform.m_id, strMesh.toStdString());
        
        // CommonFunction::getInstance().setSceneModels(m_platform);
        emit sig_addTerrain(m_platform);
    }

    // CommonFunction::getInstance().strModelType(strModelType);

    // emit sig_terrainModel(m_platform);

    // //执行结束
    // if (strModelType != "dat")
    // {
    // 	emit sig_loadedFinish();
    // }

	//发送经纬度
	double mLon = ui->lineEdit_terrainLon->text().toDouble();
	double mLat = ui->lineEdit_terrrainLat->text().toDouble();
	emit sig_LLH(mLon, mLat);
}
void TerrainEdit::setSceneEngine(SceneEngine *engine)
{
    m_sceneEngine = engine;
}
