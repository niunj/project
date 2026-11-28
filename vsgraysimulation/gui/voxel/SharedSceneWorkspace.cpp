#include "SharedSceneWorkspace.h"
#include "ModelLibraryDialog.h"

#include <vsg/maths/common.h>
#include <simcore/GeoNavigation.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>

namespace vsgraysim::gui {
namespace {
using Json = nlohmann::json;

QString text(const Json& value, const char* key) {
    return QString::fromStdString(value.value(key,std::string()));
}

class EarthScreen final : public QWidget {
public:
    explicit EarthScreen(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(430,310);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::OpenHandCursor);
        setAccessibleName(tr("三维体素地球"));
    }
    void image(QPixmap next) { frame_ = std::move(next); update(); }
    std::function<void()> changed;
    simcore::geo::EarthNavigation navigation;
    std::optional<Json> target;

    Json camera() const {
        Json camera = {{"preset","workspace"},{"yaw_deg",navigation.yaw},{"pitch_deg",navigation.pitch},
            {"zoom",navigation.zoom},{"pan_x",navigation.panX},{"pan_y",navigation.panY}};
        if (target) camera["focus_m"] = *target;
        return camera;
    }
    void focus(double zoom, std::optional<Json> next = {}) {
        navigation = {};
        target = std::move(next);
        navigation.zoom = zoom;
        changed();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(),QColor("#091623"));
        if (frame_.isNull()) return;
        auto fitted = frame_.size();
        fitted.scale(size(),Qt::KeepAspectRatio);
        const QRect box(QPoint((width()-fitted.width())/2,(height()-fitted.height())/2),fitted);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(box,frame_);
    }
    void mousePressEvent(QMouseEvent* event) override {
        button_ = event->button();
        last_ = event->position();
        pan_ = event->modifiers().testFlag(Qt::ShiftModifier) ||
           (button_ == Qt::LeftButton && event->modifiers().testFlag(Qt::ControlModifier));
        setCursor(Qt::ClosedHandCursor);
        setFocus();
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        button_ = Qt::NoButton;
        setCursor(Qt::OpenHandCursor);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (button_ == Qt::NoButton) return;
        const auto step = event->position() - last_;
        last_ = event->position();
        if (button_ == Qt::RightButton)
            dolly(std::exp(step.y() * simcore::geo::EarthNavigation::dollyPerPixel));
        else {
            if (pan_) navigation.pan(step.x(),step.y());
            else navigation.orbit(step.x(),step.y());
            changed();
        }
    }
    void mouseDoubleClickEvent(QMouseEvent*) override { focus(1.); }
    void wheelEvent(QWheelEvent* event) override {
        const auto steps = event->angleDelta().y() / 120.;
        dolly(std::pow(simcore::geo::EarthNavigation::zoomPerNotch,steps));
        event->accept();
    }
    void keyPressEvent(QKeyEvent* event) override {
        switch (event->key()) {
        case Qt::Key_Home: focus(920.); return;
        case Qt::Key_End: focus(1.); return;
        case Qt::Key_Left: navigation.orbit(-12,0); break;
        case Qt::Key_Right: navigation.orbit(12,0); break;
        case Qt::Key_Up: navigation.orbit(0,12); break;
        case Qt::Key_Down: navigation.orbit(0,-12); break;
        case Qt::Key_Plus: case Qt::Key_Equal: dolly(simcore::geo::EarthNavigation::zoomPerNotch); return;
        case Qt::Key_Minus: dolly(1. / simcore::geo::EarthNavigation::zoomPerNotch); return;
        default: QWidget::keyPressEvent(event); return;
        }
        changed();
    }
private:
    void dolly(double factor) {
        navigation.zoom = std::clamp(navigation.zoom * factor,.004,1600.);
        changed();
    }
    QPixmap frame_;
    QPointF last_;
    Qt::MouseButton button_ = Qt::NoButton;
    bool pan_ = false;
};

class SharedScene final : public QMainWindow {
public:
    explicit SharedScene(QString address) : endpoint_(address,QUrl::StrictMode) {
        setWindowTitle(tr("VSG 场景设计"));
        setObjectName("sharedSceneWorkspace");
        resize(1220,790);
        setStyleSheet(
            "QMainWindow,QWidget#sceneCatalog,QWidget#sceneEditor{background:#101e2b;color:#edf3fa}"
            "QLabel{color:#dce9f4} QPushButton,QPlainTextEdit,QTreeWidget{background:#172b3b;color:#e8f2fa;"
            "border:1px solid #3b5668;border-radius:6px;padding:7px}"
            "QPushButton:hover{border-color:#85bddf} QPushButton:disabled{color:#859ba9}"
            "QHeaderView::section{background:#243d52;color:#edf3fa;padding:5px}");
        network_.setProxy(QNetworkProxy::NoProxy);
        pages_ = new QStackedWidget(this);
        setCentralWidget(pages_);
        buildCatalog();
        buildEditor();
        pages_->setCurrentWidget(catalogPage_);
        if (endpoint_.host().compare("localhost",Qt::CaseInsensitive)==0) endpoint_.setHost("127.0.0.1");
        const bool addressValid = endpoint_.isValid() && endpoint_.scheme()=="http" &&
            QHostAddress(endpoint_.host()).isLoopback() && endpoint_.userInfo().isEmpty() &&
            !endpoint_.hasQuery() && !endpoint_.hasFragment() && (endpoint_.path().isEmpty() || endpoint_.path()=="/");
        if (addressValid) {
            connect(&timer_,&QTimer::timeout,this,[this]{poll();});
            timer_.start(160);
        } else catalogStatus_->setText(tr("请输入本机 HTTP 仿真服务地址。"));
    }

private:
    using Reply = std::function<void(const Json&, int)>;

    QNetworkReply* request(const QByteArray& method, const QString& path, const QByteArray& data = {},
                           const QByteArray& after = {}) {
        QNetworkRequest request(endpoint_.resolved(QUrl(path)));
        request.setTransferTimeout(5000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        if (!session_.isEmpty()) request.setRawHeader("X-Client-Id",session_);
        if (!after.isEmpty()) request.setRawHeader("X-After-Frame",after);
        if (!data.isEmpty()) request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        return network_.sendCustomRequest(request,method,data);
    }
    void api(const QByteArray& method, const QString& path, const Json& body, Reply done) {
        auto* reply = request(method,"/api/v1"+path,body.is_null() ? QByteArray() : QByteArray::fromStdString(body.dump()));
        connect(reply,&QNetworkReply::finished,this,[this,reply,done=std::move(done)] {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto bytes = reply->readAll();
            auto value = Json::parse(bytes.begin(),bytes.end(),nullptr,false);
            if (!value.is_object()) value = {{"message",reply->errorString().toStdString()}};
            if (status == 401) { session_.clear(); viewId_.clear(); }
            reply->deleteLater();
            done(value,status);
        });
    }
    QString path(const QString& action = "layout") const {
        const auto id = QUrl::toPercentEncoding(text(project_,"project_id"));
        const auto revision = QUrl::toPercentEncoding(text(project_,"published_revision_id"));
        return "/projects/" + QString::fromLatin1(id) + "/" + action + "?revision=" + QString::fromLatin1(revision);
    }
    void buildCatalog() {
        catalogPage_ = new QWidget(pages_);
        catalogPage_->setObjectName("sceneCatalog");
        auto* layout = new QVBoxLayout(catalogPage_);
        layout->setContentsMargins(32,28,32,25);
        auto* heading = new QLabel(tr("场景工程"),catalogPage_);
        heading->setStyleSheet("font-size:30px;font-weight:650");
        layout->addWidget(heading);
        auto* intro = new QLabel(tr("选择工程，进入共享的三维地球。"),catalogPage_);
        layout->addWidget(intro);
        catalogStatus_ = new QLabel(tr("正在读取场景目录"),catalogPage_);
        layout->addWidget(catalogStatus_);
        auto* scroll = new QScrollArea(catalogPage_);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* cards = new QWidget(scroll);
        cards->setStyleSheet("background:#101e2b");
        cards_ = new QHBoxLayout(cards);
        cards_->setContentsMargins(4,12,4,12);
        scroll->setWidget(cards);
        layout->addWidget(scroll,1);
        pages_->addWidget(catalogPage_);
    }
    void buildEditor() {
        editorPage_ = new QWidget(pages_);
        editorPage_->setObjectName("sceneEditor");
        auto* layout = new QVBoxLayout(editorPage_);
        auto* top = new QHBoxLayout;
        auto* back = new QPushButton(tr("← 场景列表"),editorPage_);
        auto* models = new QPushButton(tr("模型工作台"),editorPage_);
        auto* simulate = new QPushButton(tr("启动场景仿真"),editorPage_);
        title_ = new QLabel(editorPage_);
        revision_ = new QLabel(editorPage_);
        top->addWidget(back); top->addWidget(title_,1); top->addWidget(revision_); top->addWidget(models); top->addWidget(simulate);
        layout->addLayout(top);
        connect(back,&QPushButton::clicked,this,[this]{
            ++scope_; project_ = nullptr; layout_ = nullptr; viewId_.clear();
            earth_->image({}); pages_->setCurrentWidget(catalogPage_);
        });
        connect(models,&QPushButton::clicked,this,[this]{
            auto* dialog = new ModelLibraryDialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        });
        connect(simulate,&QPushButton::clicked,this,[this,simulate] {
            if (!layout_.is_object()) return;
            simulate->setEnabled(false);
            api("POST","/runs",{{"project_id",project_.at("project_id")},
                {"published_revision_id",project_.at("published_revision_id")},{"layout_revision",layout_.at("revision")}},
                [this,simulate](const Json& value,int status) {
                    simulate->setEnabled(true);
                    if (status != 202) { feedback_->setText(text(value,"message")); return; }
                    const auto run = text(value,"run_id");
                    feedback_->setText(tr("已启动仿真会话 %1").arg(run));
                    const auto app = QApplication::applicationDirPath() + "/vsgraysim_sim_control";
                    QProcess::startDetached(app,{"--service-run",run,endpoint_.toString()});
                });
        });
        auto* split = new QSplitter(Qt::Horizontal,editorPage_);
        earth_ = new EarthScreen(split);
        earth_->setObjectName("sharedEarthScreen");
        earth_->changed = [this]{ ++cameraVersion_; };
        split->addWidget(earth_);
        auto* sceneTree = new QWidget(split);
        auto* treeLayout = new QVBoxLayout(sceneTree);
        tree_ = new QTreeWidget(sceneTree);
        tree_->setObjectName("sharedSceneTree");
        tree_->setHeaderLabel(tr("场景树"));
        tree_->setMinimumWidth(240);
        treeLayout->addWidget(tree_);
        selectedInfo_ = new QLabel(sceneTree);
        selectedInfo_->setWordWrap(true);
        treeLayout->addWidget(selectedInfo_);
        auto* locate = new QPushButton(tr("定位对象"),sceneTree);
        treeLayout->addWidget(locate);
        connect(tree_,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem* item) {
            selected_ = item ? item->data(0,Qt::UserRole).toString() : QString();
            showSelection();
        });
        connect(locate,&QPushButton::clicked,this,[this] {
            if (auto position = selectedPosition()) earth_->focus(.009,*position);
        });
        connect(tree_,&QTreeWidget::itemDoubleClicked,this,[this] {
            if (auto position = selectedPosition()) earth_->focus(.009,*position);
        });
        split->addWidget(sceneTree);
        split->setStretchFactor(0,1);
        layout->addWidget(split,1);

        auto* navigation = new QHBoxLayout;
        auto* globe = new QPushButton(tr("全地球"),editorPage_);
        auto* site = new QPushButton(tr("定位工程"),editorPage_);
        connect(globe,&QPushButton::clicked,this,[this]{ earth_->focus(920.); });
        connect(site,&QPushButton::clicked,this,[this]{ earth_->focus(1.); });
        frameStatus_ = new QLabel(editorPage_);
        frameStatus_->setObjectName("sharedFrameStatus");
        navigation->addWidget(globe); navigation->addWidget(site); navigation->addWidget(frameStatus_,1);
        layout->addLayout(navigation);
        auto* composer = new QHBoxLayout;
        input_ = new QPlainTextEdit(editorPage_);
        input_->setObjectName("sharedDeploymentInstruction");
        input_->setPlaceholderText(tr("添加 survey-vessel 编号 survey-02 到 (25, 0, 30) 米"));
        input_->setAccessibleName(tr("智能部署指令"));
        input_->setFixedHeight(48);
        preflight_ = new QPushButton(tr("预检"),editorPage_);
        preflight_->setObjectName("sharedDeploymentPreview");
        apply_ = new QPushButton(tr("提交到共享工程"),editorPage_);
        apply_->setObjectName("sharedDeploymentApply");
        composer->addWidget(input_,1); composer->addWidget(preflight_); composer->addWidget(apply_);
        layout->addLayout(composer);
        feedback_ = new QLabel(editorPage_);
        feedback_->setObjectName("sharedDeploymentFeedback");
        feedback_->setWordWrap(true);
        feedback_->setTextFormat(Qt::PlainText);
        layout->addWidget(feedback_);
        connect(input_,&QPlainTextEdit::textChanged,this,[this]{ plan_ = nullptr; buttons(); });
        connect(preflight_,&QPushButton::clicked,this,[this]{ preflight(); });
        connect(apply_,&QPushButton::clicked,this,[this]{ submit(); });
        pages_->addWidget(editorPage_);
        buttons();
    }
    void list(const Json& projects) {
        while (auto* item = cards_->takeAt(0)) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        for (const auto& project : projects) {
            auto* panel = new QWidget(catalogPage_);
            auto* layout = new QVBoxLayout(panel);
            auto* photo = new QLabel(panel);
            photo->setAlignment(Qt::AlignCenter);
            photo->setMinimumHeight(140);
            photo->setStyleSheet("background:#1c3344;border-radius:8px");
            layout->addWidget(photo,1);
            auto* name = new QLabel(text(project,"name"),panel);
            name->setStyleSheet("font-size:19px;font-weight:650");
            layout->addWidget(name);
            auto* desc = new QLabel(text(project,"description"),panel);
            desc->setWordWrap(true);
            layout->addWidget(desc);
            auto* open = new QPushButton(tr("打开工程"),panel);
            open->setObjectName(QStringLiteral("open-")+text(project,"project_id"));
            connect(open,&QPushButton::clicked,this,[this,project]{ openProject(project); });
            layout->addWidget(open);
            cards_->addWidget(panel,1);
            const auto url = text(project,"thumbnail");
            if (url.startsWith("/assets/projects/")) {
                auto* reply = request("GET",url);
                connect(reply,&QNetworkReply::finished,photo,[reply,photo] {
                    QPixmap pixels;
                    if (pixels.loadFromData(reply->readAll()))
                        photo->setPixmap(pixels.scaled(490,275,Qt::KeepAspectRatio,Qt::SmoothTransformation));
                });
                connect(reply,&QNetworkReply::finished,reply,&QObject::deleteLater);
            }
        }
        catalogStatus_->setText(projects.empty() ? tr("目录中暂无场景工程") : QString());
    }
    void openProject(const Json& project) {
        ++scope_;
        project_ = project;
        layout_ = plan_ = nullptr;
        selected_.clear(); viewId_.clear(); frameToken_.clear();
        frameRevision_ = -1;
        tree_->clear(); input_->clear(); earth_->image({}); earth_->focus(1.);
        title_->setText(text(project_,"name"));
        frameStatus_->setText(tr("正在准备三维地球"));
        feedback_->clear(); revision_->clear();
        showSelection();
        pages_->setCurrentWidget(editorPage_);
        nextLayout_ = 0;
        buttons();
    }
    void applyLayout(const Json& incoming) {
        if (layout_.is_object() && incoming.at("revision") < layout_.at("revision")) return;
        if (layout_.is_object() && incoming.at("revision") == layout_.at("revision")) return;
        layout_ = incoming;
        const auto selected = selected_;
        const QSignalBlocker guard(tree_);
        tree_->clear();
        auto* earth = new QTreeWidgetItem(tree_,{tr("体素地球")});
        auto* terrain = new QTreeWidgetItem(earth,{tr("体素地形")});
        auto* environment = new QTreeWidgetItem(earth,{tr("环境")});
        new QTreeWidgetItem(environment,{tr("天空与云")});
        const bool authoredOcean = layout_.value("environment",Json::object()).value("ocean",false);
        new QTreeWidgetItem(environment,{authoredOcean ? tr("海洋") : tr("地球海洋")});
        auto* targets = new QTreeWidgetItem(earth,{tr("目标")});
        for (const auto& object : layout_.at("objects")) {
            const auto id = text(object,"id");
            const auto label = text(object,"label");
            auto* row = new QTreeWidgetItem(object.at("role")=="terrain" ? terrain : targets,{label.isEmpty() ? id : label});
            row->setData(0,Qt::UserRole,id);
            if (selected == id) tree_->setCurrentItem(row);
        }
        tree_->expandAll();
        const auto revision = layout_.at("revision").get<qint64>();
        const auto author = layout_.value("last_edit",Json(nullptr));
        revision_->setText(tr("共享 r%1").arg(revision) +
            (author.is_object() ? tr(" · 最近提交 %1").arg(text(author,"author")) : QString()));
        showSelection();
        buttons();
    }
    std::optional<Json> selectedPosition() const {
        if (selected_.isEmpty() || !layout_.is_object()) return {};
        for (const auto& object : layout_.at("objects")) {
            if (text(object,"id") != selected_ || object.at("role") != "target") continue;
            return layout_.at("positions").value(selected_.toStdString(),object.at("position_m"));
        }
        return {};
    }
    void showSelection() {
        const auto position = selectedPosition();
        if (!position) { selectedInfo_->clear(); return; }
        selectedInfo_->setText(tr("东 %1 m · 上 %2 m · 南 %3 m")
            .arg((*position)[0].get<double>(),0,'f',1).arg((*position)[1].get<double>(),0,'f',1)
            .arg((*position)[2].get<double>(),0,'f',1));
    }
    void buttons() {
        const bool editable = layout_.is_object() && layout_.value("editable",false) && !writing_;
        preflight_->setEnabled(editable && !input_->toPlainText().trimmed().isEmpty());
        apply_->setEnabled(editable && plan_.is_object());
    }
    void preflight() {
        if (!layout_.is_object() || writing_) return;
        const auto command = input_->toPlainText().trimmed();
        const auto scope = scope_;
        writing_ = true;
        plan_ = nullptr;
        buttons();
        api("POST",path("layout-plan"),{{"expected_revision",layout_.at("revision")},
            {"instruction",command.toStdString()},{"selected",selected_.toStdString()}},[this,scope,command](const Json& value,int status) {
            writing_ = false;
            if (scope != scope_) { buttons(); return; }
            if (status == 200 && command == input_->toPlainText().trimmed()) {
                plan_ = value;
                QStringList lines;
                for (const auto& action : value.at("plan").at("actions")) {
                    const auto id = text(action,action.at("op")=="add_target" ? "id" : "object");
                    const auto p = action.at("position").at("local_m");
                    lines << tr("%1 → %2 / %3 / %4 m").arg(id)
                        .arg(p[0].get<double>()).arg(p[1].get<double>()).arg(p[2].get<double>());
                }
                feedback_->setText(lines.join("\n"));
            } else if (status != 200) feedback_->setText(text(value,"message"));
            buttons();
        });
    }
    void submit() {
        if (!plan_.is_object() || writing_) return;
        const auto scope = scope_;
        writing_ = true;
        buttons();
        api("PUT",path(),plan_,[this,scope](const Json& value,int status) {
            writing_ = false;
            if (scope != scope_) { buttons(); return; }
            if (status == 200) {
                input_->clear();
                applyLayout(value);
                feedback_->setText(tr("已提交，其他客户端会收到同一工程的更新。"));
            } else {
                plan_ = nullptr;
                feedback_->setText(text(value,"message"));
                nextLayout_ = 0;
            }
            buttons();
        });
    }
    void poll() {
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (session_.isEmpty()) {
            if (sessionPending_ || now < retry_) return;
            sessionPending_ = true;
            api("POST","/sessions",{{"client_kind","qt"}},[this](const Json& value,int status) {
                sessionPending_ = false;
                if (status == 201) {
                    session_ = text(value,"client_id").toLatin1();
                    nextLayout_ = nextCatalog_ = 0;
                } else {
                    retry_ = QDateTime::currentMSecsSinceEpoch() + 1600;
                    auto* message = project_.is_object() ? feedback_ : catalogStatus_;
                    message->setText(tr("正在连接场景服务 %1").arg(endpoint_.toDisplayString()));
                }
            });
            return;
        }
        if (!project_.is_object()) {
            if (now < nextCatalog_) return;
            nextCatalog_ = now + 4500;
            api("GET","/projects",nullptr,[this](const Json& value,int status) {
                if (status != 200) { catalogStatus_->setText(text(value,"message")); return; }
                const auto projects = value.at("projects");
                if (projects != projects_) { projects_ = projects; list(projects_); }
            });
            return;
        }
        const auto scope = scope_;
        if (!layoutPending_ && now >= nextLayout_) {
            layoutPending_ = true;
            nextLayout_ = now + 1100;
            api("GET",path(),nullptr,[this,scope](const Json& value,int status) {
                layoutPending_ = false;
                if (scope != scope_) return;
                if (status == 200) applyLayout(value);
                else feedback_->setText(text(value,"message"));
            });
        }
        if (!layout_.is_object() || viewPending_ || now < viewRetry_) return;
        if (viewId_.isEmpty()) {
            viewPending_ = true;
            const auto version = cameraVersion_;
            api("POST","/views",{{"project_id",project_.at("project_id")},
                {"published_revision_id",project_.at("published_revision_id")},{"camera",earth_->camera()}},
                [this,scope,version](const Json& value,int status) {
                    viewPending_ = false;
                    if (scope != scope_) return;
                    if (status == 201) { viewId_ = text(value,"view_id"); sentCamera_ = version; }
                    else { frameStatus_->setText(text(value,"message")); viewRetry_ = QDateTime::currentMSecsSinceEpoch()+1800; }
                });
            return;
        }
        if (cameraVersion_ != sentCamera_) {
            viewPending_ = true;
            const auto version = cameraVersion_;
            api("PUT","/views/" + viewId_ + "/camera",earth_->camera(),[this,scope,version](const Json& value,int status) {
                viewPending_ = false;
                if (scope != scope_) return;
                if (status == 204) sentCamera_ = version;
                else if (status == 404) { viewId_.clear(); frameToken_.clear(); }
                else frameStatus_->setText(text(value,"message"));
            });
            return;
        }
        viewPending_ = true;
        auto* reply = request("GET","/api/v1/views/" + viewId_ + "/frame",{},frameToken_);
        connect(reply,&QNetworkReply::finished,this,[this,reply,scope] {
            viewPending_ = false;
            reply->deleteLater();
            if (scope != scope_) return;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QPixmap pixels;
            if (status == 200 && pixels.loadFromData(reply->readAll(),"JPEG")) {
                frameToken_ = reply->rawHeader("X-Frame-Token");
                frameRevision_ = reply->rawHeader("X-Layout-Revision").toLongLong();
                earth_->image(std::move(pixels));
                frameStatus_->setText(tr("Vulkan 视口 · 共享工程 r%1").arg(frameRevision_));
            } else if (status == 401 || status == 404 || status == 503) {
                if (status == 401) session_.clear();
                viewId_.clear(); frameToken_.clear(); viewRetry_ = QDateTime::currentMSecsSinceEpoch() + 1100;
            }
        });
    }

    QUrl endpoint_;
    QNetworkAccessManager network_;
    QTimer timer_;
    QByteArray session_, frameToken_;
    QString viewId_, selected_;
    qint64 nextCatalog_ = 0, nextLayout_ = 0, retry_ = 0, viewRetry_ = 0, frameRevision_ = -1;
    uint64_t scope_ = 0, cameraVersion_ = 0, sentCamera_ = 0;
    bool sessionPending_ = false, layoutPending_ = false, viewPending_ = false, writing_ = false;
    Json project_, layout_, plan_, projects_;
    QStackedWidget* pages_;
    QWidget *catalogPage_, *editorPage_;
    QHBoxLayout* cards_;
    EarthScreen* earth_;
    QTreeWidget* tree_;
    QPlainTextEdit* input_;
    QLabel *catalogStatus_, *title_, *revision_, *frameStatus_, *selectedInfo_, *feedback_;
    QPushButton *preflight_, *apply_;
};
}

int runSharedSceneWorkspace(QApplication& app, const QString& endpoint) {
    SharedScene window(endpoint);
    window.show();
#ifdef VSGRAYSIM_FORMAL_VALIDATION
    if (const auto directory = qEnvironmentVariable("VSGRAYSIM_SHARED_SCENE_PROBE"); !directory.isEmpty()) {
        const QDir files(directory);
        auto* probe = new QTimer(&window);
        QObject::connect(probe,&QTimer::timeout,&window,[&window,&app,files,phase=0]() mutable {
            auto* open = window.findChild<QPushButton*>("open-taipei-coast");
            auto* preflight = window.findChild<QPushButton*>("sharedDeploymentPreview");
            auto* apply = window.findChild<QPushButton*>("sharedDeploymentApply");
            if (phase == 0 && open && open->isVisible()) {
                window.grab().save(files.filePath("qt-catalog.png"));
                open->click();
                window.findChild<QPlainTextEdit*>("sharedDeploymentInstruction")->setPlainText(
                    QStringLiteral("添加 survey-vessel 编号 qt-sea 到 (30, 0, 40) 米"));
                ++phase;
            } else if (phase == 1 && preflight->isEnabled()) {
                preflight->click();
                ++phase;
            } else if (phase == 2 && apply->isEnabled()) {
                QFile ready(files.filePath("qt-prepared"));
                if (ready.open(QIODevice::WriteOnly)) ++phase;
            } else if (phase == 3 && files.exists("browser-committed")) {
                apply->click();
                ++phase;
            } else if (phase == 4) {
                const auto status = window.findChild<QLabel*>("sharedFrameStatus")->text();
                const auto* tree = window.findChild<QTreeWidget*>("sharedSceneTree");
                if (status.contains(QStringLiteral("工程 r2")) &&
                    !tree->findItems(QStringLiteral("Skywalker X8 固定翼测绘无人机"),Qt::MatchExactly|Qt::MatchRecursive).isEmpty() &&
                    !tree->findItems(QStringLiteral("近岸测量船"),Qt::MatchExactly|Qt::MatchRecursive).isEmpty()) {
                    app.exit(window.grab().save(files.filePath("qt-workspace.png")) ? 0 : 2);
                }
            }
        });
        probe->start(200);
        QTimer::singleShot(240000,&app,[&app]{app.exit(3);});
    }
#endif
    return app.exec();
}
}
