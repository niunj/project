#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QDebug>
#include <zlib.h>

// Copy file to directory
bool copyFileToDir(const QString &sourceFilePath, const QString &destDirPath)
{
    QFileInfo sourceFileInfo(sourceFilePath);
    if (!sourceFileInfo.exists()) {
        qDebug() << "Source file not found:" << sourceFilePath;
        return false;
    }

    QDir destDir(destDirPath);
    if (!destDir.exists()) {
        if (!destDir.mkpath(destDirPath)) {
            qDebug() << "Failed to create directory:" << destDirPath;
            return false;
        }
    }

    QString destFilePath = destDir.filePath(sourceFileInfo.fileName());
    if (QFile::exists(destFilePath)) {
        if (!QFile::remove(destFilePath)) {
            qDebug() << "Failed to remove existing file:" << destFilePath;
            return false;
        }
    }

    if (QFile::copy(sourceFilePath, destFilePath)) {
        qDebug() << "Copied successfully:" << destFilePath;
        return true;
    } else {
        qDebug() << "Copy failed:" << sourceFilePath << "->" << destFilePath;
        return false;
    }
}

// Simple GZIP compression function using zlib
bool compressFileToGzip(const QString &sourceFilePath, const QString &destFilePath);

// Compress a directory with multiple files into a single ZIP archive
bool compressDirectoryToZip(const QString &dirPath, const QString &outputFilePath);

// First copy files to a directory, then compress the entire directory
bool compressToRAR(const QStringList &fileList, const QString &rarFileName)
{
    // Get desktop path for output files
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (desktopPath.isEmpty()) {
        qDebug() << "Failed to get desktop path";
        return false;
    }
    
    // Create a temporary directory
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        qDebug() << "Failed to create temporary directory";
        return false;
    }
    
    // Create a subdirectory with the specified name
    QString targetDirPath = tempDir.path() + QDir::separator() + rarFileName;
    QDir targetDir;
    if (!targetDir.mkpath(targetDirPath)) {
        qDebug() << "Failed to create target directory:" << targetDirPath;
        return false;
    }
    
    // Copy all files to the target directory
    bool allCopied = true;
    foreach (const QString &filePath, fileList) {
        if (!copyFileToDir(filePath, targetDirPath)) {
            qDebug() << "Failed to copy file:" << filePath;
            allCopied = false;
        }
    }
    
    if (!allCopied) {
        qDebug() << "Some files failed to copy";
        return false;
    }
    
    // Create output archive path
    QString outputFilePath = QDir(desktopPath).filePath(rarFileName + ".zip");
    
    // Compress the entire directory
    if (compressDirectoryToZip(targetDirPath, outputFilePath)) {
        qDebug() << "Directory successfully compressed to:" << outputFilePath;
        return true;
    } else {
        qDebug() << "Failed to compress directory";
        return false;
    }
}

// Simple implementation to compress a directory into a ZIP format
bool compressDirectoryToZip(const QString &dirPath, const QString &outputFilePath)
{
    QDir dir(dirPath);
    if (!dir.exists()) {
        qDebug() << "Directory does not exist:" << dirPath;
        return false;
    }
    
    // Get all files in the directory
    QStringList files = dir.entryList(QDir::Files);
    if (files.isEmpty()) {
        qDebug() << "Directory is empty:" << dirPath;
        return false;
    }
    
    // Create output file
    QFile outputFile(outputFilePath);
    if (!outputFile.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to open output file:" << outputFilePath;
        return false;
    }
    
    // Compression buffer
    const int CHUNK_SIZE = 32768;
    char in[CHUNK_SIZE];
    char out[CHUNK_SIZE];
    
    // Process each file in the directory
    bool allCompressed = true;
    foreach (const QString &fileName, files) {
        QString filePath = dir.filePath(fileName);
        QFile sourceFile(filePath);
        
        // Open source file
        if (!sourceFile.open(QIODevice::ReadOnly)) {
            qDebug() << "Failed to open source file:" << filePath;
            allCompressed = false;
            continue;
        }
        
        // Write file entry header (simple format for compatibility)
        // Format: [filename_length(4 bytes)] [filename] [file_size(8 bytes)] [compressed_data]
        
        QByteArray fileNameBytes = fileName.toUtf8();
        quint32 fileNameLen = fileNameBytes.length();
        quint64 fileSize = sourceFile.size();
        
        // Write filename length
        outputFile.write((char*)&fileNameLen, sizeof(quint32));
        // Write filename
        outputFile.write(fileNameBytes);
        // Write original file size
        outputFile.write((char*)&fileSize, sizeof(quint64));
        
        // Initialize zlib for each file (standard DEFLATE, not GZIP)
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        
        if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK) {
            qDebug() << "Failed to initialize zlib compression for" << fileName;
            sourceFile.close();
            outputFile.close();
            return false;
        }
        
        // Compress file content and write to output
        do {
            int bytesRead = sourceFile.read(in, CHUNK_SIZE);
            if (bytesRead < 0) {
                qDebug() << "Error reading from source file:" << filePath;
                deflateEnd(&zs);
                sourceFile.close();
                outputFile.close();
                return false;
            }
            
            zs.avail_in = bytesRead;
            zs.next_in = (Bytef*)in;
            
            do {
                zs.avail_out = CHUNK_SIZE;
                zs.next_out = (Bytef*)out;
                
                int result = deflate(&zs, bytesRead == 0 ? Z_FINISH : Z_NO_FLUSH);
                
                if (result == Z_STREAM_ERROR) {
                    qDebug() << "Zlib stream error during compression";
                    deflateEnd(&zs);
                    sourceFile.close();
                    outputFile.close();
                    return false;
                }
                
                int bytesWritten = CHUNK_SIZE - zs.avail_out;
                if (outputFile.write(out, bytesWritten) != bytesWritten) {
                    qDebug() << "Error writing compressed data";
                    deflateEnd(&zs);
                    sourceFile.close();
                    outputFile.close();
                    return false;
                }
                
            } while (zs.avail_out == 0);
            
        } while (!sourceFile.atEnd());
        
        // Clean up for this file
        deflateEnd(&zs);
        sourceFile.close();
    }
    
    // Close output file
    outputFile.close();
    
    return allCompressed;
}

// Implementation of GZIP compression function
bool compressFileToGzip(const QString &sourceFilePath, const QString &destFilePath)
{
    QFile sourceFile(sourceFilePath);
    QFile destFile(destFilePath);
    
    // Open source file for reading
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open source file:" << sourceFilePath;
        return false;
    }
    
    // Open destination file for writing
    if (!destFile.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to open destination file:" << destFilePath;
        sourceFile.close();
        return false;
    }
    
    // Initialize zlib
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    
    // Create GZIP header
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        qDebug() << "Failed to initialize zlib compression";
        sourceFile.close();
        destFile.close();
        return false;
    }
    
    // Compression buffer
    const int CHUNK_SIZE = 32768;
    char in[CHUNK_SIZE];
    char out[CHUNK_SIZE];
    
    // Process data in chunks
    do {
        // Read chunk from source file
        int bytesRead = sourceFile.read(in, CHUNK_SIZE);
        if (bytesRead < 0) {
            qDebug() << "Error reading from source file";
            deflateEnd(&zs);
            sourceFile.close();
            destFile.close();
            return false;
        }
        
        // Set input buffer for zlib
        zs.avail_in = bytesRead;
        zs.next_in = (Bytef*)in;
        
        // Process until all input is consumed
        do {
            // Set output buffer for zlib
            zs.avail_out = CHUNK_SIZE;
            zs.next_out = (Bytef*)out;
            
            // Compress data
            int result = deflate(&zs, bytesRead == 0 ? Z_FINISH : Z_NO_FLUSH);
            
            if (result == Z_STREAM_ERROR) {
                qDebug() << "Zlib stream error during compression";
                deflateEnd(&zs);
                sourceFile.close();
                destFile.close();
                return false;
            }
            
            // Write compressed data to destination file
            int bytesWritten = CHUNK_SIZE - zs.avail_out;
            if (destFile.write(out, bytesWritten) != bytesWritten) {
                qDebug() << "Error writing compressed data";
                deflateEnd(&zs);
                sourceFile.close();
                destFile.close();
                return false;
            }
            
        } while (zs.avail_out == 0);
        
    } while (!sourceFile.atEnd());
    
    // Clean up
    deflateEnd(&zs);
    sourceFile.close();
    destFile.close();
    
    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    // Sample file list
    QStringList myFileList;
    myFileList << "C:/Users/56333/Desktop/Test/test1.txt"
               << "C:/Users/56333/Desktop/Test/test2.txt"
               << "C:/Users/56333/Desktop/Test/test3.txt";

    // Execute compression
    bool mySuccess = compressToRAR(myFileList, "temp_files");
    
    return mySuccess ? 0 : 1;
}
