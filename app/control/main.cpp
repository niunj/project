#ifdef VSGRAYSIM_FORMAL_VALIDATION
#include "testing/FormalChainValidation.h"
#endif
/**
 * @file control/main.cpp
 * @brief 统一显控软件入口 — 启动即进入"链接装配"界面。
 *
 * 设计背景:
 *   不再为每类装备单独写一个显控程序, 而是进入统一显控工作台后, 直接展示
 *   仿真进程在线报告的平台/装备; 用户勾选装备完成链接装订后, 依装备与平台
 *   类型装配观测面板。提供综合、飞机、雷达、无人机模板，并支持自定义布局文件。
 *
 * 用法:
 *   vsgraysim_control                 # 进入链接装配界面
 *   vsgraysim_control --equipment=5,12  # (可选) 为各通道强制指定装备 id
 *   vsgraysim_control --workmode=aircraft       # 飞机操作台模板
 *   vsgraysim_control --workspace=/path/operator.workspace.json
 */

#include <QApplication>
#include <QSurfaceFormat>
#include <QTimer>
#include <QMessageBox>
#include <QFileInfo>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QPixmap>
#include <QFile>
#include <QSocketNotifier>
#include <fcntl.h>
#include <vsgraysim/broker/BrokerNet.h>

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "display/ControlWorkbench.h"
#include "display/VoxelEquipmentWorkbench.h"
#include "display/ConsoleSelector.h"
#include "base/ThemeManager.h"
#include <telemetry/TelemetryManager.h>

#ifndef QT6_PLUGIN_PATH
#  define QT6_PLUGIN_PATH ""
#endif

static void preferNvidiaVulkanIcd() {
    // Respect a deployment's explicit ICD, including software Vulkan for tests.
    for(const char* key:{"VK_DRIVER_FILES","VK_ICD_FILENAMES","VK_ICD_FILE_LIST"})
        if(const char* existing=std::getenv(key);existing&&*existing)return;
    const char* candidates[] = {
        "/usr/share/vulkan/icd.d/nvidia_icd.json",
        "/etc/vulkan/icd.d/nvidia_icd.json",
        nullptr
    };
    for (auto* p = candidates; *p; ++p) {
        if (access(*p, R_OK) == 0) {
            setenv("VK_ICD_FILE_LIST", *p, 0);
            break;
        }
    }
}

/// 解析 --key=value; 返回该 key 的值 (未找到返回空串)
static QString argValue(int argc, char** argv, const char* key)
{
    const QString prefix = QStringLiteral("--%1=").arg(QString::fromLatin1(key));
    for (int i = 1; i < argc; ++i) {
        QString a = QString::fromUtf8(argv[i]);
        if (a.startsWith(prefix)) return a.mid(prefix.size());
    }
    return {};
}
template<class F,class T> static nlohmann::json productDigest(const F& frame,const std::vector<T>& values){return {{"frame_id",frame.frame_id},{"sim_time",frame.sim_time},{"surface_revision",frame.surface_revision},{"payload_sha256",QCryptographicHash::hash(QByteArray::fromRawData(reinterpret_cast<const char*>(values.data()),qsizetype(values.size()*sizeof(T))),QCryptographicHash::Sha256).toHex().toStdString()}};}

static nlohmann::json radarDigest(const vsgraysim::RadarFrame& frame){
    auto out=productDigest(frame,frame.rd_map);
    if(frame.ground){
        out["ground_samples"]=frame.ground->size();
        out["ground_sha256"]=productDigest(frame,*frame.ground).at("payload_sha256");
    }
    return out;
}

int main(int argc, char** argv) {
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            fs::path dir = exe.parent_path();
            for (int i = 0; i < 8 && !dir.empty(); ++i) {
                if (fs::exists(dir / "config", ec) && fs::exists(dir / "models", ec)) {
                    fs::current_path(dir);
                    break;
                }
                dir = dir.parent_path();
            }
        }
    }

    preferNvidiaVulkanIcd();

    if (QT6_PLUGIN_PATH[0] != 0)
        qputenv("QT_PLUGIN_PATH", QT6_PLUGIN_PATH);
    qputenv("QT_QPA_PLATFORM", "xcb");

    // 启动即进入工作台；--workmode 选择模板，--equipment 等待指定装备上线。
    QString workmode  = argValue(argc, argv, "workmode");
    const QString equip_str = argValue(argc, argv, "equipment");
    const QString workspace_file = argValue(argc, argv, "workspace");
    const auto equipmentView=argValue(argc,argv,"equipment-view");
    const auto consoleView=argValue(argc,argv,"console-view");
    const auto viewState=argValue(argc,argv,"view-state");
    const auto viewScene=argValue(argc,argv,"view-scene");
    const bool managed=argValue(argc,argv,"view-managed")=="1";
    QString cockpit_profile=argValue(argc,argv,"cockpit-profile");
    QStringList forced_eq = equip_str.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if(!equipmentView.isEmpty())forced_eq={equipmentView};

    QApplication app(argc, argv);
    app.setApplicationName("vsgraysim_control");
    app.setOrganizationName("vsgraysim");
    app.setApplicationDisplayName("VSG Display-Control Workbench");
    vsgraysim::ui::ThemeManager::applyDarkTheme(&app);
    if(!cockpit_profile.isEmpty()&&!cockpit_profile.endsWith(".json"))cockpit_profile="config/cockpits/"+cockpit_profile+".json";
    auto voxel_equipment=argValue(argc,argv,"voxel-equipment");
    // Production always opens the existing online platform/equipment assembly.
    // Console skins are views of linked platforms, not a prerequisite to link.
    // The explicitly requested --voxel-equipment path remains a developer tool.

    {
        // 异步日志: 帧数据注入/渲染热路径不被日志 I/O 阻塞。
        spdlog::init_thread_pool(8192, 1);
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::async_logger>(
            "control", sink, spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);
        logger->set_level(spdlog::level::info);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
        spdlog::set_default_logger(logger);
    }

    spdlog::info("=== Display-Control Workbench (workmode={}) ===", workmode.toStdString());

    // 遥测初始化 (统一入口, 以工作模式或固定名作为服务名)
    auto telemetry = std::make_shared<vsgraysim::telemetry::TelemetryManager>();
    const std::string svc = workmode.isEmpty()
        ? std::string("control_assemble")
        : workmode.toStdString();
    telemetry->init(svc);

    // 初始化 CPU 基准
    vsgraysim::telemetry::TelemetryManager::readCpuPercent(true);

    // 定时健康上报 (CPU/内存)
    QTimer* health_timer = new QTimer();
    QObject::connect(health_timer, &QTimer::timeout, [telemetry]() {
        telemetry->setCpuPercent(
            vsgraysim::telemetry::TelemetryManager::readCpuPercent());
        telemetry->setMemBytes(
            vsgraysim::telemetry::TelemetryManager::readMemoryBytes());
    });
    health_timer->start(2000);

    int rc;
    {
        if(!voxel_equipment.isEmpty()){rc=vsgraysim::gui::display::runVoxelEquipmentWorkbench(voxel_equipment,argValue(argc,argv,"capture-equipment"),workmode.isEmpty()?"mission":workmode,cockpit_profile);}
        else {
        const bool platformMode=QStringList{"aircraft","helicopter","airliner","uav","aircraft_cockpit","uav_cockpit"}.contains(workmode);
        const bool separate=equipmentView.isEmpty()&&consoleView.isEmpty()&&!platformMode&&!qEnvironmentVariableIsSet("VSGRAYSIM_INLINE_DISPLAY_REFERENCE");
        vsgraysim::gui::display::ControlWorkbench window(workmode, forced_eq,nullptr,true,separate,!equipmentView.isEmpty());
        window.setExpectedScene(viewScene.toStdString());window.setPreferredPlatform(argValue(argc,argv,"preferred-platform").toUInt());
        if(!cockpit_profile.isEmpty()){QString error;if(!window.setCockpitProfile(cockpit_profile,error))QMessageBox::warning(&window,QObject::tr("无法打开机型显控台"),error);}
        if(!workspace_file.isEmpty()) {
            QString error;
            if(!window.importWorkspace(QFileInfo(workspace_file).absoluteFilePath(),error))
                QMessageBox::warning(&window,QObject::tr("无法载入工作台"),error);
        }
        window.show();
        if(!equipmentView.isEmpty())window.resize(900,650);
        QTimer stateTimer;stateTimer.setInterval(200);
        if(!equipmentView.isEmpty()&&!viewState.isEmpty()){
            QObject::connect(&stateTimer,&QTimer::timeout,&window,[&]{auto state=window.connectionStatus();const auto meta=vsgraysim::BrokerNet::instance().lastSimMeta();state["equipment_view"]=equipmentView.toUInt();state["expected_scene"]=viewScene.toStdString();state["pid"]=::getpid();state["scene_session_id"]=meta.scene_session_id;state["running"]=meta.running;state["online"]=vsgraysim::BrokerNet::instance().simOnline();state["client_id"]=vsgraysim::BrokerNet::instance().flightClientId();
                if(!meta.running){const auto id=equipmentView.toUInt();auto& broker=vsgraysim::SimDataBroker::instance();if(auto f=broker.visSnapshot(id))state["product"]=productDigest(*f,f->intensity);else if(auto f=broker.irSnapshot(id))state["product"]=productDigest(*f,f->radiance);else if(auto f=broker.radarSnapshot(id))state["product"]=radarDigest(*f);else if(auto f=broker.sarSnapshot(id))state["product"]=productDigest(*f,f->echo_data);}
                const auto bytes=QByteArray::fromStdString(state.dump());if(bytes.size()<512*1024){QSaveFile file(viewState);if(file.open(QIODevice::WriteOnly)){file.write(bytes);file.commit();}}
#ifdef VSGRAYSIM_FORMAL_VALIDATION
                if(qEnvironmentVariableIsSet("VSGRAYSIM_WORKER_PROBE")&&QFile::exists(viewState+".capture")){window.grab().save(viewState+".png");if(auto radar=vsgraysim::SimDataBroker::instance().radarSnapshot(equipmentView.toUInt())){std::ofstream output((viewState+".rd-f32").toStdString(),std::ios::binary);output.write(reinterpret_cast<const char*>(radar->rd_map.data()),std::streamsize(radar->rd_map.size()*sizeof(std::complex<float>)));}QFile::remove(viewState+".capture");}
#endif
            });stateTimer.start();
        }
        std::unique_ptr<QSocketNotifier> commands;QByteArray commandBuffer;
        if(managed){::fcntl(STDIN_FILENO,F_SETFL,::fcntl(STDIN_FILENO,F_GETFL,0)|O_NONBLOCK);commands=std::make_unique<QSocketNotifier>(STDIN_FILENO,QSocketNotifier::Read,&window);QObject::connect(commands.get(),&QSocketNotifier::activated,&window,[&]{char bytes[1024];ssize_t count;while((count=::read(STDIN_FILENO,bytes,sizeof(bytes)))>0){commandBuffer.append(bytes,int(count));if(commandBuffer.size()>8192){app.quit();return;}}if(count==0){commands->setEnabled(false);app.quit();return;}int end;while((end=commandBuffer.indexOf('\n'))>=0){const auto command=commandBuffer.left(end);commandBuffer.remove(0,end+1);if(command=="quit")app.quit();else if(command=="raise"){window.showNormal();window.raise();window.activateWindow();}}});}
#ifdef VSGRAYSIM_FORMAL_VALIDATION
        if(equipmentView.isEmpty()&&consoleView.isEmpty())vsgraysim::gui::installFormalValidation(window);
#endif

        spdlog::info("Control Workbench entering event loop");
        rc = app.exec();

        }
        vsgraysim::BrokerNet::instance().stop();
        health_timer->stop();
        telemetry->shutdown();
        spdlog::info("Control Workbench exiting");
    }   // 窗口先析构 (析构链会打日志), 之后才能关异步日志线程池。
    spdlog::shutdown();
    return rc;
}
