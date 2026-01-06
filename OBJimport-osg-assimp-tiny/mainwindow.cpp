#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cexport.h>
#include "TinyObj/tiny_obj_loader.h"
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <osgGA/TrackballManipulator>
#include <osg/Texture2D>
#include <osg/Material>
#include <osgDB/ReadFile>
#include <osgUtil/Optimizer>

// 模型转换辅助函数前向声明
osg::ref_ptr<osg::Node> convertAssimpToOSG(const aiScene* scene, const aiVector3D& position, const QString& filePath);
osg::ref_ptr<osg::Node> convertTinyObjToOSG(const tinyobj::attrib_t& attrib, const std::vector<tinyobj::shape_t>& shapes, const std::vector<tinyobj::material_t>& materials, const aiVector3D& position, const QString& filePath);

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , assimpLoadCount(0)
    , tinyobjLoadCount(0)
    , osgLoadCount(0)
    , defaultObjPath("data/BTR-70.obj")
    , assimpPositionOffset(0.0f)
    , tinyobjPositionOffset(0.0f)
    , osgPositionOffset(0.0f)
    , assimpViewerWidget(nullptr)
    , tinyobjViewerWidget(nullptr)
    , osgViewerWidget(nullptr)
{
    ui->setupUi(this);
    setWindowTitle("OBJ Import/Export Tool");
    
    // 初始化OSG渲染器
    setupOSGViewers();
    
    updateCountLabels();
}

MainWindow::~MainWindow()
{
    // 释放Assimp模型资源
    for (auto& model : assimpModels) {
        if (model.scene) {
            aiFreeScene(model.scene);
        }
    }
    
    // 释放OSG渲染相关资源
    if (assimpViewerWidget) {
        delete assimpViewerWidget;
    }
    if (tinyobjViewerWidget) {
        delete tinyobjViewerWidget;
    }
    if (osgViewerWidget) {
        delete osgViewerWidget;
    }
    
    delete ui;
}

void MainWindow::updateCountLabels()
{
    ui->assimpCountLabel->setText(QString("Assimp Load Count: %1").arg(assimpLoadCount));
    ui->tinyobjCountLabel->setText(QString("TinyObj Load Count: %1").arg(tinyobjLoadCount));
    
    // 检查是否存在osgCountLabel，如果存在则更新
    QLabel* osgCountLabel = ui->osgCountLabel;
    if (osgCountLabel) {
        osgCountLabel->setText(QString("OSG Load Count: %1").arg(osgLoadCount));
    }
}

// ********** Assimp导入流程 **********
void MainWindow::on_assimpReadBtn_clicked()
{
    // 使用默认文件路径（data/BTR-70.obj）
    QString filePath = defaultObjPath;

    // 创建Assimp导入器实例
    Assimp::Importer importer;
    
    // 读取OBJ文件，应用三角化、翻转UV和计算切线空间等后处理
    const aiScene* scene = importer.ReadFile(filePath.toStdString().c_str(), 
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);

    // 检查导入是否成功
    if (!scene || !scene->mRootNode)
    {
        QMessageBox::critical(this, "Error", QString("Assimp Read Error: %1\nFile: %2").arg(importer.GetErrorString(), filePath));
        return;
    }

    // 创建可修改的场景副本（原始场景是const的）
    aiScene* copyScene = nullptr;
    aiCopyScene(scene, &copyScene);
    
    // 检查场景复制是否成功
    if (!copyScene)
    {
        QMessageBox::critical(this, "Error", "Failed to copy scene");
        return;
    }

    // 为当前模型设置位置偏移（每次递增5.0f）
    aiVector3D currentPosition(assimpPositionOffset, 0.0f, 0.0f);
    aiMatrix4x4 transform;
    
    // 创建平移变换矩阵
    aiMatrix4x4::Translation(currentPosition, transform);
    
    // 保存原始变换矩阵
    aiMatrix4x4 originalTransform = copyScene->mRootNode->mTransformation;
    
    // 应用新的变换矩阵（矩阵在前，向量在后）
    copyScene->mRootNode->mTransformation = transform * originalTransform;
    
    // 存储模型数据到向量中
    AssimpModelData modelData;
    modelData.scene = copyScene;       // 存储场景指针
    modelData.position = currentPosition;  // 存储位置偏移
    assimpModels.push_back(modelData);
    
    // 更新加载计数和UI显示
    assimpLoadCount++;
    updateCountLabels();
    
    // 递增位置偏移，确保下次加载的模型位置不同
    assimpPositionOffset += 5.0f;
    
    // 将Assimp模型转换为OSG节点并添加到场景中
    osg::ref_ptr<osg::Node> osgNode = convertAssimpToOSG(copyScene, currentPosition, filePath);
    if (osgNode) {
        assimpSceneRoot->addChild(osgNode);
    }

    // 显示成功信息
    QMessageBox::information(this, "Success", QString("Assimp Read Success!\nFile: %1\nMeshes: %2\nObjects: %3\nLoad Count: %4\nPosition Offset: %5\nTotal Models: %6").arg(
        filePath, QString::number(copyScene->mNumMeshes), QString::number(copyScene->mRootNode->mNumChildren), 
        QString::number(assimpLoadCount), QString::number(currentPosition.x), QString::number(assimpModels.size())));
}

// ********** Assimp导出流程 **********
// 功能：将所有加载的Assimp模型合并并导出为单个OBJ文件
// 每个模型会被放置在正确的偏移位置
void MainWindow::on_assimpWriteBtn_clicked()
{
    // 检查是否有模型需要导出
    if (assimpModels.empty())
    {
        QMessageBox::warning(this, "Warning", "No Assimp models loaded. Please load some models first.");
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this, "Save OBJ File", "", "OBJ Files (*.obj);;All Files (*.*)");
    if (filePath.isEmpty())
        return;

    std::ofstream outFile(filePath.toStdString().c_str());
    if (!outFile.is_open())
    {
        QMessageBox::critical(this, "Error", "Failed to open file for writing");
        return;
    }

    // 写入文件头
    outFile << "# Merged OBJ file with multiple Assimp models\n";
    outFile << "# Total models: " << assimpModels.size() << "\n\n";

    // 记录当前顶点、法线、纹理坐标的起始索引
    unsigned int vertexOffset = 1; // OBJ文件索引从1开始
    unsigned int normalOffset = 1;
    unsigned int texOffset    = 1;

    // 遍历所有Assimp模型
    for (size_t modelIndex = 0; modelIndex < assimpModels.size(); ++modelIndex)
    {
        const auto& model = assimpModels[modelIndex];
        const aiScene* scene = model.scene;
        
        outFile << "# Model " << modelIndex << " - Position: (" 
                << model.position.x << ", " << model.position.y << ", " << model.position.z << ")\n\n";

        // 遍历所有网格
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
        {
            const aiMesh* mesh = scene->mNumMeshes > meshIndex ? scene->mMeshes[meshIndex] : nullptr;
            if (!mesh) continue;

            // 遍历所有顶点，应用变换矩阵
            for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
            {
                // 获取顶点位置
                aiVector3D vertex = mesh->mVertices[i];
                
                // 应用变换矩阵（包含模型的位置偏移）
                aiMatrix4x4 transform = model.scene->mRootNode->mTransformation;
                vertex = transform * vertex;
                
                outFile << "v " << vertex.x << " " << vertex.y << " " << vertex.z << "\n";
            }

            // 遍历所有法线，应用变换矩阵
            if (mesh->HasNormals())
            {
                for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
                {
                    aiVector3D normal = mesh->mNormals[i];
                    // 法线需要应用变换矩阵的旋转部分（去除平移和缩放）
                    aiMatrix3x3 normalMatrix = aiMatrix3x3(model.scene->mRootNode->mTransformation);
                    normalMatrix.Inverse().Transpose();
                    normal = normalMatrix * normal;
                    
                    outFile << "vn " << normal.x << " " << normal.y << " " << normal.z << "\n";
                }
            }

            // 遍历所有纹理坐标
            if (mesh->HasTextureCoords(0))
            {
                for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
                {
                    aiVector3D texCoord = mesh->mTextureCoords[0][i];
                    outFile << "vt " << texCoord.x << " " << texCoord.y << "\n";
                }
            }

            // 遍历所有面，重映射索引
            for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
            {
                const aiFace& face = mesh->mFaces[i];
                
                outFile << "f ";
                for (unsigned int j = 0; j < face.mNumIndices; ++j)
                {
                    unsigned int index = face.mIndices[j];
                    
                    // 顶点索引
                    outFile << (vertexOffset + index);
                    
                    // 纹理坐标索引
                    if (mesh->HasTextureCoords(0))
                    {
                        outFile << "/" << (texOffset + index);
                    }
                    else
                    {
                        outFile << "/";
                    }
                    
                    // 法线索引
                    if (mesh->HasNormals())
                    {
                        outFile << "/" << (normalOffset + index);
                    }
                    
                    if (j < face.mNumIndices - 1)
                    {
                        outFile << " ";
                    }
                }
                outFile << "\n";
            }
        }

        // 更新索引偏移，正确统计每个网格的顶点数
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
        {
            const aiMesh* mesh = scene->mNumMeshes > meshIndex ? scene->mMeshes[meshIndex] : nullptr;
            if (!mesh) continue;
            
            vertexOffset += mesh->mNumVertices;
            if (mesh->HasNormals())
            {
                normalOffset += mesh->mNumVertices;
            }
            if (mesh->HasTextureCoords(0))
            {
                texOffset += mesh->mNumVertices;
            }
        }
    }

    outFile.close();

    QMessageBox::information(this, "Success", QString("Assimp Write Success!\nFile: %1\nTotal Models: %2").arg(filePath, QString::number(assimpModels.size())));
}

// ********** TinyObj导入流程 **********
// 功能：使用TinyObj库读取OBJ文件，并存储带有位置偏移的模型数据
// 每次读取会分配不同的位置偏移，确保模型在不同位置
void MainWindow::on_tinyobjReadBtn_clicked()
{
    // 使用默认文件路径（data/BTR-70.obj）
    QString filePath = defaultObjPath;

    // 初始化TinyObj读取器和配置
    tinyobj::ObjReaderConfig reader_config;
    tinyobj::ObjReader reader;

    // 读取OBJ文件
    if (!reader.ParseFromFile(filePath.toStdString().c_str(), reader_config))
    {
        // 显示读取错误信息
        if (!reader.Error().empty())
        {
            QMessageBox::critical(this, "Error", QString("TinyObj Read Error: %1\nFile: %2").arg(reader.Error().c_str(), filePath));
        }
        return;
    }

    // 显示读取警告信息（如果有）
    if (!reader.Warning().empty())
    {
        QMessageBox::warning(this, "Warning", QString("TinyObj Read Warning: %1").arg(reader.Warning().c_str()));
    }

    // 为当前模型生成位置偏移（X轴方向，每次递增5.0f）
    aiVector3D currentPosition(tinyobjPositionOffset, 0.0f, 0.0f);
    
    // 存储模型数据到自定义结构体中
    TinyObjModelData modelData;
    modelData.attrib = reader.GetAttrib();      // 顶点、法线、纹理坐标等属性
    modelData.shapes = reader.GetShapes();      // 形状数据，包含面索引
    modelData.materials = reader.GetMaterials(); // 材质数据
    modelData.position = currentPosition;       // 当前模型的位置偏移
    tinyobjModels.push_back(modelData);         // 添加到模型列表
    
    // 更新加载计数和UI显示
    tinyobjLoadCount++;
    updateCountLabels();
    
    // 递增位置偏移，确保下次加载的模型位置不同
    tinyobjPositionOffset += 5.0f;
    
    // 将TinyObj模型转换为OSG节点并添加到场景中
    osg::ref_ptr<osg::Node> osgNode = convertTinyObjToOSG(modelData.attrib, modelData.shapes, modelData.materials, currentPosition, filePath);
    if (osgNode) {
        tinyobjSceneRoot->addChild(osgNode);
    }

    // 显示成功信息，包含模型统计数据
    QMessageBox::information(this, "Success", QString("TinyObj Read Success!\nFile: %1\nShapes: %2\nMaterials: %3\nVertices: %4\nLoad Count: %5\nPosition Offset: %6\nTotal Models: %7").arg(
        filePath, QString::number(modelData.shapes.size()), QString::number(modelData.materials.size()), 
        QString::number(modelData.attrib.vertices.size() / 3), QString::number(tinyobjLoadCount), 
        QString::number(currentPosition.x), QString::number(tinyobjModels.size())));
}

// ********** TinyObj导出流程 **********
// 功能：将所有加载的TinyObj模型合并并导出为单个OBJ文件
// 每个模型会被放置在正确的偏移位置，通过直接修改顶点坐标实现
void MainWindow::on_tinyobjWriteBtn_clicked()
{
    // 检查是否有模型需要导出
    if (tinyobjModels.empty())
    {
        QMessageBox::warning(this, "Warning", "No TinyObj models loaded. Please load some models first.");
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this, "Save OBJ File", "", "OBJ Files (*.obj);;All Files (*.*)");
    if (filePath.isEmpty())
        return;

    std::ofstream outFile(filePath.toStdString().c_str());
    if (!outFile.is_open())
    {
        QMessageBox::critical(this, "Error", "Failed to open file for writing");
        return;
    }

    // 写入文件头
    outFile << "# Merged OBJ file with multiple models\n";
    outFile << "# Total models: " << tinyobjModels.size() << "\n\n";

    // 记录当前顶点、法线、纹理坐标的起始索引
    unsigned int vertexOffset = 1; // OBJ文件索引从1开始
    unsigned int normalOffset = 1;
    unsigned int texOffset = 1;

    // 遍历所有模型
    for (unsigned int modelIndex = 0; modelIndex < tinyobjModels.size(); ++modelIndex)
    {
        const auto& model = tinyobjModels[modelIndex];
        const auto& attrib = model.attrib;
        const auto& shapes = model.shapes;
        
        outFile << "# Model " << modelIndex << " - Position: (" 
                << model.position.x << ", " << model.position.y << ", " << model.position.z << ")\n\n";

        // 写入顶点，应用位置偏移
        for (size_t i = 0; i < attrib.vertices.size(); i += 3)
        {
            float x = attrib.vertices[i] + model.position.x;
            float y = attrib.vertices[i + 1] + model.position.y;
            float z = attrib.vertices[i + 2] + model.position.z;
            outFile << "v " << x << " " << y << " " << z << "\n";
        }

        // 写入法线，如果有的话
        if (!attrib.normals.empty())
        {
            for (size_t i = 0; i < attrib.normals.size(); i += 3)
            {
                outFile << "vn " << attrib.normals[i] << " " 
                        << attrib.normals[i + 1] << " " 
                        << attrib.normals[i + 2] << "\n";
            }
        }

        // 写入纹理坐标，如果有的话
        if (!attrib.texcoords.empty())
        {
            for (size_t i = 0; i < attrib.texcoords.size(); i += 2)
            {
                outFile << "vt " << attrib.texcoords[i] << " " 
                        << attrib.texcoords[i + 1] << "\n";
            }
        }

        // 写入面
        for (const auto& shape : shapes)
        {
            outFile << "\n# Shape: " << shape.name << "\n";
            
            // 遍历所有三角形，每个三角形有3个索引
            for (size_t i = 0; i < shape.mesh.indices.size(); i += 3)
            {
                outFile << "f ";
                
                // 写入三角形的3个顶点索引
                for (size_t j = 0; j < 3; ++j)
                {
                    const auto& index = shape.mesh.indices[i + j];
                    
                    // 顶点索引
                    outFile << (vertexOffset + index.vertex_index) << "/";
                    
                    // 纹理坐标索引
                    if (index.texcoord_index >= 0)
                    {
                        outFile << (texOffset + index.texcoord_index) << "/";
                    }
                    else
                    {
                        outFile << "/";
                    }
                    
                    // 法线索引
                    if (index.normal_index >= 0)
                    {
                        outFile << (normalOffset + index.normal_index);
                    }
                    
                    if (j < 2)
                    {
                        outFile << " ";
                    }
                }
                
                outFile << "\n";
            }
        }

        // 更新索引偏移
        vertexOffset += static_cast<unsigned int>(attrib.vertices.size() / 3);
        normalOffset += static_cast<unsigned int>(attrib.normals.size() / 3);
        texOffset += static_cast<unsigned int>(attrib.texcoords.size() / 2);
    }

    outFile.close();

    QMessageBox::information(this, "Success", QString("TinyObj Write Success!\nFile: %1\nTotal Models: %2").arg(filePath, QString::number(tinyobjModels.size())));
}

// ********** 模型转换辅助方法 **********

// 将Assimp模型转换为OSG节点
osg::ref_ptr<osg::Node> convertAssimpToOSG(const aiScene* scene, const aiVector3D& position, const QString& filePath)
{
    osg::ref_ptr<osg::Group> root = new osg::Group;
    
    // 获取模型所在目录
    QFileInfo fileInfo(filePath);
    QString modelDir = fileInfo.path();
    
    // 遍历所有网格
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        if (!mesh) continue;
        
        // 创建几何体
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        
        // 创建顶点数组
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        vertices->reserve(mesh->mNumVertices);
        
        // 创建法线数组
        osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
        normals->reserve(mesh->mNumVertices);
        
        // 创建纹理坐标数组
        osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array;
        texcoords->reserve(mesh->mNumVertices);
        
        // 填充顶点数据
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            const aiVector3D& v = mesh->mVertices[i];
            vertices->push_back(osg::Vec3(v.x + position.x, v.y + position.y, v.z + position.z));
            
            if (mesh->HasNormals()) {
                const aiVector3D& n = mesh->mNormals[i];
                normals->push_back(osg::Vec3(n.x, n.y, n.z));
            }
            
            if (mesh->HasTextureCoords(0)) {
                const aiVector3D& t = mesh->mTextureCoords[0][i];
                texcoords->push_back(osg::Vec2(t.x, t.y));
            }
        }
        
        // 设置顶点数组
        geometry->setVertexArray(vertices);
        
        // 设置法线数组
        if (mesh->HasNormals()) {
            geometry->setNormalArray(normals);
            geometry->setNormalBinding(osg::Geometry::BIND_PER_VERTEX);
        }
        
        // 设置纹理坐标数组
        if (mesh->HasTextureCoords(0)) {
            geometry->setTexCoordArray(0, texcoords);
        }
        
        // 收集所有面的索引
        osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);
        for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
            const aiFace& face = mesh->mFaces[i];
            if (face.mNumIndices >= 3) {
                for (unsigned int j = 0; j < face.mNumIndices; ++j) {
                    indices->push_back(face.mIndices[j]);
                }
            }
        }
        
        // 添加索引数据
        if (!indices->empty()) {
            geometry->addPrimitiveSet(indices);
        }
        
        // 创建几何体节点
        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(geometry);
        
        // 设置光照和材质属性
        osg::ref_ptr<osg::StateSet> stateSet = geode->getOrCreateStateSet();
        stateSet->setMode(GL_LIGHTING, osg::StateAttribute::ON);
        stateSet->setMode(GL_LIGHT0, osg::StateAttribute::ON);
        stateSet->setMode(GL_LIGHT1, osg::StateAttribute::ON);
        
        // 加载并应用材质纹理和漫反射颜色
        if (mesh->mMaterialIndex >= 0 && scene->mMaterials) {
            const aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            if (material) {
                // 1. 处理材质漫反射颜色
                aiColor4D diffuseColor;
                if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor)) {
                    // 创建材质
                    osg::ref_ptr<osg::Material> osgMaterial = new osg::Material;
                    osgMaterial->setDiffuse(osg::Material::FRONT_AND_BACK, 
                                           osg::Vec4(diffuseColor.r, diffuseColor.g, diffuseColor.b, diffuseColor.a));
                    stateSet->setAttributeAndModes(osgMaterial, osg::StateAttribute::ON);
                }
                
                // 2. 加载纹理
                aiString texturePath;
                if (AI_SUCCESS == material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath)) {
                    // 构建完整的纹理路径
                    QString texPath = texturePath.C_Str();
                    QFileInfo texFileInfo(texPath);
                    if (!texFileInfo.isAbsolute()) {
                        // 如果是相对路径，添加模型所在目录
                        texPath = modelDir + "/" + texPath;
                    }
                    
                    // 加载纹理
                    osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
                    osg::ref_ptr<osg::Image> image = osgDB::readImageFile(texPath.toStdString());
                    if (image) {
                        texture->setImage(image);
                        stateSet->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);
                        // 启用纹理混合，确保纹理与材质颜色正确结合
                        stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
                        stateSet->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
                    }
                }
            }
        }
        
        root->addChild(geode);
    }
    
    // 优化模型：合并Geometry（减少DrawCall）和移除冗余节点
    osgUtil::Optimizer optimizer;
    optimizer.optimize(root.get(), osgUtil::Optimizer::MERGE_GEOMETRY |
                                 osgUtil::Optimizer::REMOVE_REDUNDANT_NODES);
    
    return root;
}

// 将TinyObj模型转换为OSG节点
osg::ref_ptr<osg::Node> convertTinyObjToOSG(const tinyobj::attrib_t& attrib, const std::vector<tinyobj::shape_t>& shapes, const std::vector<tinyobj::material_t>& materials, const aiVector3D& position, const QString& filePath)
{
    osg::ref_ptr<osg::Group> root = new osg::Group;
    
    // 获取模型所在目录
    QFileInfo fileInfo(filePath);
    QString modelDir = fileInfo.path();
    
    // 遍历所有形状
    for (const auto& shape : shapes) {
        // 创建几何体
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
        
        // 创建顶点数组
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
        
        // 创建法线数组
        osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
        
        // 创建纹理坐标数组
        osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array;
        
        // 遍历所有三角形（每3个索引组成一个三角形）
        for (size_t i = 0; i < shape.mesh.indices.size(); i += 3) {
            // 确保有足够的索引组成一个三角形
            if (i + 2 >= shape.mesh.indices.size()) {
                break;
            }
            
            // 获取三角形的三个索引
            const auto& index0 = shape.mesh.indices[i];
            const auto& index1 = shape.mesh.indices[i+1];
            const auto& index2 = shape.mesh.indices[i+2];
            
            // 检查所有顶点索引是否有效
            bool valid0 = index0.vertex_index >= 0 && static_cast<size_t>(index0.vertex_index * 3 + 2) < attrib.vertices.size();
            bool valid1 = index1.vertex_index >= 0 && static_cast<size_t>(index1.vertex_index * 3 + 2) < attrib.vertices.size();
            bool valid2 = index2.vertex_index >= 0 && static_cast<size_t>(index2.vertex_index * 3 + 2) < attrib.vertices.size();
            
            if (valid0 && valid1 && valid2) {
                // 添加三个顶点
                vertices->push_back(osg::Vec3(
                    attrib.vertices[index0.vertex_index * 3] + position.x,
                    attrib.vertices[index0.vertex_index * 3 + 1] + position.y,
                    attrib.vertices[index0.vertex_index * 3 + 2] + position.z
                ));
                vertices->push_back(osg::Vec3(
                    attrib.vertices[index1.vertex_index * 3] + position.x,
                    attrib.vertices[index1.vertex_index * 3 + 1] + position.y,
                    attrib.vertices[index1.vertex_index * 3 + 2] + position.z
                ));
                vertices->push_back(osg::Vec3(
                    attrib.vertices[index2.vertex_index * 3] + position.x,
                    attrib.vertices[index2.vertex_index * 3 + 1] + position.y,
                    attrib.vertices[index2.vertex_index * 3 + 2] + position.z
                ));
                
                // 检查所有法线索引是否有效
                bool normalsValid = index0.normal_index >= 0 && static_cast<size_t>(index0.normal_index * 3 + 2) < attrib.normals.size() &&
                                   index1.normal_index >= 0 && static_cast<size_t>(index1.normal_index * 3 + 2) < attrib.normals.size() &&
                                   index2.normal_index >= 0 && static_cast<size_t>(index2.normal_index * 3 + 2) < attrib.normals.size();
                
                if (normalsValid) {
                    // 添加三个法线
                    normals->push_back(osg::Vec3(
                        attrib.normals[index0.normal_index * 3],
                        attrib.normals[index0.normal_index * 3 + 1],
                        attrib.normals[index0.normal_index * 3 + 2]
                    ));
                    normals->push_back(osg::Vec3(
                        attrib.normals[index1.normal_index * 3],
                        attrib.normals[index1.normal_index * 3 + 1],
                        attrib.normals[index1.normal_index * 3 + 2]
                    ));
                    normals->push_back(osg::Vec3(
                        attrib.normals[index2.normal_index * 3],
                        attrib.normals[index2.normal_index * 3 + 1],
                        attrib.normals[index2.normal_index * 3 + 2]
                    ));
                }
                
                // 检查所有纹理坐标索引是否有效
                bool texcoordsValid = index0.texcoord_index >= 0 && static_cast<size_t>(index0.texcoord_index * 2 + 1) < attrib.texcoords.size() &&
                                     index1.texcoord_index >= 0 && static_cast<size_t>(index1.texcoord_index * 2 + 1) < attrib.texcoords.size() &&
                                     index2.texcoord_index >= 0 && static_cast<size_t>(index2.texcoord_index * 2 + 1) < attrib.texcoords.size();
                
                if (texcoordsValid) {
                    // 添加三个纹理坐标
                    texcoords->push_back(osg::Vec2(
                        attrib.texcoords[index0.texcoord_index * 2],
                        attrib.texcoords[index0.texcoord_index * 2 + 1]
                    ));
                    texcoords->push_back(osg::Vec2(
                        attrib.texcoords[index1.texcoord_index * 2],
                        attrib.texcoords[index1.texcoord_index * 2 + 1]
                    ));
                    texcoords->push_back(osg::Vec2(
                        attrib.texcoords[index2.texcoord_index * 2],
                        attrib.texcoords[index2.texcoord_index * 2 + 1]
                    ));
                }
            }
        }
        
        // 设置顶点数组
        geometry->setVertexArray(vertices);
        
        // 设置法线数组
        if (!normals->empty()) {
            geometry->setNormalArray(normals);
            geometry->setNormalBinding(osg::Geometry::BIND_PER_VERTEX);
        }
        
        // 设置纹理坐标数组
        if (!texcoords->empty()) {
            geometry->setTexCoordArray(0, texcoords);
        }
        
        // 设置绘制方式 - 直接使用DrawArrays绘制所有三角形
        if (vertices->size() > 0) {
            geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::TRIANGLES, 0, vertices->size()));
        }
        
        // 创建几何体节点
        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(geometry);
        
        // 设置光照和材质属性
        osg::ref_ptr<osg::StateSet> stateSet = geode->getOrCreateStateSet();
        stateSet->setMode(GL_LIGHTING, osg::StateAttribute::ON);
        stateSet->setMode(GL_LIGHT0, osg::StateAttribute::ON);
        stateSet->setMode(GL_LIGHT1, osg::StateAttribute::ON);
        
        // 加载并应用材质纹理和漫反射颜色
        // 使用集合存储已处理的材质ID，避免重复加载纹理
        std::unordered_set<int> processedMaterialIds;
        
        // 遍历所有材质组，只处理唯一的材质ID
        for (size_t i = 0; i < shape.mesh.material_ids.size(); ++i) {
            int material_id = shape.mesh.material_ids[i];
            
            // 检查材质ID是否有效且未被处理过
            if (material_id >= 0 && static_cast<size_t>(material_id) < materials.size() && 
                processedMaterialIds.find(material_id) == processedMaterialIds.end()) {
                
                const tinyobj::material_t& material = materials[material_id];
                
                // 1. 处理材质漫反射颜色
                osg::ref_ptr<osg::Material> osgMaterial = new osg::Material;
                osgMaterial->setDiffuse(osg::Material::FRONT_AND_BACK, 
                                       osg::Vec4(material.diffuse[0], material.diffuse[1], material.diffuse[2], material.diffuse[3]));
                stateSet->setAttributeAndModes(osgMaterial, osg::StateAttribute::ON);
                
                // 2. 加载纹理
                if (!material.diffuse_texname.empty()) {
                    // 构建完整的纹理路径
                    QString texPath = material.diffuse_texname.c_str();
                    QFileInfo texFileInfo(texPath);
                    if (!texFileInfo.isAbsolute()) {
                        // 如果是相对路径，添加模型所在目录
                        texPath = modelDir + "/" + texPath;
                    }
                    
                    // 加载纹理
                    osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
                    osg::ref_ptr<osg::Image> image = osgDB::readImageFile(texPath.toStdString());
                    if (image) {
                        texture->setImage(image);
                        stateSet->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);
                        // 启用纹理混合，确保纹理与材质颜色正确结合
                        stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
                        stateSet->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
                    }
                }
                
                // 标记此材质ID已处理
                processedMaterialIds.insert(material_id);
            }
        }
        
        root->addChild(geode);
    }
    
    // 优化模型：合并Geometry（减少DrawCall）和移除冗余节点
    osgUtil::Optimizer optimizer;
    optimizer.optimize(root.get(), osgUtil::Optimizer::MERGE_GEOMETRY |
                                 osgUtil::Optimizer::REMOVE_REDUNDANT_NODES);
    
    return root;
}

// ********** OSG相关辅助方法 **********

// 初始化OSG渲染器
void MainWindow::setupOSGViewers()
{
    // 创建场景根节点
    assimpSceneRoot = new osg::Group;
    tinyobjSceneRoot = new osg::Group;
    osgSceneRoot = new osg::Group;
    
    // 为每个场景根节点添加默认几何体
    addDefaultGeometry(assimpSceneRoot);
    addDefaultGeometry(tinyobjSceneRoot);
    addDefaultGeometry(osgSceneRoot);
    
    // 创建三个osgQtViewer实例
    assimpViewerWidget = new osgQtViewer;
    tinyobjViewerWidget = new osgQtViewer;
    osgViewerWidget = new osgQtViewer;
    
    // 设置场景根节点
    assimpViewerWidget->getViewer()->setSceneData(assimpSceneRoot);
    tinyobjViewerWidget->getViewer()->setSceneData(tinyobjSceneRoot);
    osgViewerWidget->getViewer()->setSceneData(osgSceneRoot);
    
    // 为每个viewer添加相机操纵器和相机设置
    for (auto viewer : {assimpViewerWidget, tinyobjViewerWidget, osgViewerWidget}) {
        osgViewer::Viewer* osgViewer = viewer->getViewer();
        
        // 添加轨道操纵器，允许用户旋转、缩放和平移视图
        osg::ref_ptr<osgGA::CameraManipulator> manipulator = new osgGA::TrackballManipulator();
        osgViewer->setCameraManipulator(manipulator);
        
        // 设置默认相机位置，确保能看到场景
        osg::Vec3 eye(10, 10, 10);
        osg::Vec3 center(0, 0, 0);
        osg::Vec3 up(0, 0, 1);
        osgViewer->getCamera()->setViewMatrixAsLookAt(eye, center, up);
        
        // 设置相机投影参数
        osg::Camera* camera = osgViewer->getCamera();
        camera->setProjectionMatrixAsPerspective(30.0f, 1.0f, 1.0f, 10000.0f);
        camera->setClearColor(osg::Vec4(0.2f, 0.2f, 0.2f, 1.0f));
    }
    
    // 设置渲染窗口大小
    assimpViewerWidget->setMinimumSize(300, 200);
    tinyobjViewerWidget->setMinimumSize(300, 200);
    osgViewerWidget->setMinimumSize(300, 200);
    
    // 设置大小策略
    assimpViewerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    tinyobjViewerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    osgViewerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // 将渲染窗口添加到UI中
    QWidget* osgViewContainer = ui->osgViewContainer;
    if (osgViewContainer) {
        QVBoxLayout* layout = new QVBoxLayout(osgViewContainer);
        
        // 添加标题
        QLabel* titleLabel = new QLabel("3D Viewers");
        titleLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(titleLabel);
        
        // 添加水平布局，包含三个渲染窗口
        QHBoxLayout* viewersLayout = new QHBoxLayout;
        
        // 添加Assimp渲染窗口
        QVBoxLayout* assimpLayout = new QVBoxLayout;
        QLabel* assimpTitleLabel = new QLabel("Assimp Viewer");
        assimpTitleLabel->setAlignment(Qt::AlignCenter);
        assimpLayout->addWidget(assimpTitleLabel);
        assimpLayout->addWidget(assimpViewerWidget);
        viewersLayout->addLayout(assimpLayout);
        
        // 添加TinyObj渲染窗口
        QVBoxLayout* tinyobjLayout = new QVBoxLayout;
        QLabel* tinyobjTitleLabel = new QLabel("TinyObj Viewer");
        tinyobjTitleLabel->setAlignment(Qt::AlignCenter);
        tinyobjLayout->addWidget(tinyobjTitleLabel);
        tinyobjLayout->addWidget(tinyobjViewerWidget);
        viewersLayout->addLayout(tinyobjLayout);
        
        // 添加OSG渲染窗口
        QVBoxLayout* osgLayout = new QVBoxLayout;
        QLabel* osgTitleLabel = new QLabel("OSG Viewer");
        osgTitleLabel->setAlignment(Qt::AlignCenter);
        osgLayout->addWidget(osgTitleLabel);
        osgLayout->addWidget(osgViewerWidget);
        viewersLayout->addLayout(osgLayout);
        
        layout->addLayout(viewersLayout);
    }
}

// 添加默认几何体到场景
void MainWindow::addDefaultGeometry(osg::ref_ptr<osg::Group> &sceneRoot)
{
    // 创建一个简单的立方体作为默认几何体
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    
    // 创建立方体形状
    osg::ref_ptr<osg::Box> box = new osg::Box(osg::Vec3(0, 0, 0), 2.0f);
    
    // 创建形状可绘制对象
    osg::ref_ptr<osg::ShapeDrawable> shapeDrawable = new osg::ShapeDrawable(box);
    shapeDrawable->setColor(osg::Vec4(1.0f, 0.5f, 0.0f, 1.0f)); // 设置为橙色
    
    // 添加到几何体节点
    geode->addDrawable(shapeDrawable);
    
    // 将几何体添加到场景根节点
    sceneRoot->addChild(geode);
}

// ********** OSG导入流程 **********
// 功能：使用OSG库读取OBJ文件，并存储带有位置偏移的模型数据
// 每次读取会分配不同的位置偏移，确保模型在不同位置
void MainWindow::on_osgReadBtn_clicked()
{
    // 使用默认文件路径（data/BTR-70.obj）
    QString filePath = defaultObjPath;
    
    // 创建加载选项，强制加载纹理和读取完整材质属性
    osg::ref_ptr<osgDB::Options> readOptions = new osgDB::Options;
    readOptions->setOptionString("ReadFullMaterialProperties true");
    readOptions->setOptionString("LoadTextures true");                  //强制加载纹理
    readOptions->setPluginStringData("obj", "y-up=true"); // 确保OSG将OBJ的Z-up转换为Y-up
    
    // 加载模型
    osg::ref_ptr<osg::Node> node = osgDB::readNodeFile(filePath.toStdString(), readOptions);
    
    // 检查读取是否成功
    if (!node) {
        QMessageBox::critical(this, "Error", QString("OSG Read Error: Failed to read file %1").arg(filePath));
        return;
    }
    
    // 优化模型：合并Geometry（减少DrawCall）
    osgUtil::Optimizer optimizer;
    optimizer.optimize(node.get(), osgUtil::Optimizer::MERGE_GEOMETRY |
                                 osgUtil::Optimizer::REMOVE_REDUNDANT_NODES);
    
    // 为当前模型生成位置偏移（X轴方向，每次递增5.0f）
    aiVector3D currentPosition(osgPositionOffset, 0.0f, 0.0f);
    
    // 创建位置变换节点
    osg::ref_ptr<osg::PositionAttitudeTransform> pat = new osg::PositionAttitudeTransform;
    pat->setPosition(osg::Vec3(currentPosition.x, currentPosition.y, currentPosition.z));
    
    // 设置光照和材质属性
    osg::ref_ptr<osg::StateSet> stateSet = pat->getOrCreateStateSet();
    stateSet->setMode(GL_LIGHTING, osg::StateAttribute::ON);
    stateSet->setMode(GL_LIGHT0, osg::StateAttribute::ON);
    stateSet->setMode(GL_LIGHT1, osg::StateAttribute::ON);
    
    // 将模型添加到变换节点
    pat->addChild(node);
    
    // 存储模型数据到自定义结构体中
    OSGModelData modelData;
    modelData.node = pat;
    modelData.position = currentPosition;
    osgModels.push_back(modelData);
    
    // 将模型添加到OSG场景中
    osgSceneRoot->addChild(pat);
    
    // 更新加载计数和UI显示
    osgLoadCount++;
    updateCountLabels();
    
    // 递增位置偏移，确保下次加载的模型位置不同
    osgPositionOffset += 5.0f;
    
    // 显示成功信息，包含模型统计数据
    QMessageBox::information(this, "Success", QString("OSG Read Success!\nFile: %1\nLoad Count: %2\nPosition Offset: %3\nTotal Models: %4").arg(
        filePath, QString::number(osgLoadCount), 
        QString::number(currentPosition.x), QString::number(osgModels.size())));
}

// ********** OSG导出流程 **********
// 功能：将所有加载的OSG模型合并并导出为单个OBJ文件
// 每个模型会被放置在正确的偏移位置，并进行坐标转换（Y轴朝上转Z轴朝上）
void MainWindow::on_osgWriteBtn_clicked()
{
    // 检查是否有模型需要导出
    if (osgModels.empty()) {
        QMessageBox::warning(this, "Warning", "No OSG models loaded. Please load some models first.");
        return;
    }
    
    QString filePath = QFileDialog::getSaveFileName(this, "Save OBJ File", "", "OBJ Files (*.obj);;All Files (*.*)");
    if (filePath.isEmpty())
        return;
    
    // 创建一个临时根节点来合并所有模型
    osg::ref_ptr<osg::Group> exportRoot = new osg::Group;
    
    // 将所有模型添加到根节点
    for (const auto& model : osgModels) {
        exportRoot->addChild(model.node);
    }
    
    // 创建一个坐标转换矩阵，将Y轴朝上转换为Z轴朝上
    // 旋转90度绕X轴，将Y轴转到Z轴方向
    osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform;
    osg::Matrix rotationMatrix = osg::Matrix::rotate(osg::DegreesToRadians(90.0f), osg::Vec3(1, 0, 0));
    transform->setMatrix(rotationMatrix);
    transform->addChild(exportRoot);
    
    // 使用OSG导出OBJ文件，应用坐标转换
    if (!osgDB::writeNodeFile(*transform, filePath.toStdString())) {
        QMessageBox::critical(this, "Error", QString("Failed to write file %1").arg(filePath));
        return;
    }
    
    QMessageBox::information(this, "Success", QString("OSG Write Success!\nFile: %1\nTotal Models: %2").arg(filePath, QString::number(osgModels.size())));
}
