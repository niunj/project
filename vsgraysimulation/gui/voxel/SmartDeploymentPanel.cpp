#include "SmartDeploymentPanel.h"
#include "viewport3d/VsgQtWidget.h"
#include <vsg/maths/transform.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <chrono>
#include <stdexcept>

namespace vsgraysim::gui {
namespace {
QString resource(const QString& file) {
    const auto relative="config/deployment/"+file;
    const QStringList roots={QDir::currentPath(),QCoreApplication::applicationDirPath()+"/..",
                            QCoreApplication::applicationDirPath()+"/../share/vsgraysim"};
    for(const auto& root:roots) {
        const auto path=QDir(root).absoluteFilePath(relative);
        if(QFile::exists(path)) return QDir::cleanPath(path);
    }
    return QDir::current().absoluteFilePath(relative);
}
}

SmartDeploymentPanel::SmartDeploymentPanel(Host host,VsgQtWidget* view,QWidget* parent)
    :QWidget(parent),host_(std::move(host)),view_(view),network_(new QNetworkAccessManager(this)),poll_(new QTimer(this)) {
    network_->setProxy(QNetworkProxy::NoProxy);
    auto* layout=new QVBoxLayout(this);
    layout->setContentsMargins(6,4,6,4);
    auto* composer=new QHBoxLayout;
    instruction_=new QPlainTextEdit(this);
    instruction_->setObjectName("deploymentInstruction");
    instruction_->setAccessibleName(tr("智能部署指令"));
    instruction_->setPlaceholderText(tr("添加 skywalker-x8 到 (20, 250, 50) 米，或输入本地模型指令"));
    instruction_->setFixedHeight(48);
    generate_=new QPushButton(tr("智能部署"),this);
    auto* showReview=new QPushButton(tr("方案与设置"),this);
    showReview->setCheckable(true);
    composer->addWidget(instruction_,1);
    composer->addWidget(generate_);
    composer->addWidget(showReview);
    layout->addLayout(composer);
    review_=new QWidget(this);
    review_->setVisible(false);
    layout->addWidget(review_);
    auto* reviewLayout=new QVBoxLayout(review_);
    reviewLayout->setContentsMargins(0,0,0,0);
    auto* form=new QFormLayout;
    endpoint_=new QLineEdit(qEnvironmentVariable("VSGRAYSIM_DEPLOYMENT_ENDPOINT","http://127.0.0.1:8080/v1/chat/completions"),this);
    model_=new QLineEdit(qEnvironmentVariable("VSGRAYSIM_DEPLOYMENT_MODEL","vsim-local"),this);
    form->addRow(tr("本地服务"),endpoint_);
    form->addRow(tr("模型"),model_);
    reviewLayout->addLayout(form);
    auto* requestActions=new QHBoxLayout;
    pick_=new QPushButton(tr("选地图点"),this);
    pick_->setCheckable(true);
    example_=new QPushButton(tr("载入示例"),this);
    requestActions->addWidget(pick_);
    requestActions->addWidget(example_);
    reviewLayout->addLayout(requestActions);
    pointLabel_=new QLabel(tr("地图点未选取"),this);
    pointLabel_->setWordWrap(true);
    reviewLayout->addWidget(pointLabel_);
    plan_=new QPlainTextEdit(this);
    plan_->setPlaceholderText(tr("预览或粘贴 scene-deployment-v1 JSON 方案"));
    plan_->setMaximumHeight(115);
    reviewLayout->addWidget(plan_,1);
    auto* editActions=new QHBoxLayout;
    preview_=new QPushButton(tr("预检"),this);
    apply_=new QPushButton(tr("应用方案"),this);
    undoButton_=new QPushButton(tr("撤销上次部署"),this);
    editActions->addWidget(preview_);
    editActions->addWidget(apply_);
    editActions->addWidget(undoButton_);
    reviewLayout->addLayout(editActions);
    status_=new QLabel(this);
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    status_->hide();
    connect(showReview,&QPushButton::toggled,review_,&QWidget::setVisible);
    connect(plan_,&QPlainTextEdit::textChanged,this,[this,showReview]{
        if (!plan_->toPlainText().isEmpty()) showReview->setChecked(true);
        status_->show();
    });
    if(view_) view_->installEventFilter(this);
    connect(plan_,&QPlainTextEdit::textChanged,this,[this]{invalidate();});
    connect(pick_,&QPushButton::toggled,this,[this](bool active){
        if(active) status_->setText(tr("请在三维视口点击体素地形。"));
    });
    connect(example_,&QPushButton::clicked,this,[this]{
        QFile file(resource("taipei-survey.json"));
        if(file.open(QIODevice::ReadOnly)) {
            plan_->setPlainText(QString::fromUtf8(file.readAll()));
            status_->setText(tr("台北无人机示例已载入。可编辑 JSON 后预检。"));
        } else status_->setText(file.errorString());
    });
    connect(generate_,&QPushButton::clicked,this,[this]{requestPlan();});
    connect(preview_,&QPushButton::clicked,this,[this]{preview();});
    connect(apply_,&QPushButton::clicked,this,[this]{apply();});
    connect(undoButton_,&QPushButton::clicked,this,[this]{undo();});
    connect(poll_,&QTimer::timeout,this,[this]{finishApply();});
    try {
        adapter_.emplace(resource("catalog.json").toStdString());
    } catch(const std::exception& e) { reportError(e); }
    updateButtons();
}

earth::VoxelScene& SmartDeploymentPanel::scene() const {
    auto* runtime=host_.runtime();
    if(!runtime||host_.path().isEmpty()||!runtime->paused())
        throw std::logic_error("请打开地球体素专题，并等待场景加载及暂停");
    return *runtime;
}

std::optional<vsg::dvec3> SmartDeploymentPanel::selectedPoint() const {
    return pointScene_==host_.runtime()&&pointTopic_==host_.path()?point_:std::nullopt;
}

bool SmartDeploymentPanel::eventFilter(QObject* object,QEvent* event) {
    if(object!=view_||event->type()!=QEvent::MouseButtonPress||!isVisible()||!pick_->isChecked()) return false;
    const auto* mouse=static_cast<QMouseEvent*>(event);
    if(mouse->button()!=Qt::LeftButton) return false;
    pick_->setChecked(false);
    try {
        const auto& runtime=scene();
        const auto snapshot=runtime.snapshot();
        vsg::dvec3 origin,direction;
        if(!view_->mouseRay(mouse->position().toPoint(),origin,direction))
            throw std::runtime_error("当前视口无法拾取地图点");
        const auto inverse=vsg::inverse(simcore::geo::makeEnuToWorldMatrix(snapshot->anchor));
        const auto ray=inverse*vsg::dvec4(direction,0.);
        const auto hit=earth::raycastVoxelScene(*snapshot,inverse*origin,{ray.x,ray.y,ray.z},1e7,true);
        if(!hit) throw std::runtime_error("点选位置未命中专题中的体素地形");
        point_=hit->position;
        pointScene_=&runtime;
        pointTopic_=host_.path();
        pointLabel_->setText(tr("地图点 E %1 m · U %2 m · S %3 m")
            .arg(point_->x,0,'f',1).arg(point_->y,0,'f',1).arg(point_->z,0,'f',1));
        invalidate();
        status_->setText(tr("已选取地图点。可以用相对此点的坐标生成方案。"));
    } catch(const std::exception& e) { reportError(e); }
    return true;
}

void SmartDeploymentPanel::invalidate() {
    staged_.reset();
    updateButtons();
}

void SmartDeploymentPanel::requestPlan() {
    try {
        auto& runtime=scene();
        const auto instruction=instruction_->toPlainText().trimmed();
        if (instruction.isEmpty()||instruction.size()>4096) throw std::invalid_argument("请输入 1 至 4096 字的部署指令");
        if (const auto direct=instruction.startsWith('{') ? std::optional{scene_deployment::parse(instruction.toStdString())}
                : scene_deployment::simplePlan(instruction.toStdString(),host_.selected().toStdString())) {
            plan_->setPlainText(QString::fromStdString(direct->dump(2)));
            preview();
            return;
        }
        auto endpoint=QUrl(endpoint_->text().trimmed(),QUrl::StrictMode);
        if(endpoint.host().compare("localhost",Qt::CaseInsensitive)==0) endpoint.setHost("127.0.0.1");
        if(!endpoint.isValid()||endpoint.scheme()!="http"||!QHostAddress(endpoint.host()).isLoopback()||
           endpoint.path()!="/v1/chat/completions"||!endpoint.userInfo().isEmpty()||endpoint.hasQuery()||endpoint.hasFragment())
            throw std::invalid_argument("本地服务必须使用回环地址的 /v1/chat/completions");
        auto snapshot=runtime.snapshot();
        const auto payload=scene_deployment::modelRequest(model_->text().trimmed().toStdString(),
            instruction.toStdString(),
            adapter_->context(*snapshot,host_.selected().toStdString(),selectedPoint()));
        QNetworkRequest request(endpoint);
        request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(120000);
        response_.clear();
        oversized_=false;
        requested_=snapshot;
        requestTopic_=host_.path();
        pick_->setChecked(false);
        auto* reply=network_->post(request,QByteArray::fromStdString(payload.dump()));
        reply_=reply;
        connect(reply,&QIODevice::readyRead,this,[this,reply]{
            const auto remaining=scene_deployment::maxResponseBytes*2-response_.size();
            const auto bytes=reply->read(remaining+1);
            if(bytes.size()>remaining) {oversized_=true;reply->abort();}
            else response_.append(bytes);
        });
        connect(reply,&QNetworkReply::finished,this,[this,reply]{finishPlan(reply);});
        QTimer::singleShot(120000,reply,[reply]{if(reply->isRunning()) reply->abort();});
        status_->setText(tr("正在本地生成方案…"));
        status_->show();
        updateButtons();
    } catch(const std::exception& e) { reportError(e); }
}

void SmartDeploymentPanel::finishPlan(QNetworkReply* reply) {
    reply_.clear();
    reply->deleteLater();
    try {
        if(oversized_) throw std::runtime_error("本地模型响应过大");
        if(reply->error()!=QNetworkReply::NoError||reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200)
            throw std::runtime_error(("本地推理失败。请检查服务地址和模型名称。 "+reply->errorString()).toStdString());
        if(scene().snapshot()!=requested_.lock()||host_.path()!=requestTopic_)
            throw std::runtime_error("生成期间场景已更新，请重新生成");
        const auto remaining=scene_deployment::maxResponseBytes*2-response_.size();
        const auto tail=reply->read(remaining+1);
        if(tail.size()>remaining) throw std::runtime_error("本地模型响应过大");
        response_.append(tail);
        const auto generated=scene_deployment::modelPlan(
            std::string_view(response_.constData(),size_t(response_.size())));
        plan_->setPlainText(QString::fromStdString(generated.dump(2)));
        status_->setText(tr("已生成 %1 项操作。请检查坐标、对象和参数后预检。").arg(generated.at("actions").size()));
    } catch(const std::exception& e) { reportError(e); }
    response_.clear();
    updateButtons();
}

void SmartDeploymentPanel::preview() {
    try {
        auto& runtime=scene();
        const auto plan=scene_deployment::parse(plan_->toPlainText().toStdString());
        staged_=adapter_->stage(plan,runtime.snapshot(),host_.topic(),host_.path().toStdString(),selectedPoint());
        stagedTopic_=host_.path();
        const auto count=staged_->imports.at("objects").size();
        const auto equipment=[](const nlohmann::json& value){return value.is_null()?size_t(0):value.at("equipment").size();};
        status_->setText(tr("预检通过。新增模型 %1，场景编辑 %2，新增装备 %3。")
            .arg(count).arg(staged_->commands.size()).arg(equipment(staged_->equipmentAfter)-equipment(staged_->equipmentBefore)));
    } catch(const std::exception& e) { staged_.reset();reportError(e); }
    updateButtons();
}

void SmartDeploymentPanel::apply() {
    try {
        if(!staged_||scene().snapshot()!=staged_->before||host_.path()!=stagedTopic_)
            throw std::logic_error("场景已更新，请重新预检");
        if(staged_->imports.at("objects").empty()) commit();
        else {
            preparing_.emplace(earth::VoxelScene::prepareImportAsync(staged_->imports));
            status_->setText(tr("在后台准备体素模型…"));
            poll_->start(60);
        }
    } catch(const std::exception& e) { reportError(e); }
    updateButtons();
}

void SmartDeploymentPanel::finishApply() {
    if(!preparing_||preparing_->snapshot.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
    poll_->stop();
    try {
        auto prepared=preparing_->snapshot.get();
        commit(std::move(prepared));
    } catch(const std::exception& e) { reportError(e); }
    preparing_.reset();
    updateButtons();
}

void SmartDeploymentPanel::commit(earth::VoxelSceneSnapshotPtr prepared) {
    if(!staged_||host_.path()!=stagedTopic_) throw std::logic_error("专题已切换，请重新预检");
    undo_=earth::deployment::Adapter::commit(scene(),host_.topic(),*staged_,std::move(prepared));
    undoTopic_=host_.path();
    staged_.reset();
    host_.changed();
    status_->setText(tr("已应用。可以保存或通过原有发布流程生成 .vsimscene。"));
}

void SmartDeploymentPanel::undo() {
    try {
        if(!undo_||host_.path()!=undoTopic_) throw std::logic_error("当前专题没有可撤销的部署");
        earth::deployment::Adapter::undo(scene(),host_.topic(),*undo_);
        undo_.reset();
        invalidate();
        host_.changed();
        status_->setText(tr("已撤销上次部署。"));
    } catch(const std::exception& e) { reportError(e); }
    updateButtons();
}

void SmartDeploymentPanel::reportError(const std::exception& e) {
    status_->setText(QString::fromUtf8(e.what()));
    status_->show();
}

void SmartDeploymentPanel::updateButtons() {
    const bool idle=!reply_&&!preparing_;
    generate_->setEnabled(idle&&adapter_.has_value());
    pick_->setEnabled(idle&&view_);
    preview_->setEnabled(idle&&adapter_&&!plan_->toPlainText().trimmed().isEmpty());
    apply_->setEnabled(idle&&staged_.has_value());
    undoButton_->setEnabled(idle&&undo_.has_value());
    plan_->setReadOnly(!idle);
    example_->setEnabled(idle);
}
}
