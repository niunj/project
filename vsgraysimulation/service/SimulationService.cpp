#include "SimulationService.h"
#include "BrowserEarthSurface.h"
#include <QDateTime>
#include "WebWorkspace.h"
#include "ModelLibrary.h"
#include <Deployment.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpServerRequest>
#include <QHttpServerConfiguration>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QMimeDatabase>
#include <QProcess>
#include <QSaveFile>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>

namespace vsgraysim::service {
namespace {
using Json = nlohmann::json;
using Method = QHttpServerRequest::Method;
using Status = QHttpServerResponse::StatusCode;
using namespace std::chrono_literals;

constexpr auto leaseDuration = 12s;
constexpr auto sessionDuration = 45s;
constexpr auto commandTimeout = 15s;
struct ApiError { int status; const char* code; const char* message; };

std::string uniqueId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

Json readJson(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return nullptr;
    const auto body = file.readAll();
    return Json::parse(body.begin(), body.end(), nullptr, false);
}

Json bodyOf(const QHttpServerRequest& request) {
    const auto bytes = request.body();
    if (bytes.size() > 16 * 1024) throw ApiError{413, "body_too_large", "Request exceeds 16 KiB"};
    const auto json = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (!json.is_object()) throw ApiError{400, "invalid_json", "Expected one JSON object"};
    return json;
}

QHttpServerResponse response(int code, const Json& data = nullptr) {
    if (code == 204) return QHttpServerResponse(Status::NoContent);
    return QHttpServerResponse("application/json", QByteArray::fromStdString(data.dump()), Status(code));
}

QHttpServerResponse noCache(QHttpServerResponse result) {
    auto headers = result.headers();
    if (!headers.contains("Cache-Control")) headers.append("Cache-Control", "no-store");
    headers.replaceOrAppend("X-Content-Type-Options", "nosniff");
    headers.replaceOrAppend("Content-Security-Policy", "default-src 'self'; frame-ancestors 'none'; base-uri 'none'");
    result.setHeaders(std::move(headers));
    return result;
}

QHttpServerResponse frameResponse(WebWorkspace::Frame frame) {
    if (frame.jpeg.isEmpty()) return response(204);
    auto result = QHttpServerResponse("image/jpeg", std::move(frame.jpeg));
    auto headers = result.headers();
    for (const auto& [name,field] : {std::pair{"X-Frame-Sequence","sequence"}, {"X-Frame-Token","token"}, {"X-Sim-Tick","tick"}})
        headers.append(name, QByteArray::fromStdString(frame.stamp.at(field).get<std::string>()));
    for (const auto& [name,field] : {std::pair{"X-Layout-Revision","layout_revision"}, {"X-Camera-Revision","camera_input_revision"}})
        if (frame.stamp.contains(field)) headers.append(name, QByteArray::number(frame.stamp.at(field).get<qint64>()));
    if (frame.stamp.contains("camera_zoom"))
        headers.append("X-Camera-Zoom", QByteArray::number(frame.stamp.at("camera_zoom").get<double>()));
    if (frame.stamp.contains("ground_revision"))
        headers.append("X-Ground-Revision", QByteArray::number(frame.stamp.at("ground_revision").get<qulonglong>()));
    headers.append("X-Render-Ms", QByteArray::number(frame.stamp.value("render_ms", 0.), 'f', 1));
    headers.append("X-Encode-Ms", QByteArray::number(frame.stamp.value("encode_ms", 0.), 'f', 1));
    result.setHeaders(std::move(headers));
    return result;
}

QHttpServerResponse earthChunk(const QString& root, const QString& name, QStringView extension, int maximum) {
    const bool image = name.endsWith(".png");
    if (root.isEmpty() || name.size()!=68 || (!image && !name.endsWith(extension)) ||
        !std::all_of(name.begin(),name.begin()+64,[](QChar c) {
            return (c>=u'0'&&c<=u'9')||(c>=u'a'&&c<=u'f');
        })) throw ApiError{404,"earth_missing","体素地球资源不存在"};
    const auto source=QFileInfo(QDir(root).filePath(name)).canonicalFilePath();
    QFile file(source);
    if (!source.startsWith(root+"/") || !file.open(QIODevice::ReadOnly) || file.size()>maximum)
        throw ApiError{404,"earth_missing","体素地球资源不存在"};
    QHttpServerResponse result(image ? "image/png" : "application/octet-stream",file.readAll());
    auto headers=result.headers();
    headers.append("Cache-Control","private, max-age=31536000, immutable");
    headers.append("ETag",QByteArray("\"")+name.left(64).toLatin1()+'"');
    result.setHeaders(std::move(headers));
    return result;
}

template<class T>
T number(const Json& value) {
    if (value.is_number_unsigned() && value.get<uint64_t>() <= std::numeric_limits<T>::max())
        return T(value.get<uint64_t>());
    if (value.is_string()) {
        const auto& input = value.get_ref<const std::string&>();
        T parsed{};
        const auto result = std::from_chars(input.data(), input.data() + input.size(), parsed);
        if (result.ec == std::errc{} && result.ptr == input.data() + input.size()) return parsed;
    }
    throw ApiError{400, "invalid_counter", "Invalid unsigned counter or identifier"};
}

const Json* findId(const Json& list, const char* key, uint32_t id) {
    for (const auto& entry : list)
        if (entry.value(key, uint32_t(0)) == id) return &entry;
    return nullptr;
}

Json publicProject(Json entry) {
    entry.erase("topic");
    return entry;
}
}

struct SimulationService::FrameWait {
    std::string view;
    QByteArray after;
    Clock::time_point deadline;
    QHttpServerResponder responder;
};

struct SimulationService::SurfaceWait {
    std::string run, generation;
    QByteArray after;
    Clock::time_point deadline;
    QHttpServerResponder responder;
};

struct SimulationService::Run {
    std::string id, project, publishedRevision, generation, pendingJob;
    QString topic, directory, checkpoint;
    Json status, demand, initialLayout = Json::object();
    Json browserSurface;
    QDateTime browserSurfaceModified;
    qint64 browserSurfaceSize = -1;
    QProcess process;
    struct QueuedCommand { std::string job, lease; Json message; };
    std::deque<QueuedCommand> queued;
    uint64_t seq = 0;
    Clock::time_point deadline;
    bool recovered = false;

    bool send(const Json& request) {
        if (process.state() != QProcess::Running || process.bytesToWrite() > 16384) return false;
        return process.write(QByteArray::fromStdString(request.dump() + "\n")) != -1;
    }
};

SimulationService::SimulationService(QString root, QString web)
    : root_(std::move(root)), web_(QDir(root_).absoluteFilePath(web)),
      models_(std::make_unique<ModelLibrary>(root_)),
      workspace_(std::make_unique<WebWorkspace>(root_, cache_.path(), *models_)) {
    auto configuration = http_.configuration();
    configuration.setMaximumUrlSize(2048);
    configuration.setMaximumBodySize(24 * 1024 * 1024);
    configuration.setMaximumConnections(128);
    http_.setConfiguration(configuration);
    http_.setMissingHandler(this, [this](const auto& request, auto& responder) {
        auto answer = handle(request);
        if (answer.statusCode() == Status::NoContent && request.method() == Method::Get &&
            request.value("X-Wait-For-Frame") == "1") {
            const auto parts = request.url().path().split('/');
            if (parts.size() == 6 && parts[3] == "views" && parts[5] == "frame") {
                const auto owner = request.value("X-Client-Id").toStdString();
                if (auto current = frameWaiters_.find(owner); current != frameWaiters_.end())
                    current->second->responder.sendResponse(std::move(answer));
                frameWaiters_[owner] = std::make_unique<FrameWait>(FrameWait{
                    parts[4].toStdString(),request.value("X-After-Frame"),Clock::now()+900ms,std::move(responder)});
                return;
            }
        }
        if (answer.statusCode() == Status::NoContent && request.method() == Method::Get &&
            request.value("X-Wait-For-Surface") == "1") {
            const auto parts=request.url().path().split('/');
            if (parts.size()==7 && parts[3]=="runs" && parts[5]=="earth") {
                const auto owner=request.value("X-Client-Id").toStdString();
                if (auto current=surfaceWaiters_.find(owner);current!=surfaceWaiters_.end())
                    current->second->responder.sendResponse(std::move(answer));
                surfaceWaiters_[owner]=std::make_unique<SurfaceWait>(SurfaceWait{
                    parts[4].toStdString(),parts[6].toStdString(),request.value("X-After-Surface"),
                    Clock::now()+900ms,std::move(responder)});
                return;
            }
        }
        responder.sendResponse(std::move(answer));
    });
    workspace_->setFrameNotification([this] { flushFrameWaiters(); });
    connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
}

SimulationService::~SimulationService() {
    timer_.stop();
    frameWaiters_.clear();
    surfaceWaiters_.clear();
    workspace_.reset();
    for (auto& [id, run] : runs_) {
        if (run->process.state() == QProcess::NotRunning) continue;
        run->process.closeWriteChannel();
    }
    for (auto& [id, run] : runs_) {
        if (run->process.state() == QProcess::NotRunning) continue;
        if (!run->process.waitForFinished(2000)) {
            run->process.kill();
            run->process.waitForFinished(1000);
        }
    }
}

quint16 SimulationService::listen(quint16 port) {
    const auto published = readJson(QDir(root_).filePath("config/service/projects.json"));
    if (!published.is_object() || published.value("format", "") != std::string("vsgraysim-service-projects-v1") ||
        !published.contains("projects") || !published["projects"].is_array()) return 0;
    std::set<std::pair<std::string, std::string>> versions;
    for (const auto& item : published.at("projects")) {
        if (!item.is_object() || !item.contains("project_id") || !item["project_id"].is_string() ||
            !item.contains("published_revision_id") || !item["published_revision_id"].is_string() ||
            !item.contains("name") || !item["name"].is_string() || !item.contains("topic") || !item["topic"].is_string())
            return 0;
        if (!versions.emplace(item.at("project_id").get<std::string>(), item.at("published_revision_id").get<std::string>()).second)
            return 0;
        const auto relative = QString::fromStdString(item.at("topic").get<std::string>());
        const auto file = QFileInfo(QDir(root_).filePath(relative)).canonicalFilePath();
        if (!file.startsWith(QFileInfo(root_).canonicalFilePath() + "/") ||
            (!file.endsWith(".vtopic.json") && !file.endsWith(".vsimscene"))) return 0;
    }
    catalog_ = published.at("projects");
    if (!cache_.isValid() || !socket_.listen(QHostAddress::LocalHost, port) || !http_.bind(&socket_)) return 0;
    timer_.start(75);
    return socket_.serverPort();
}

QHttpServerResponse SimulationService::handle(const QHttpServerRequest& request) {
    auto result = [&]() -> QHttpServerResponse {
        try {
            const auto host = request.value("Host");
            const auto port = QByteArray::number(socket_.serverPort());
            if (host != "127.0.0.1:" + port && host != "localhost:" + port)
                throw ApiError{403, "invalid_host", "Use the local service address"};
            const auto origin = request.value("Origin");
            if ((!origin.isEmpty() && origin != "http://" + host) ||
                request.value("Sec-Fetch-Site") == "cross-site")
                throw ApiError{403, "invalid_origin", "Only same-origin requests are accepted"};
            return dispatch(request);
        } catch (const ApiError& error) {
            return response(error.status, {{"code", error.code}, {"message", error.message}, {"request_id", uniqueId()}});
        } catch (const WorkspaceError& error) {
            return response(error.status, {{"code", error.code}, {"message", error.message}, {"request_id", uniqueId()}});
        } catch (const Json::exception&) {
            return response(400, {{"code", "invalid_request"}, {"message", "Missing or invalid request field"}, {"request_id", uniqueId()}});
        } catch (const std::exception&) {
            return response(500, {{"code", "workspace_error"}, {"message", "无法读取场景或准备工作区"}, {"request_id", uniqueId()}});
        }
    }();
    return noCache(std::move(result));
}

QHttpServerResponse SimulationService::dispatch(const QHttpServerRequest& request) {
    const auto method = request.method();
    const auto path = request.url().path();
    if (!path.startsWith("/api/v1/")) {
        QString relative = path.mid(1);
        if (path == "/" || path == "/design" || path == "/models") relative = "index.html";
        else if (path == "/simulation") relative = "simulation.html";
        const auto asset = QFileInfo(QDir(web_).filePath(relative)).canonicalFilePath();
        const auto root = QFileInfo(web_).canonicalFilePath();
        if (method == Method::Get && !root.isEmpty() && asset.startsWith(root + "/") &&
            (relative == "index.html" || relative == "simulation.html" || relative.startsWith("assets/"))) {
            QFile file(asset);
            if (file.open(QIODevice::ReadOnly) && file.size() <= 8 * 1024 * 1024)
                return QHttpServerResponse(QMimeDatabase().mimeTypeForFile(asset).name().toUtf8(), file.readAll());
        }
        throw ApiError{404, "not_found", "Resource not found"};
    }
    const auto parts = path.mid(8).split('/');
    const auto first = parts[0];
    auto client = request.value("X-Client-Id").toStdString();
    auto session = sessions_.find(client);
    if (session != sessions_.end() && session->second.expiry <= Clock::now()) session = sessions_.end();
    if (session != sessions_.end()) session->second.expiry = Clock::now() + sessionDuration;

    if (first == "sessions" && parts.size() == 1 && method == Method::Post) {
        poll();
        if (sessions_.size() >= 64) throw ApiError{429, "session_limit", "Too many active client sessions"};
        const auto id = uniqueId();
        const auto kind = request.body().isEmpty() ? std::string() : bodyOf(request).value("client_kind",std::string());
        const auto author = kind == "browser" ? "B/S" : kind == "qt" ? "Qt" : "API";
        sessions_.emplace(id, Session{Clock::now() + sessionDuration, {},std::string(author) + " · " + id.substr(0,6)});
        return response(201, {{"client_id", id}});
    }
    if (first == "capabilities" && parts.size() == 1 && method == Method::Get)
        return response(200, {{"api_version", "1"}, {"deployment", "localhost"},
            {"features", {{"multi_run", true}, {"run_control", true}, {"platform_telemetry", true},
                          {"shared_run_control", true}, {"draft_edit", true}, {"live_view", workspace_->available()},
                          {"shared_drafts", true}, {"distributed_workers", false},
                          {"web_voxel_earth", !qEnvironmentVariableIsEmpty("VSGRAYSIM_WEB_EARTH_ROOT") &&
                              QFileInfo(QDir(qEnvironmentVariable("VSGRAYSIM_WEB_EARTH_ROOT")).filePath("manifest.json")).isFile()},
                          {"frozen_view", false}, {"webrtc", false}, {"continuous_platform_input", false}}},
            {"models", Json::array()}, {"limits", {{"max_active_runs", 4}}},
            {"equipment_previews", {"visible", "infrared", "navigation", "depth_sounder"}},
            {"platform_command_ids", {"set_prescribed_course"}}});
    if (session == sessions_.end()) throw ApiError{401, "session_required", "Open a client session"};

    if (first == "earth" && method == Method::Get) {
        const auto configured = qEnvironmentVariable("VSGRAYSIM_WEB_EARTH_ROOT");
        if (configured.isEmpty()) throw ApiError{404,"earth_unpublished","体素地球瓦片包尚未发布"};
        const auto root = QFileInfo(configured).canonicalFilePath();
        if (!root.isEmpty() && parts.size()==2 && parts[1]=="manifest") {
            QFile file(QDir(root).filePath("manifest.json"));
            if (file.open(QIODevice::ReadOnly) && file.size()<=8*1024*1024)
                return QHttpServerResponse("application/json",file.readAll());
        }
        if (!root.isEmpty() && parts.size()==3 && parts[1]=="chunks")
            return earthChunk(root+"/chunks",parts[2],u".vxt",8*1024*1024);
        throw ApiError{404,"earth_missing","体素地球资源不存在"};
    }

    if (first == "sessions" && parts.size() == 2 && method == Method::Delete && parts[1].toStdString() == client) {
        sessions_.erase(session);
        workspace_->close(client);
        poll();
        return response(204);
    }
    const auto findTopic = [&](const std::string& project, const Json& revision) -> QString {
        for (const auto& entry : catalog_) {
            if (entry.at("project_id") == project && entry.at("published_revision_id") == revision)
                return QDir(root_).filePath(QString::fromStdString(entry.at("topic").get<std::string>()));
        }
        throw ApiError{404, "revision_not_found", "场景版本不存在"};
    };
    if (first == "models") {
        if (parts.size() == 1 && method == Method::Get) return response(200, {{"models",models_->list()}});
        if (parts.size() == 1 && method == Method::Post) {
            if (request.value("Content-Type") != "model/gltf-binary")
                throw ApiError{415,"model_format","请上传自包含 GLB 文件"};
            return response(201,models_->importGlb(request.body()));
        }
        if (parts.size() == 2 && method == Method::Get) return response(200,models_->get(parts[1].toStdString()));
        if (parts.size() == 2 && method == Method::Put)
            return response(200,models_->update(parts[1].toStdString(),bodyOf(request)));
    }
    if (first == "projects" && parts.size() == 3 && parts[2] == "layout-plan" && method == Method::Post) {
        const auto project = parts[1].toStdString();
        const auto revision = request.query().queryItemValue("revision", QUrl::FullyDecoded).toStdString();
        const auto layout = workspace_->layout(findTopic(project,revision),project,revision);
        const auto body = bodyOf(request);
        if (body.at("expected_revision") != layout.at("revision"))
            throw ApiError{409,"layout_conflict","草稿已更新，请重新读取"};
        const auto instruction = QString::fromStdString(body.at("instruction").get<std::string>()).trimmed();
        if (instruction.size() > 4096) throw ApiError{400,"plan_length","指令过长"};
        Json plan;
        try {
            if (instruction.startsWith('{')) plan = scene_deployment::parse(instruction.toStdString());
            else {
                const auto simple = scene_deployment::simplePlan(instruction.toStdString(),body.value("selected",std::string()));
                if (!simple)
                    throw ApiError{400,"plan_instruction","请用“添加 skywalker-x8 到 (东, 上, 南) 米”、选中目标后输入“移动到 (东, 上, 南) 米”，或输入部署 JSON"};
                plan = *simple;
            }
        } catch (const std::logic_error&) {
            throw ApiError{400,"invalid_plan","智能部署方案无效，请检查型号、目标编号和坐标"};
        }
        const auto preview = workspace_->previewDeployment(layout,plan);
        return response(200,{{"expected_revision",layout.at("revision")},{"positions",preview.at("positions")},{"plan",preview.at("preview_plan")}});
    }
    if (first == "projects" && parts.size() == 3 && parts[2] == "layout") {
        const auto project = parts[1].toStdString();
        const auto revision = request.query().queryItemValue("revision", QUrl::FullyDecoded).toStdString();
        const auto topic = findTopic(project, revision);
        if (method == Method::Get) return response(200, workspace_->layout(topic, project, revision));
        if (method == Method::Put) return response(200, workspace_->save(topic, project, revision, bodyOf(request), session->second.author));
    }
    if (first == "projects" && parts.size() == 3 && parts[2] == "model-binding" && method == Method::Put) {
        const auto project = parts[1].toStdString();
        const auto revision = request.query().queryItemValue("revision", QUrl::FullyDecoded).toStdString();
        return response(200, workspace_->bindModel(findTopic(project, revision), project, revision, bodyOf(request), session->second.author));
    }
    if (first == "views" && parts.size() == 1 && method == Method::Post) {
        const auto body = bodyOf(request);
        const auto camera = body.value("camera", Json::object());
        if (body.contains("model_id")) {
            const auto spec = models_->renderSpec(body.at("model_id").get<std::string>(),
                                                 body.value("representation","original"));
            return response(201,workspace_->open(client, {}, {}, camera, Json::object(), spec));
        }
        if (body.contains("run_id")) {
            const auto found = runs_.find(body.at("run_id").get<std::string>());
            if (found == runs_.end()) throw ApiError{404, "run_not_found", "运行会话不存在"};
            const auto& run = *found->second;
            if (body.at("expected_generation_id") != run.generation)
                throw ApiError{409, "generation_mismatch", "会话版本已更新"};
            return response(201, workspace_->open(client, run.topic, run.directory + "/status.json", camera, Json::object()));
        }
        const auto project = body.at("project_id").get<std::string>();
        const auto revision = body.at("published_revision_id").get<std::string>();
        const auto topic = findTopic(project, revision);
        const auto layout = workspace_->layout(topic, project, revision);
        const auto shared = camera.value("preset",std::string()) == "workspace" ? layout : Json(nullptr);
        return response(201, workspace_->open(client, topic, {}, camera, layout.at("positions"), nullptr,
            layout.at("model_bindings"), workspace_->deployment(layout), shared));
    }
    if (first == "views" && parts.size() == 3) {
        const auto id = parts[1].toStdString();
        if (parts[2] == "camera" && method == Method::Put) {
            workspace_->camera(client, id, bodyOf(request));
            return response(204);
        }
        if (parts[2] == "frame" && method == Method::Get) {
            return frameResponse(workspace_->frame(client, id, request.value("X-After-Frame")));
        }
    }
    if (first == "projects" && method == Method::Get) {
        if (parts.size() == 1) {
            Json projects = Json::array();
            for (const auto& entry : catalog_) projects.push_back(publicProject(entry));
            return response(200, {{"projects", projects}});
        }
        if (parts.size() == 3 && parts[2] == "revisions") {
            auto revisions = Json::array();
            for (const auto& entry : catalog_)
                if (entry.at("project_id") == parts[1].toStdString()) revisions.push_back(publicProject(entry));
            if (!revisions.empty()) return response(200, {{"revisions", revisions}});
        }
    }
    if (first == "leases" && parts.size() >= 2) {
        const auto it = leases_.find(parts[1].toStdString());
        if (it == leases_.end() || it->second.owner != client || it->second.expiry <= Clock::now())
            throw ApiError{404, "lease_not_found", "Lease expired or does not belong to this client"};
        if (parts.size() == 2 && method == Method::Delete) {
            leases_.erase(it);
            return response(204);
        }
        if (parts.size() == 3 && parts[2] == "renew" && method == Method::Post) {
            it->second.expiry = Clock::now() + leaseDuration;
            return response(200, {{"lease_id", it->first}, {"expires_in_ms", 12000}});
        }
    }
    if (first == "jobs" && parts.size() == 2 && method == Method::Get) {
        const auto it = jobs_.find(parts[1].toStdString());
        if (it != jobs_.end()) return response(200, it->second);
    }
    if (first == "runs" && parts.size() == 1 && method == Method::Get) {
        auto list = Json::array();
        for (const auto& [id, run] : runs_) list.push_back(runStatus(*run));
        return response(200, {{"runs", list}});
    }
    if (first == "runs" && parts.size() == 1 && method == Method::Post) {
        const auto data = bodyOf(request);
        const auto key = request.value("Idempotency-Key").toStdString();
        auto& submissions = session->second.submissions;
        if (!key.empty()) {
            if (key.size() > 96) throw ApiError{400, "invalid_key", "Idempotency key is too long"};
            if (const auto it = submissions.find(key); it != submissions.end()) {
                if (it->second.path != path.toStdString() || it->second.body != request.body())
                    throw ApiError{409, "idempotency_conflict", "Key already used with another request"};
                return response(202, it->second.response);
            }
            if (submissions.size() >= 128) throw ApiError{429, "submission_limit", "Too many keys in this session"};
        }
        if (runs_.size() >= 4) throw ApiError{409, "run_limit", "Close a stopped run first"};
        const auto project = data.at("project_id").get<std::string>();
        const auto found = std::find_if(catalog_.begin(), catalog_.end(), [&](const Json& p) {
            return p.at("project_id") == project && p.at("published_revision_id") == data.at("published_revision_id");
        });
        if (found == catalog_.end()) throw ApiError{404, "revision_not_found", "Published scene not found"};
        if (data.contains("checkpoint_id")) throw ApiError{400, "unsupported_checkpoint_id", "Use recover on an existing run"};
        auto run = std::make_unique<Run>();
        run->id = "r" + std::to_string(++nextRun_);
        run->project = project;
        run->publishedRevision = found->at("published_revision_id").get<std::string>();
        run->topic = QDir(root_).filePath(QString::fromStdString(found->at("topic").get<std::string>()));
        if (!QFileInfo::exists(run->topic)) throw ApiError{503, "scene_missing", "Published scene file is unavailable"};
        if (data.contains("layout_revision")) {
            const auto layout = workspace_->layout(run->topic, project, run->publishedRevision);
            if (data.at("layout_revision") != layout.at("revision"))
                throw ApiError{409, "layout_conflict", "启动前请重新打开最新草稿"};
            if (!layout.at("positions").empty() || !layout.at("model_bindings").empty() || !layout.at("deployments").empty())
                run->initialLayout = {{"positions",layout.at("positions")},{"model_bindings",layout.at("model_bindings")},
                                      {"deployment",workspace_->deployment(layout)}};
        }
        launch(*run);
        run->pendingJob = newJob(*run, "initialize");
        const Json result = {{"job_id", run->pendingJob}, {"run_id", run->id}, {"generation_id", run->generation}};
        runs_.emplace(run->id, std::move(run));
        if (!key.empty()) submissions.emplace(key, Submission{path.toStdString(), request.body(), result});
        return response(202, result);
    }
    if (first == "runs" && parts.size() >= 2) {
        const auto it = runs_.find(parts[1].toStdString());
        if (it != runs_.end()) {
            if (parts.size() == 2 && method == Method::Delete) {
                const auto state = it->second->status.value("state", std::string{});
                if (state != "stopped" && state != "fault")
                    throw ApiError{409, "run_active", "Stop the run before closing"};
                it->second->process.closeWriteChannel();
                if (!it->second->pendingJob.empty() || !it->second->queued.empty() ||
                    it->second->process.state() != QProcess::NotRunning)
                    throw ApiError{409, "run_closing", "Worker is exiting, retry shortly"};
                const auto runDirectory = QDir(cache_.path()).filePath(parts[1]);
                runs_.erase(it);
                QDir(runDirectory).removeRecursively();
                return response(204);
            }
            return runRequest(request, *it->second, parts);
        }
    }
    throw ApiError{404, "not_found", "API resource not found"};
}

QHttpServerResponse SimulationService::runRequest(const QHttpServerRequest& request, Run& run,
                                                  const QStringList& parts) {
    const auto method = request.method();
    const auto owner = request.value("X-Client-Id").toStdString();
    const auto world = run.status.value("platforms", Json::array());
    const auto devices = run.status.value("equipment", Json::array());
    if (method==Method::Post && parts.size()==5 && parts[2]=="earth" && parts[4]=="pick") {
        if (parts[3].toStdString()!=run.generation) throw ApiError{409,"generation_mismatch","会话版本已更新"};
        const auto data=bodyOf(request);
        const auto& latest=run.browserSurface;
        if (!latest.is_object() || latest.at("surface_revision")!=data.at("surface_revision"))
            throw ApiError{409,"earth_revision","地表已更新，请重新拾取"};
        const auto vector=[&](const char* name) {
            const auto& values=data.at(name);
            if (!values.is_array() || values.size()!=3) throw ApiError{400,"invalid_ray","拾取射线格式错误"};
            vsg::dvec3 result;
            for (int i=0;i<3;++i) {
                const auto n=values.at(i).get<double>();
                if (!std::isfinite(n) || std::abs(n)>2e6) throw ApiError{400,"invalid_ray","拾取射线超出范围"};
                result[i]=n;
            }
            return result;
        };
        const auto origin=vector("origin_m"),ray=vector("direction");
        if (std::abs(vsg::length(ray)-1.)>.001) throw ApiError{400,"invalid_ray","拾取方向必须归一化"};
        const auto hit=BrowserEarthSurface::intersect((run.directory+"/browser-earth").toStdString(),latest,origin,ray,300000.);
        if (!hit) return response(204);
        return response(200,{{"position_m",{hit->x,hit->y,hit->z}},{"surface_revision",latest.at("surface_revision")}});
    }
    if (method == Method::Get) {
        if (parts.size() == 2) return response(200, runStatus(run));
        if (parts.size()>=4 && parts[2]=="earth") {
            if (parts[3].toStdString()!=run.generation) throw ApiError{409,"generation_mismatch","会话版本已更新"};
            if (parts.size()==4) return surface(run,request.value("X-After-Surface"));
            if (parts.size()==5) return earthChunk(run.directory+"/browser-earth",parts[4],u".vxm",32*1024*1024);
        }
        if (parts.size() == 3 && parts[2] == "native-preview") return response(200, runStatus(run, true));
        if (parts.size() == 3 && parts[2] == "platforms")
            return response(200, {{"run_id", run.id}, {"generation_id", run.generation}, {"platforms", world},
                                  {"prescribed_controls", run.status.value("prescribed_controls", Json::object())}});
        if (parts.size() == 3 && parts[2] == "equipment")
            return response(200, {{"run_id", run.id}, {"generation_id", run.generation}, {"equipment", devices}});
        if (parts.size() == 4 && parts[2] == "platforms") {
            const auto id = number<uint32_t>(parts[3].toStdString());
            if (const auto* platform = findId(world, "platform_id", id)) return response(200, *platform);
        }
        if (parts.size() == 5 && parts[2] == "equipment" && parts[4] == "preview") {
            const auto id = number<uint32_t>(parts[3].toStdString());
            const auto* device = findId(devices, "id", id);
            if (!device) throw ApiError{404, "equipment_not_found", "Equipment is outside this run"};
            requireLease(run, owner, request.value("X-Lease-Id").toStdString(), "equipment_data", id);
            auto data = readJson(run.directory + "/frame-" + QString::number(id) + ".json");
            if (!data.is_object() || data.value("equipment_id", 0u) != id ||
                data.value("platform_id", 0u) != device->value("platform_id", 0u))
                throw ApiError{404, "product_pending", "No preview has been published yet"};
            const auto through = request.value("X-Through-Tick");
            const auto latest = run.status.value("tick", uint64_t(0));
            const auto tick = through.isEmpty() ? latest : std::min(latest, number<uint64_t>(through.toStdString()));
            const double time = tick * run.status.value("fixed_step_seconds", 0.);
            if (data.at("sim_time_s").get<double>() > time + 1e-9)
                throw ApiError{404, "product_pending", "No preview published through the requested tick"};
            data.update({{"run_id", run.id}, {"generation_id", run.generation}, {"through_tick", std::to_string(tick)}});
            return response(200, data);
        }
    }
    if (method == Method::Post) {
        const auto data = bodyOf(request);
        if (parts.size() == 3 && parts[2] == "leases") {
            if (data.at("expected_generation_id") != run.generation)
                throw ApiError{409, "generation_mismatch", "Refresh the run after a generation change"};
            return response(201, createLease(run, owner, data));
        }

        const bool clock = parts.size() == 3 && parts[2] == "commands";
        const bool platform = parts.size() == 5 && parts[2] == "platforms" && parts[4] == "commands";
        if (!clock && !platform) throw ApiError{404, "not_found", "Run operation not found"};
        const auto key = request.value("Idempotency-Key").toStdString();
        auto& submissions = sessions_.at(owner).submissions;
        if (!key.empty()) {
            if (key.size() > 96) throw ApiError{400, "invalid_key", "Idempotency key is too long"};
            if (const auto it = submissions.find(key); it != submissions.end()) {
                if (it->second.path != request.url().path().toStdString() || it->second.body != request.body())
                    throw ApiError{409, "idempotency_conflict", "Key already used with another request"};
                return response(202, it->second.response);
            }
            if (submissions.size() >= 128) throw ApiError{429, "submission_limit", "Too many keys in this session"};
        }
        if (data.at("expected_generation_id") != run.generation)
            throw ApiError{409, "generation_mismatch", "Refresh the run after a generation change"};
        Json command;
        std::string operation;
        const auto leaseId = data.at("control_lease_id").get<std::string>();
        if (clock) {
            requireLease(run, owner, leaseId, "run_control", 0);
            operation = data.at("command").get<std::string>();
            if (operation == "recover") {
                if (!run.pendingJob.empty() || !run.queued.empty() ||
                    run.process.state() != QProcess::NotRunning || !QFileInfo::exists(run.checkpoint))
                    throw ApiError{409, "recovery_unavailable", "Worker must be offline with a recovery checkpoint"};
                launch(run, run.checkpoint);
                run.pendingJob = newJob(run, "initialize");
                const Json result = {{"job_id", run.pendingJob}, {"run_id", run.id}, {"generation_id", run.generation}};
                if (!key.empty()) submissions.emplace(key, Submission{request.url().path().toStdString(), request.body(), result});
                return response(202, result);
            }
            if (operation != "play" && operation != "pause" && operation != "step" &&
                operation != "checkpoint" && operation != "stop")
                throw ApiError{400, "unsupported_command", "Unknown run command"};
            command = {{"command", operation}};
            if (operation == "step") command["expected_tick"] = number<uint64_t>(data.at("expected_tick"));
        } else {
            const auto id = number<uint32_t>(parts[3].toStdString());
            requireLease(run, owner, leaseId, "platform_control", id);
            const auto capabilities = run.status.value("prescribed_controls", Json::object());
            const auto found = capabilities.find(std::to_string(id));
            operation = data.at("command_id").get<std::string>();
            if (found == capabilities.end() || operation != "set_prescribed_course")
                throw ApiError{400, "unsupported_command", "Platform does not expose this command"};
            const auto& args = data.at("arguments");
            const auto heading = args.at("heading_deg").get<double>();
            const auto speed = args.at("speed_mps").get<double>();
            if (!std::isfinite(heading) || !std::isfinite(speed) || heading < 0 || heading >= 360 ||
                speed < 0 || speed > found->at("max_speed_mps").get<double>())
                throw ApiError{400, "course_range", "Heading or speed exceeds the platform limit"};
            command = {{"command", operation}, {"platform_id", id}, {"heading_deg", heading}, {"speed_mps", speed}};
        }
        if (!run.status.value("initialized", false) || run.process.state() != QProcess::Running)
            throw ApiError{409, "run_unavailable", "World is not ready"};
        publishDemand(run);
        auto result = queue(run, operation, std::move(command), leaseId);
        if (!key.empty()) submissions.emplace(key, Submission{request.url().path().toStdString(), request.body(), result});
        return response(202, result);
    }
    throw ApiError{404, "not_found", "Run resource not found"};
}

void SimulationService::launch(Run& run, const QString& checkpoint) {
    const auto previous = run.generation;
    run.generation = uniqueId();
    run.directory = cache_.path() + "/" + QString::fromStdString(run.id + "/" + run.generation.substr(0, 12));
    const auto ipc = run.directory + "/ipc";
    if (!QDir().mkpath(ipc)) throw ApiError{503, "storage_unavailable", "Cannot allocate worker storage"};
    const auto oldCheckpoint = checkpoint;
    if (oldCheckpoint.isEmpty() && !run.initialLayout.empty()) {
        QSaveFile layout(run.directory + "/layout.json");
        const auto bytes = QByteArray::fromStdString(run.initialLayout.dump());
        if (!layout.open(QIODevice::WriteOnly) || layout.write(bytes) != bytes.size() || !layout.commit())
            throw ApiError{503, "storage_unavailable", "Cannot stage draft for the run"};
    }
    run.seq = 0;
    run.demand = nullptr;
    run.browserSurface = nullptr;
    run.browserSurfaceModified = {};
    run.browserSurfaceSize = -1;
    run.recovered = !oldCheckpoint.isEmpty();
    run.status = {{"generation", run.generation}, {"state", "loading"},
                  {"stage", run.recovered ? "checkpoint_recovery" : "project"},
                  {"initialized", false}, {"running", false}, {"voxel_source", true}};
    for (auto it = leases_.begin(); it != leases_.end();) {
        if (it->second.run == run.id && it->second.generation == previous) it = leases_.erase(it);
        else ++it;
    }
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("VSGRAYSIM_IPC_DIRECTORY", ipc);
    environment.insert("VSGRAYSIM_MANAGED_RUN", "1");
    run.process.setProcessEnvironment(environment);
    run.process.setWorkingDirectory(root_);
    run.process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    if (!run.recovered)
        connect(&run.process, &QProcess::readyReadStandardOutput, &run.process, [&run] { run.process.readAllStandardOutput(); });
    QStringList args{"--engine-worker", run.topic, run.directory, QString::fromStdString(run.generation)};
    if (run.recovered) args << oldCheckpoint;
    run.deadline = Clock::now() + 3min;
    run.process.start(QCoreApplication::applicationFilePath(), args);
}

void SimulationService::poll() {
    workspace_->sweep();
    const auto now = Clock::now();
    std::erase_if(sessions_, [&](const auto& entry) { return entry.second.expiry <= now; });
    std::erase_if(leases_, [&](const auto& entry) {
        return entry.second.expiry <= now || !sessions_.contains(entry.second.owner) || !runs_.contains(entry.second.run);
    });
    for (auto& [id, run] : runs_) {
        refresh(*run);
        publishDemand(*run);
        dispatchNext(*run);
    }
    flushFrameWaiters();
    flushSurfaceWaiters();
}

QHttpServerResponse SimulationService::surface(const Run& run, const QByteArray& after) const {
    const auto& latest=run.browserSurface;
    if (!latest.is_object() || latest.value("generation_id",std::string{})!=run.generation ||
        latest.value("surface_revision",std::string{})==after.toStdString()) return response(204);
    return response(200,latest);
}

void SimulationService::flushSurfaceWaiters() {
    const auto now=Clock::now();
    for (auto it=surfaceWaiters_.begin();it!=surfaceWaiters_.end();) {
        auto& waiting=*it->second;
        const auto run=runs_.find(waiting.run);
        auto result=[&]() -> QHttpServerResponse {
            if (!sessions_.contains(it->first)) return response(401,{{"code","session_required"},{"message","Open a client session"}});
            if (run==runs_.end() || run->second->generation!=waiting.generation)
                return response(409,{{"code","generation_mismatch"},{"message","会话版本已更新"}});
            return surface(*run->second,waiting.after);
        }();
        if (result.statusCode()==Status::NoContent && now<waiting.deadline) {++it;continue;}
        waiting.responder.sendResponse(noCache(std::move(result)));
        it=surfaceWaiters_.erase(it);
    }
}

void SimulationService::flushFrameWaiters() {
    const auto now = Clock::now();
    for (auto it = frameWaiters_.begin(); it != frameWaiters_.end();) {
        auto& waiting = *it->second;
        auto result = [&]() -> QHttpServerResponse {
            try {
                const auto session = sessions_.find(it->first);
                if (session == sessions_.end() || session->second.expiry <= now)
                    return response(401, {{"code","session_required"},{"message","Open a client session"}});
                return frameResponse(workspace_->frame(it->first,waiting.view,waiting.after));
            } catch (const WorkspaceError& error) {
                return response(error.status, {{"code",error.code},{"message",error.message}});
            } catch (const std::exception&) {
                return response(500, {{"code","view_frame"},{"message","无法读取三维视口"}});
            }
        }();
        if (result.statusCode() == Status::NoContent && now < waiting.deadline) { ++it; continue; }
        auto headers = result.headers();
        headers.append("X-Frame-Wait", "1");
        result.setHeaders(std::move(headers));
        waiting.responder.sendResponse(noCache(std::move(result)));
        it = frameWaiters_.erase(it);
    }
}

void SimulationService::refresh(Run& run) {
    const auto status = readJson(run.directory + "/status.json");
    if (status.is_object() && status.value("generation", "") == run.generation)
        run.status = status;
    if (!qEnvironmentVariableIsEmpty("VSGRAYSIM_WEB_EARTH_ROOT")) {
        const QFileInfo publication(run.directory+"/browser-earth.json");
        if (publication.isFile() && (publication.lastModified()!=run.browserSurfaceModified || publication.size()!=run.browserSurfaceSize)) {
            const auto current=readJson(publication.filePath());
            if (current.is_object() && current.value("generation_id",std::string{})==run.generation) {
                run.browserSurface=current;
                run.browserSurfaceModified=publication.lastModified();
                run.browserSurfaceSize=publication.size();
            }
        }
    }
    if (run.status.contains("recovery_checkpoint"))
        run.checkpoint = QString::fromStdString(run.status.at("recovery_checkpoint").get<std::string>());
    const bool workerExited = run.process.state() == QProcess::NotRunning;
    const bool stopped = run.status.value("state", "") == std::string("stopped");
    if (workerExited && !stopped) {
        if (!run.status.contains("error")) run.status["error"] = "Engine process exited (code " + std::to_string(run.process.exitCode()) + ")";
        run.status.update({{"state", "fault"}, {"initialized", false}, {"running", false}});
    }
    if (run.pendingJob.empty()) return;
    auto& job = jobs_.at(run.pendingJob);
    const bool startup = job.at("kind") == "initialize";
    const bool acknowledged = startup ? run.status.value("initialized", false) :
        run.status.value("ack", uint64_t(0)) == run.seq;
    if (acknowledged) {
        const auto error = startup ? std::string{} : run.status.value("command_error", std::string{});
        job["state"] = error.empty() ? "succeeded" : "failed";
        job["applied_at"] = runStatus(run).at("clock");
        if (!startup && run.status.contains("ack_stamp")) job["applied_at"].update(run.status.at("ack_stamp"));
        if (error.empty()) {
            job["result"] = startup ? Json{{"run", runStatus(run)}} : Json{{"applied_at", job.at("applied_at")}};
        } else {
            job["error"] = error;
        }
    } else if (workerExited || Clock::now() > run.deadline) {
        job["state"] = "failed";
        job["error"] = run.status.value("error", std::string("Worker acknowledgement timed out"));
        if (!workerExited) run.process.kill();
    } else {
        return;
    }
    run.pendingJob.clear();
}

void SimulationService::publishDemand(Run& run) {
    if (!run.status.value("initialized", false)) return;
    const auto now = Clock::now();
    std::set<uint32_t> devices;
    bool world = false;
    for (const auto& [id, lease] : leases_) {
        if (lease.run != run.id || lease.generation != run.generation || lease.expiry <= now) continue;
        if (lease.kind == "world") world = true;
        if (lease.kind == "equipment_data") devices.insert(lease.target);
    }
    const Json demand = {{"generation", run.generation}, {"command", "subscriptions"}, {"world", world}, {"devices", devices}};
    if (run.demand != demand && run.send(demand)) run.demand = demand;
}

std::string SimulationService::newJob(const Run& run, const std::string& kind) {
    if (jobs_.size() >= 512) {
        const auto finished = std::find_if(jobs_.begin(), jobs_.end(), [](const auto& entry) { return entry.second.at("state") != "pending"; });
        if (finished != jobs_.end()) jobs_.erase(finished);
    }
    const auto id = uniqueId();
    jobs_.emplace(id, Json{{"job_id", id}, {"run_id", run.id}, {"generation_id", run.generation}, {"kind", kind}, {"state", "pending"}});
    return id;
}

SimulationService::Json SimulationService::queue(Run& run, const std::string& operation, Json command,
                                                  const std::string& lease) {
    if (run.queued.size() >= 16) throw ApiError{429, "run_queue_full", "The run command queue is full"};
    const auto job = newJob(run, operation);
    run.queued.push_back({job, lease, std::move(command)});
    dispatchNext(run);
    return {{"job_id", job}, {"run_id", run.id}, {"generation_id", run.generation}};
}

void SimulationService::dispatchNext(Run& run) {
    while (run.pendingJob.empty() && !run.queued.empty()) {
        auto command = std::move(run.queued.front());
        run.queued.pop_front();
        const auto lease = leases_.find(command.lease);
        auto& job = jobs_.at(command.job);
        if (lease == leases_.end() || lease->second.expiry <= Clock::now() ||
            !sessions_.contains(lease->second.owner)) {
            job.update({{"state", "failed"}, {"error", "Control lease expired or was released"}});
            continue;
        }
        const auto expiration = std::chrono::duration_cast<std::chrono::milliseconds>(lease->second.expiry.time_since_epoch());
        command.message.update({{"generation", run.generation}, {"seq", run.seq + 1}, {"valid_until_ms", expiration.count()}});
        if (!run.send(command.message)) {
            job.update({{"state", "failed"}, {"error", "Engine worker unavailable"}});
            continue;
        }
        ++run.seq;
        run.pendingJob = std::move(command.job);
        auto budget = std::chrono::duration_cast<std::chrono::milliseconds>(commandTimeout);
        const auto operation = command.message.at("command").get<std::string>();
        if (operation == "pause" || operation == "step" || operation == "checkpoint" || operation == "stop") {
            const auto saveMs = std::clamp(run.status.value("checkpoint_ms",0.),0.,42000.);
            budget = std::max(budget,std::chrono::milliseconds(static_cast<int64_t>(saveMs * 2 + 5000)));
        }
        run.deadline = Clock::now() + budget;
    }
}

SimulationService::Json SimulationService::createLease(Run& run, const std::string& owner, const Json& data) {
    const auto kind = data.at("kind").get<std::string>();
    const auto now = Clock::now();
    uint32_t target = 0;
    if (kind == "equipment_data") {
        target = number<uint32_t>(data.at("equipment_id"));
        const auto equipment = run.status.value("equipment", Json::array());
        const auto* unit = findId(equipment, "id", target);
        if (!unit) throw ApiError{404, "equipment_not_found", "Equipment is outside this run"};
        if (!unit->value("preview_supported", false))
            throw ApiError{400, "preview_unavailable", "This equipment does not yet have a service preview"};
    } else if (kind == "platform_control") {
        target = number<uint32_t>(data.at("platform_id"));
        if (!run.status.value("prescribed_controls", Json::object()).contains(std::to_string(target)))
            throw ApiError{404, "platform_not_controllable", "Platform has no course control"};
    } else if (kind != "run_control" && kind != "world") {
        throw ApiError{400, "unsupported_lease", "Unknown lease type"};
    }
    const auto result = [&](const std::string& id) {
        return Json{{"lease_id", id}, {"client_id", owner}, {"run_id", run.id}, {"generation_id", run.generation},
                    {"expires_in_ms", 12000}, {"kind", kind}};
    };
    for (auto& [id, existing] : leases_) {
        if (existing.run != run.id || existing.generation != run.generation || existing.kind != kind ||
            existing.target != target || existing.expiry <= now || !sessions_.contains(existing.owner)) continue;
        if (existing.owner == owner) {
            existing.expiry = now + leaseDuration;
            return result(id);
        }
        if (kind == "platform_control" || kind == "run_control")
            throw ApiError{409, "already_controlled", "Another client holds control of this resource"};
    }
    const auto id = uniqueId();
    leases_.emplace(id, Lease{owner, run.id, run.generation, kind, target, now + leaseDuration});
    return result(id);
}

const SimulationService::Lease& SimulationService::requireLease(const Run& run, const std::string& owner,
    const std::string& id, const std::string& kind, uint32_t target) const {
    const auto it = leases_.find(id);
    if (it == leases_.end() || it->second.owner != owner || it->second.expiry <= Clock::now() ||
        it->second.kind != kind || it->second.target != target || it->second.run != run.id ||
        it->second.generation != run.generation)
        throw ApiError{403, "lease_required", "A valid lease for this run and resource is required"};
    return it->second;
}

SimulationService::Json SimulationService::runStatus(const Run& run, bool native) const {
    auto out = run.status;
    out.update({{"run_id", run.id}, {"generation_id", run.generation}, {"project_id", run.project},
        {"published_revision_id", run.publishedRevision}, {"recovered", run.recovered},
        {"play_requested", out.value("run_requested", false)},
        {"clock", {{"run_id", run.id}, {"generation_id", run.generation},
                   {"tick", std::to_string(out.value("tick", uint64_t(0)))},
                   {"state_revision", std::to_string(out.value("state_revision", uint64_t(0)))},
                   {"content_revision", std::to_string(out.value("content_revision", uint64_t(0)))},
                   {"sim_time_ns", std::to_string(std::llround(out.value("sim_time", 0.) * 1e9))},
                   {"sim_time_s", out.value("sim_time", 0.)}}},
        {"fixed_step_s", out.value("fixed_step_seconds", 0.)}});
    if (native) {
        out["project"] = run.topic.toStdString();
        return out;
    }
    if (out.contains("earth") && out.at("earth").is_object() && out.at("earth").contains("geo_anchor"))
        out["geo_anchor"]=out.at("earth").at("geo_anchor");
    out.erase("earth");
    out.erase("recovery_checkpoint");
    out.erase("source");
    out.erase("subscription_leases");
    return out;
}
}
