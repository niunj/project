#include "readwritefile.h"
#include <QXmlStreamWriter>
#include <QFileInfo>
#include <QTime>
#include <QSettings>
#include <QTextStream>

#include "../Log/log_manager.h"


ReadWriteFile::ReadWriteFile()
{

}

ReadWriteFile& ReadWriteFile::getInstance()
{
	static ReadWriteFile m_instance;
	return m_instance;
}

// void ReadWriteFile::readModel(const QString& strModel)
// {
    // QString strModelType = CommonFunction::getInstance().checkFileType(strModel);
//	if (strModelType == "dat")
//	{
//		readTecplot(strModel);
//	}
//	else if (strModelType == "obj")
//	{
//		parts.clear();
//		readObjData(strModel.toStdString(), parts);
//	}
// }







std::string ReadWriteFile::loadShader(const std::string& filePath)
{
	std::ifstream file(filePath);
	if (!file) {
		std::cerr << "Failed to load shader: " << filePath << std::endl;
		return "";
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();

	return filePath;
}

QVector<ModelTrackData> ReadWriteFile::readTrackFile(QString strTrackFile)
{
	QVector<ModelTrackData> trackData;
	QFile file(strTrackFile);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_DEBUG << "无法打开文件:" << strTrackFile;
	}

	QTextStream in(&file);
	while (!in.atEnd()) {
		QString line = in.readLine();
		if (!line.isEmpty())
		{
			QStringList fields = line.split(',');
			ModelTrackData m_trk;
			m_trk.model_Lon = fields[0].toDouble();
			m_trk.model_Lat = fields[1].toDouble();
			m_trk.model_Alt = fields[2].toDouble();
			m_trk.model_Az = fields[3].toDouble();
			m_trk.model_El = fields[4].toDouble();
			m_trk.model_Roll = fields[5].toDouble();
			m_trk.model_Speed = fields[6].toDouble();

			trackData.push_back(m_trk);
		}
	}

	file.close();

	return trackData;
}

void ReadWriteFile::createMultiCSVTrack(QString strPath, QVector<QVector<ModelTrackData> > trackData)
{
	for (int i = 0; i < trackData.size(); i++)
	{
		QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
		QString fileName = strPath + QString("/%1_%2.csv").arg(timestamp).arg(i);
		QFile file(fileName);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		{
			return;
		}
		QTextStream out(&file);
		out.setCodec("UTF-8");
		for (int j = 0; j < trackData[i].size(); j++)
		{
			out << QString("%1,%2,%3,%4,%5,%6,%7\n").arg(trackData[i][j].model_Lon).arg(trackData[i][j].model_Lat).arg(trackData[i][j].model_Alt)
				.arg(trackData[i][j].model_Az).arg(trackData[i][j].model_El).arg(trackData[i][j].model_Roll).arg(trackData[i][j].model_Speed);
		}

		file.close();
	}
}


QString ReadWriteFile::readConfigPath(const QString& strPath)
{
	QSettings* configIniRead = new QSettings("data/Config/PathConfig.ini", QSettings::IniFormat);

    QString ipResult         = configIniRead->value("path/" + strPath).toString();

	//读入入完成后删除指针
	delete configIniRead;

	return ipResult;
}

