#include "ModelLibraryDialog.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <tuple>

namespace vsgraysim::gui {
namespace {
QString text(const nlohmann::json& data,const char* key) {
    return QString::fromStdString(data.at(key).get<std::string>());
}
constexpr std::array sensorFields{"temperature_k", "rho_tv", "eps_mwir", "eps_lwir", "rcs_scale"};
}

ModelLibraryDialog::ModelLibraryDialog(QWidget* parent) : QDialog(parent),
    endpoint_(qEnvironmentVariable("VSGRAYSIM_SERVICE_URL","http://127.0.0.1:8765"),QUrl::StrictMode) {
    setWindowTitle(tr("VSG 模型工作台"));
    resize(1130,690);
    setMinimumSize(760,480);
    setStyleSheet("QDialog{background:#101e2a;color:#e9f3f4}"
                  "QScrollArea#modelOptionsScroll,QWidget#modelOptions{background:#101e2a}"
                  "QLabel,QLineEdit,QComboBox,QDoubleSpinBox{color:#e5f2f4}"
                  "QComboBox,QLineEdit,QDoubleSpinBox,QPushButton{background:#213947;"
                  "border:1px solid #35515d;border-radius:6px;padding:6px;color:#edf7f7;min-height:20px}"
                  "QPushButton:disabled{color:#7c939d} QPushButton:hover{border-color:#a1e8d8}");
    auto* layout = new QHBoxLayout(this);
    image_ = new QLabel(tr("正在连接模型服务"));
    image_->setObjectName("libraryModelImage");
    image_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
    image_->setAlignment(Qt::AlignCenter);
    image_->setStyleSheet("background:#202329;border-radius:9px");
    image_->setMinimumSize(450,340);
    layout->addWidget(image_,1);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("modelOptionsScroll");
    scroll->setFixedWidth(320);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* options = new QWidget;
    options->setObjectName("modelOptions");
    scroll->setWidget(options);
    auto* form = new QFormLayout(options);
    models_ = new QComboBox(options);
    models_->setObjectName("libraryModels");
    representation_ = new QComboBox(options);
    representation_->setObjectName("libraryRepresentation");
    details_ = new QLabel(options);
    details_->setWordWrap(true);
    details_->setTextFormat(Qt::PlainText);
    label_ = new QLineEdit(options);
    label_->setObjectName("libraryName");
    label_->setMaxLength(64);
    form->addRow(tr("模型"), models_);
    form->addRow(details_);
    form->addRow(tr("显示"), representation_);
    form->addRow(tr("名称"), label_);
    const std::array<QString,3> names{tr("俯仰"),tr("航向"),tr("横滚")};
    for (size_t i=0; i<angles_.size(); ++i) {
        auto* angle = new QDoubleSpinBox(options);
        angle->setRange(-180,180);
        angle->setDecimals(2);
        angle->setSuffix("°");
        angles_[i] = angle;
        if (i == 1) angle->setObjectName("libraryYaw");
        form->addRow(names[i],angle);
    }
    const std::array captions{tr("参考温度 (K)"), tr("可见光反射率"), tr("中波红外发射率"),
        tr("长波红外发射率"), tr("RCS 功率倍率")};
    const std::array maxima{5000.,1.,1.,1.,1e8};
    for (size_t i = 0; i < sensorFields.size(); ++i) {
        auto* field = new QLineEdit(options);
        field->setObjectName(QString::fromLatin1(sensorFields[i]));
        auto* validator = new QDoubleValidator(i == 0 ? 1. : 0.,maxima[i],10,field);
        validator->setLocale(QLocale::c());
        field->setValidator(validator);
        field->setPlaceholderText(tr("沿用材质"));
        form->addRow(captions[i],field);
        materials_[i] = field;
    }
    saveButton_ = new QPushButton(tr("保存档案"),options);
    saveButton_->setObjectName("librarySave");
    saveButton_->setEnabled(false);
    importButton_ = new QPushButton(tr("导入 GLB"),options);
    importButton_->setEnabled(false);
    form->addRow(saveButton_);
    form->addRow(importButton_);
    auto* cameras = new QHBoxLayout;
    for (const auto& [caption,turn,scale] : std::array<std::tuple<QString,double,double>,4>{
        std::tuple{tr("↶"),-20.,1.}, {tr("↷"),20.,1.}, {tr("−"),0.,1.25}, {tr("＋"),0.,.8}}) {
        auto* button = new QPushButton(caption,options);
        cameras->addWidget(button);
        connect(button,&QPushButton::clicked,this,[this,turn,scale]{
            yaw_=std::remainder(yaw_+turn,360.);
            zoom_=std::clamp(zoom_*scale,.25,4.);
            adjustCamera();
        });
    }
    form->addRow(cameras);
    status_ = new QLabel(options);
    status_->setObjectName("libraryStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    form->addRow(status_);
    layout->addWidget(scroll);
    network_.setProxy(QNetworkProxy::NoProxy);
    connect(models_,&QComboBox::currentIndexChanged,this,[this](int index) {
        if (index>=0 && size_t(index)<entries_.size()) select(entries_.at(index));
    });
    connect(representation_,&QComboBox::currentIndexChanged,this,[this]{preview();});
    connect(saveButton_,&QPushButton::clicked,this,[this]{save();});
    connect(importButton_,&QPushButton::clicked,this,[this]{importModel();});
    auto* timer=new QTimer(this);
    connect(timer,&QTimer::timeout,this,[this]{readFrame();showFrame();});
    timer->start(160);

    if (endpoint_.host().compare("localhost",Qt::CaseInsensitive)==0) endpoint_.setHost("127.0.0.1");
    if (!endpoint_.isValid() || endpoint_.scheme()!="http" || endpoint_.host()!="127.0.0.1" ||
        endpoint_.hasQuery() || endpoint_.hasFragment() || !endpoint_.userInfo().isEmpty()) {
        report(tr("模型后端须为回环 HTTP 地址")); return;
    }
    jsonRequest("POST","/sessions",{{"client_kind","qt"}},[this](const Json& json){
        session_=text(json,"client_id").toUtf8();
        importButton_->setEnabled(true);
        loadModels();
    });
}

void ModelLibraryDialog::closeEvent(QCloseEvent* event) {
    if (!session_.isEmpty()) request("DELETE","/sessions/"+QString::fromUtf8(session_));
    QDialog::closeEvent(event);
}

QNetworkReply* ModelLibraryDialog::request(const QByteArray& method,const QString& path,
                                          const QByteArray& body,const QByteArray& type) {
    auto url=endpoint_;
    url.setPath("/api/v1"+path);
    QNetworkRequest options(url);
    options.setTransferTimeout(type=="model/gltf-binary"?30000:6000);
    options.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    if (!session_.isEmpty()) options.setRawHeader("X-Client-Id",session_);
    if (path.endsWith("/frame")) options.setRawHeader("X-After-Frame",frameToken_);
    if (!type.isEmpty()) options.setRawHeader("Content-Type",type);
    return network_.sendCustomRequest(options,method,body);
}

void ModelLibraryDialog::jsonRequest(const QByteArray& method,const QString& path,const Json& body,Reply action) {
    auto* pending=request(method,path,body.is_null()?QByteArray{}:QByteArray::fromStdString(body.dump()),
                          body.is_null()?QByteArray{}:QByteArray("application/json"));
    connect(pending,&QNetworkReply::finished,this,[this,pending,action=std::move(action)] {
        pending->deleteLater();
        const auto payload=pending->readAll();
        const auto json=Json::parse(payload.begin(),payload.end(),nullptr,false);
        if (pending->error()!=QNetworkReply::NoError) {
            report(json.is_object()?QString::fromStdString(json.value("message",pending->errorString().toStdString())):pending->errorString());
            busy_=false;
            saveButton_->setEnabled(selected_.is_object());
            return;
        }
        if (json.is_object()) action(json);
    });
}

void ModelLibraryDialog::loadModels(const QString& prefer) {
    jsonRequest("GET","/models",nullptr,[this,prefer](const Json& reply){
        entries_=reply.at("models");
        const QSignalBlocker guard(models_);
        models_->clear();
        int match=0;
        for (const auto& item : entries_) {
            if (text(item,"id")==prefer) match=models_->count();
            models_->addItem(text(item,"name")+" · "+text(item,"format"));
        }
        if (!entries_.empty()) {
            models_->setCurrentIndex(match);
            select(entries_.at(match));
        }
    });
}

void ModelLibraryDialog::select(const Json& item) {
    selected_=item;
    label_->setText(text(item,"name"));
    details_->setText(text(item,"description"));
    for (size_t i=0;i<angles_.size();++i) angles_[i]->setValue(item.at("rotation_deg").at(i).get<double>());
    const auto properties = item.value("sensor_properties",Json::object());
    for (size_t i = 0; i < sensorFields.size(); ++i) {
        const auto* field = sensorFields[i];
        materials_[i]->setText(properties.contains(field)
            ? QString::number(properties.at(field).get<double>(),'g',11) : QString());
    }
    const QSignalBlocker guard(representation_);
    const auto previous=representation_->currentData().toString();
    representation_->clear();
    representation_->addItem(tr("原模型"),"original");
    if (item.at("has_voxel")==true) representation_->addItem(tr("体素表面"),"voxel");
    representation_->setCurrentIndex(std::max(0,representation_->findData(previous)));
    saveButton_->setEnabled(!busy_);
    preview();
}

void ModelLibraryDialog::preview() {
    if (!selected_.is_object()) return;
    viewId_.clear();
    frameToken_.clear();
    const auto revision=++scope_;
    image_->setPixmap({});
    image_->setText(tr("正在渲染模型"));
    pixels_={};
    report(tr("正在准备独立视口"));
    jsonRequest("POST","/views",{{"model_id",selected_.at("id")},
        {"representation",representation_->currentData().toString().toStdString()},
        {"camera",{{"preset","area"},{"yaw_deg",yaw_},{"zoom",zoom_}}}},
        [this,revision](const Json& reply) {
            if (revision==scope_) viewId_=text(reply,"view_id");
        });
}

void ModelLibraryDialog::readFrame() {
    if (fetching_ || viewId_.isEmpty()) return;
    fetching_=true;
    const auto revision=scope_;
    auto* pending=request("GET","/views/"+viewId_+"/frame");
    connect(pending,&QNetworkReply::finished,this,[this,pending,revision] {
        fetching_=false;
        pending->deleteLater();
        if (revision!=scope_) return;
        if (pending->error()!=QNetworkReply::NoError) {
            viewId_.clear();
            report(tr("模型视口不可用，请重新选择模型"));
            return;
        }
        if (pending->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==204) return;
        frameToken_=pending->rawHeader("X-Frame-Token");
        if (pixels_.loadFromData(pending->readAll(),"JPEG")) {
            lastImageSize_={};
            report(tr("Vulkan 视口已更新"));
            showFrame();
        }
    });
}

void ModelLibraryDialog::showFrame() {
    if (!pixels_.isNull() && image_->size()!=lastImageSize_) {
        lastImageSize_=image_->size();
        image_->setPixmap(pixels_.scaled(lastImageSize_,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    }
}

void ModelLibraryDialog::adjustCamera() {
    if (viewId_.isEmpty()) return;
    jsonRequest("PUT","/views/"+viewId_+"/camera",
                {{"preset","area"},{"yaw_deg",yaw_},{"zoom",zoom_}},[](const Json&){});
}

void ModelLibraryDialog::save() {
    if (!selected_.is_object() || busy_) return;
    auto properties = Json::object();
    for (size_t i = 0; i < sensorFields.size(); ++i) {
        const auto input = materials_[i]->text().trimmed();
        if (input.isEmpty()) continue;
        if (!materials_[i]->hasAcceptableInput()) {
            report(tr("请检查传感材质的取值范围"));
            return;
        }
        properties[sensorFields[i]] = QLocale::c().toDouble(input);
    }
    busy_=true;
    saveButton_->setEnabled(false);
    const auto id=text(selected_,"id");
    auto rotation=Json::array();
    for (const auto* angle : angles_) rotation.push_back(angle->value());
    jsonRequest("PUT","/models/"+id,{{"expected_revision",selected_.at("revision")},
        {"name",label_->text().toStdString()},{"rotation_deg",rotation},{"sensor_properties",properties}},[this,id](const Json&){
        busy_=false;
        loadModels(id);
        report(tr("共享模型档案已保存"));
    });
}

void ModelLibraryDialog::importModel() {
    if (busy_) return;
    QFile file(QFileDialog::getOpenFileName(this,tr("导入自包含模型"),{},tr("GLB 模型 (*.glb)")));
    if (file.fileName().isEmpty()) return;
    if (!file.open(QIODevice::ReadOnly) || file.size()>24*1024*1024) {
        report(tr("请选择不超过 24 MiB 的 GLB")); return;
    }
    busy_=true;
    importButton_->setEnabled(false);
    report(tr("正在导入模型"));
    auto* pending=request("POST","/models",file.readAll(),"model/gltf-binary");
    connect(pending,&QNetworkReply::finished,this,[this,pending] {
        busy_=false;
        importButton_->setEnabled(true);
        pending->deleteLater();
        const auto payload=pending->readAll();
        const auto json=Json::parse(payload.begin(),payload.end(),nullptr,false);
        if (pending->error()==QNetworkReply::NoError && json.is_object()) loadModels(text(json,"id"));
        else report(json.is_object()?QString::fromStdString(json.value("message",std::string("导入失败"))):pending->errorString());
    });
}

void ModelLibraryDialog::report(const QString& message) { status_->setText(message); }
}
