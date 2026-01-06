#include "modeltrackedit.h"
#include "ui_modeltrackedit.h"
#include <QFileDialog>
#include <QtConcurrent/QtConcurrent>
#include <QVector4D>
#include "../Common/MteStructDef.h"
#include "../SceneEngine/sceneengine.h"

ModelTrackEdit::ModelTrackEdit(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ModelTrackEdit),
    m_sceneEngine(nullptr)
{
    ui->setupUi(this);

    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget_self->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setStyleSheet("QHeaderView::section{background:skyblue;}");
    ui->tableWidget_self->horizontalHeader()->setStyleSheet("QHeaderView::section{background:skyblue;}");

    on_lineEdit_keyNum_editingFinished();

    connect(ui->Btn_Ok, &QPushButton::clicked, this, &ModelTrackEdit::setModelTrack);
    connect(ui->Btn_Cancel, &QPushButton::clicked, this, &ModelTrackEdit::cancelTrack);
}

void ModelTrackEdit::setSceneEngine(SceneEngine *engine)
{
    m_sceneEngine = engine;
}

void ModelTrackEdit::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    
    // 当窗口显示时，自动加载当前平台的航迹信息
    if (model_ID != 0 && m_sceneEngine) {
        // 获取当前平台的航迹数据
        QVector<std::tuple<double, double, double, double, double, double>> trackData;
        
        // 首先检查是否已经有轨迹数据
        // QVector<QVector3D> existingTrack = m_sceneEngine->getTrackData(model_ID);
        // if (!existingTrack.isEmpty()) {
        //     // 将现有轨迹数据填充到表格中
        //     ui->tableWidget->setRowCount(existingTrack.size());
        //     for (int i = 0; i < existingTrack.size(); i++) {
        //         ui->tableWidget->setItem(i, 0, new QTableWidgetItem(QString::number(existingTrack[i].x(), 'f', 6)));
        //         ui->tableWidget->setItem(i, 1, new QTableWidgetItem(QString::number(existingTrack[i].y(), 'f', 6)));
        //         ui->tableWidget->setItem(i, 2, new QTableWidgetItem(QString::number(existingTrack[i].z(), 'f', 6)));
                
        //         // 默认值
        //         ui->tableWidget->setItem(i, 3, new QTableWidgetItem(QString::number(0)));
        //         ui->tableWidget->setItem(i, 4, new QTableWidgetItem(QString::number(0)));
        //         ui->tableWidget->setItem(i, 5, new QTableWidgetItem(QString::number(0)));
        //         ui->tableWidget->setItem(i, 6, new QTableWidgetItem(QString::number(10)));
        //     }
        // }
    }
}

ModelTrackEdit::~ModelTrackEdit()
{
    delete ui;
}

void ModelTrackEdit::setModelTrack()
{
    int rows = ui->tableWidget->rowCount();
    
    if (rows == 0) {
        // 如果没有轨迹点，默认设置一个简单轨迹
        // Mte3DService::getInstance().setTrackForPlatform(model_ID);
        
        // 创建默认的MteTrackStru对象
        // MteTrackStru track;
        // track.trackColor = Qt::red;
        // track.trackWidth = 2.0;
        // track.isVisible = true;
        
        // emit sig_addTrack(model_ID, track);
    }
    else
    {
        // 提取表格中的轨迹数据
        MteTrackStru track;
        track.trackColor = Qt::red;      // 默认红色
        track.trackWidth = 2.0;          // 默认线宽2.0
        track.isVisible  = true;         // 默认可见
        
        for (int i = 0; i < rows; i++) {
            ModelTrackData trackPoint;
            trackPoint.model_Lon   = ui->tableWidget->item(i, 0)->text().toDouble();
            trackPoint.model_Lat   = ui->tableWidget->item(i, 1)->text().toDouble();
            trackPoint.model_Alt   = ui->tableWidget->item(i, 2)->text().toDouble();
            trackPoint.model_Az    = ui->tableWidget->item(i, 3)->text().toDouble();
            trackPoint.model_El    = ui->tableWidget->item(i, 4)->text().toDouble();
            trackPoint.model_Roll  = ui->tableWidget->item(i, 5)->text().toDouble();
            trackPoint.model_Speed = ui->tableWidget->item(i, 6)->text().toDouble();
            
            track.trackData.push_back(trackPoint);
        }
        
        // 检查是否已经存在轨迹
        // if (m_sceneEngine) {
        //     QVector<QVector3D> existingTrack = m_sceneEngine->getTrackData(model_ID);
        //     if (existingTrack.isEmpty()) {
        //         // 添加新轨迹
        //         emit sig_addTrack(model_ID, track);
        //     } else {
        //         // 修改现有轨迹
        //         emit sig_modifyTrack(model_ID, track);
        //     }
        // }
    }
    
    this->close();
}


void ModelTrackEdit::cancelTrack()
{
    this->close();
}


void ModelTrackEdit::setModelID(int modelID)
{
    model_ID = modelID;
}

void ModelTrackEdit::endTrackClicked()
{
    this->show();

    // trackData.clear();

    // QVector <QVector3D>trackData = Mte3DService::getInstance().getTrackData(model_ID);

    // ui->tableWidget->setRowCount(trackData.size());
    // for (int i = 0; i < trackData.size(); i++)
    // {
    //     ui->tableWidget->setItem(i, 0, new QTableWidgetItem(QString::number(trackData[i].x(), 'f', 6)));
    //     ui->tableWidget->setItem(i, 1, new QTableWidgetItem(QString::number(trackData[i].y(), 'f', 6)));
    //     ui->tableWidget->setItem(i, 2, new QTableWidgetItem(QString::number(trackData[i].z(), 'f', 6)));

    //     ui->tableWidget->setItem(i, 3, new QTableWidgetItem(QString::number(0)));
    //     ui->tableWidget->setItem(i, 4, new QTableWidgetItem(QString::number(0)));
    //     ui->tableWidget->setItem(i, 5, new QTableWidgetItem(QString::number(0)));

    //     ui->tableWidget->setItem(i, 6, new QTableWidgetItem(QString::number(10)));
    // }
}

void ModelTrackEdit::closeEvent(QCloseEvent* event)
{
    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);
}

void ModelTrackEdit::on_pushButton_clicked()
{
    int tracksNum=ui->lineEdit_tracks->text().toInt();
    double trackInterval=ui->lineEdit_step->text().toDouble();
    int rows=ui->lineEdit_keyNum->text().toInt();

    QVector<QVector<ModelTrackData>>trackData;
    for(int i=0;i<tracksNum;i++)
    {
        QVector<ModelTrackData>m_trk;
        for (int j = 0; j < rows; j++)
        {
            ModelTrackData modelInfo;
            modelInfo.model_Lon = ui->tableWidget_self->item(j, 0)->text().toDouble();
            modelInfo.model_Lat = ui->tableWidget_self->item(j, 1)->text().toDouble();
            modelInfo.model_Alt = ui->tableWidget_self->item(j, 2)->text().toDouble();
            modelInfo.model_Az = ui->tableWidget_self->item(j, 3)->text().toDouble();
            modelInfo.model_El = ui->tableWidget_self->item(j, 4)->text().toDouble();
            modelInfo.model_Roll = ui->tableWidget_self->item(j, 5)->text().toDouble();
            modelInfo.model_Speed = ui->tableWidget_self->item(j, 6)->text().toDouble();


            //第一个点
            double outLonA, outLatA;
            // osgEarth::GeoMath::destination(osg::DegreesToRadians(modelInfo.model_Lat), osg::DegreesToRadians(modelInfo.model_Lon),
            //                                osg::DegreesToRadians(90.0), i*trackInterval, outLatA, outLonA);
            modelInfo.model_Lon=osg::RadiansToDegrees(outLonA);
            modelInfo.model_Lat=osg::RadiansToDegrees(outLatA);

            m_trk.push_back(modelInfo);
        }

        trackData.push_back(m_trk);
    }

    QString dirPath = QFileDialog::getExistingDirectory(this, "选择路径", "");

    //批量生成轨迹
    ReadWriteFile::getInstance().createMultiCSVTrack(dirPath,trackData);
}

void ModelTrackEdit::on_radioButton_map_clicked()
{
    ui->stackedWidget->setCurrentIndex(0);
}

void ModelTrackEdit::on_radioButton_file_clicked()
{
    ui->stackedWidget->setCurrentIndex(1);
}

void ModelTrackEdit::on_radioButton_self_clicked()
{
    ui->stackedWidget->setCurrentIndex(2);
}

void ModelTrackEdit::on_pushButton_file_clicked()
{
    QString strFile                 = QFileDialog::getOpenFileName(this,"open file","data/track/","*.csv");
    ui->lineEdit_filePath->setText(strFile);

    QVector<ModelTrackData> trkData = ReadWriteFile::getInstance().readTrackFile(strFile);
    QVector<QVector4D>      keyPoints;
    QVector<QVector3D>      linePoints;
    for(int i=0;i<trkData.size();i++)
    {
        keyPoints.push_back(QVector4D(trkData[i].model_Lon, trkData[i].model_Lat, trkData[i].model_Alt, trkData[i].model_Speed));
        linePoints.push_back(QVector3D(trkData[i].model_Lon, trkData[i].model_Lat, trkData[i].model_Alt));
    }

    //绘制新轨迹
    // Mte3DService::getInstance().drawLine(model_ID, linePoints);

    // MtePlatformStru m_info = Mte3DService::getInstance().getPlatformInfo(model_ID);
    // QtConcurrent::run(this, &ModelTrackEdit::setModelTrackInfo, model_ID, keyPoints);


}

void ModelTrackEdit::on_lineEdit_keyNum_editingFinished()
{
    int rows=ui->lineEdit_keyNum->text().toInt();
    ui->tableWidget_self->setRowCount(rows);
}

void ModelTrackEdit::setModelPos(double mLon, double mLat, double mAlt)
{
    if (ui->checkBox->isChecked())
    {
        ui->tableWidget_self->setItem(tableID, 0, new QTableWidgetItem(QString::number(mLon, 'f', 6)));
        ui->tableWidget_self->setItem(tableID, 1, new QTableWidgetItem(QString::number(mLat, 'f', 6)));
        ui->tableWidget_self->setItem(tableID, 2, new QTableWidgetItem(QString::number(mAlt, 'f', 6)));

        ui->tableWidget_self->setItem(tableID, 3, new QTableWidgetItem(QString::number(0)));
        ui->tableWidget_self->setItem(tableID, 4, new QTableWidgetItem(QString::number(0)));
        ui->tableWidget_self->setItem(tableID, 5, new QTableWidgetItem(QString::number(0)));

        ui->tableWidget_self->setItem(tableID, 6, new QTableWidgetItem(QString::number(10)));

        tableID++;
    }

    if (tableID== ui->lineEdit_keyNum->text().toInt())
    {
        ui->checkBox->setCheckState(Qt::Unchecked);
        tableID = 0;
    }
}
