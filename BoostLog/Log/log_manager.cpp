#include "log_manager.h"
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/core/null_deleter.hpp>
#include <algorithm>
#include <ctime>
#include "Common/loginfo.h"

#ifdef _WIN32
#include <windows.h>
#include <QDebug>
#else
#include <QDebug>
#endif

// Boost.Log相关命名空间别名，简化代码书写
namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace expr = boost::log::expressions;
namespace fs = boost::filesystem;
namespace pt = boost::property_tree;

// 全局日志配置对象，存储从配置文件加载的日志参数
static LogConfig global_config;

// 前向声明LogInfo类（UI日志显示界面），避免循环包含
class LogInfo;

#ifdef _WIN32
// Windows 调试输出流缓冲区，将输出同时发送到 std::cout、OutputDebugString 和 Qt 调试器
class DebugOutputBuf : public std::streambuf {
public:
    DebugOutputBuf() : std::streambuf() {
        qDebug() << "=== DebugOutputBuf 构造函数调用 ===";
    }

protected:
    virtual std::streamsize xsputn(const char* s, std::streamsize count) override {
        if (s && count > 0) {
            std::cout.write(s, count);
            std::cout.flush();
            OutputDebugStringA(s);
            QString msg = QString::fromLocal8Bit(s, count);
            qDebug().noquote() << msg;
        }
        return count;
    }

    virtual int overflow(int c) override {
        if (c != EOF) {
            char ch = static_cast<char>(c);
            std::cout.put(ch);
            std::cout.flush();
            OutputDebugStringA(&ch);
            qDebug().noquote() << QString(QLatin1Char(ch));
        }
        return c;
    }
};

static DebugOutputBuf g_debugOutputBuf;
static std::ostream g_debugStream(&g_debugOutputBuf);
#else
// Linux/Mac 调试输出流缓冲区，将输出同时发送到 std::cout 和 Qt 调试器
class DebugOutputBuf : public std::streambuf {
public:
    DebugOutputBuf() : std::streambuf() {
        qDebug() << "=== DebugOutputBuf 构造函数调用 ===";
    }

protected:
    virtual std::streamsize xsputn(const char* s, std::streamsize count) override {
        if (s && count > 0) {
            std::cout.write(s, count);
            std::cout.flush();
            QString msg = QString::fromLocal8Bit(s, count);
            qDebug().noquote() << msg;
        }
        return count;
    }

    virtual int overflow(int c) override {
        if (c != EOF) {
            char ch = static_cast<char>(c);
            std::cout.put(ch);
            std::cout.flush();
            qDebug().noquote() << QString(QLatin1Char(ch));
        }
        return c;
    }
};

static DebugOutputBuf g_debugOutputBuf;
static std::ostream g_debugStream(&g_debugOutputBuf);
#endif

// 全局指针指向LogInfo界面实例，用于日志转发到UI
// 注意：需确保UI界面生命周期长于日志管理器
static LogInfo* g_logInfoWidget = nullptr;

/**
 * @brief 自定义UI日志Sink后端
 * @details 继承自Boost.Log的格式化Sink后端，用于捕获日志并转发到Qt界面
 *          支持跨线程安全的UI更新（通过Qt元对象系统）
 */
class UILogSinkBackend : public sinks::basic_formatted_sink_backend<char> {
public:
    /**
     * @brief 处理格式化后的日志记录
     * @param record 日志记录视图（包含级别、属性等信息）
     * @param formatted_log 已格式化的日志字符串（带时间戳、级别等）
     */
    void consume(const logging::record_view& record, const std::string& formatted_log) {

        if (!g_logInfoWidget) {
           return;
        }

        QString logStr = QString::fromUtf8(formatted_log.c_str());
        QMetaObject::invokeMethod(g_logInfoWidget, "addLog",
            Qt::QueuedConnection,
            Q_ARG(QString, logStr)
        );
    }
};

/**
 * @brief 设置UI日志显示界面的全局指针
 * @param widget LogInfo界面实例指针
 * @note 需在UI初始化后调用，确保日志能转发到界面
 */
void setLogInfoWidget(LogInfo* widget) {
    g_logInfoWidget = widget;
}

/**
 * @brief 重载std::ostream<<运算符，支持QString输出
 * @param os 输出流对象
 * @param str 待输出的QString
 * @return 输出流引用（支持链式调用）
 * @note 用于Boost.Log输出QString类型的日志内容
 */
std::ostream& operator<<(std::ostream& os, const QString& str) {
    return os << str.toUtf8().constData() << " ";
}



/**
 * @brief 重载std::ostream<<运算符，支持QStringList输出
 * @param os 输出流对象
 * @param list 待输出的QStringList
 * @return 输出流引用
 * @note 格式化输出为[元素1, 元素2, ...]形式，便于日志查看
 */
std::ostream& operator<<(std::ostream& os, const QStringList& list) {
    os << "[";
    for (int i = 0; i < list.size(); ++i) {
        if (i > 0) os << ", ";  // 元素间添加逗号分隔
        os << list[i];          // 复用QString的重载运算符
    }
    os << "]";
    return os;
}


std::ostream& operator<<(std::ostream& os, const QRect& str) {
    return os << str.x() << " " << str.y() << " " << str.width() << " " << str.height();  // 或使用toUtf8().constData()（支持中文）
}

std::ostream& operator<<(std::ostream& os, const QPoint& str) {
    return os << str.x() << " " << str.y();  // 或使用toUtf8().constData()（支持中文）
}



/**
 * @brief 字符串转布尔值（支持多种常见格式）
 * @param str 输入字符串（如"true"/"false"/"1"/"0"/"yes"/"no"）
 * @return 转换后的布尔值
 * @note 不区分大小写，默认返回false（转换失败时）
 */
bool string_to_bool(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower); // 转为小写统一判断
    return (s == "true" || s == "1" || s == "yes");
}

/**
 * @brief 字符串转日志级别（适配Boost.Log的trivial级别）
 * @param level_str 输入字符串（如"trace"/"debug"/"info"等）
 * @return 对应的SeverityLevel枚举值
 * @note 转换失败时默认返回info级别
 */
SeverityLevel string_to_level(const std::string& level_str) {
    std::string s = level_str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower); // 转为小写统一判断

    if (s == "trace") return boost::log::trivial::trace;
    else if (s == "debug") return boost::log::trivial::debug;
    else if (s == "info") return boost::log::trivial::info;
    else if (s == "warning") return boost::log::trivial::warning;
    else if (s == "error") return boost::log::trivial::error;
    else if (s == "fatal") return boost::log::trivial::fatal;
    else return boost::log::trivial::info; // 默认级别
}

/**
 * @brief 从INI配置文件加载日志配置
 * @param config_path 配置文件路径
 * @return 填充后的LogConfig结构体
 * @note 配置项缺失时使用默认值，加载失败时输出错误并返回默认配置
 */
LogConfig load_config(const std::string& config_path) {
    LogConfig config;  // 默认初始化配置结构体
    pt::ptree tree;    // 属性树对象，用于解析INI文件

    try {
        // 读取INI配置文件到属性树
        pt::read_ini(config_path, tree);

        // 逐行加载配置项，第二个参数为默认值
        config.mode = tree.get<std::string>("Logging.Mode", config.mode);
        config.outputType = tree.get<std::string>("Logging.OutputType", config.outputType);
        config.logDir = tree.get<std::string>("Logging.LogDir", config.logDir);
        config.fileName = tree.get<std::string>("Logging.FileName", config.fileName);
        config.debugFileName = tree.get<std::string>("Logging.DebugFileName", config.debugFileName);
        config.rotateByDate = string_to_bool(tree.get<std::string>("Logging.RotateByDate", "true"));
        config.autoFlush = string_to_bool(tree.get<std::string>("Logging.AutoFlush", "true"));
        config.maxSize = tree.get<int>("Logging.MaxSizeMB", config.maxSize);
        config.maxDays = tree.get<int>("Logging.MaxDays", config.maxDays);
        config.minLevel = string_to_level(tree.get<std::string>("Logging.MinLevel", "info"));
        // 新增：UI界面显示的最低日志级别（默认info）
        config.uiMinLevel = string_to_level(tree.get<std::string>("Logging.UIMinLevel", "info"));
        config.useAsyncSink = string_to_bool(tree.get<std::string>("Logging.UseAsyncSink", "false"));
    }
    catch (...) {
        // 捕获所有异常（文件不存在、格式错误等），使用默认配置
        std::cerr << "配置文件加载失败，使用默认配置" << std::endl;
    }

    return config;
}

/**
 * @brief 创建日志格式化器（定义日志输出格式）
 * @return Boost.Log格式化器对象
 * @note 格式为：[时间戳] [级别] 日志内容
 */
logging::formatter create_formatter() {
    return expr::stream
        << "[" << expr::format_date_time(timestamp_attr, "%Y-%m-%d %H:%M:%S") << "]"  // 时间戳（精确到秒）
        << "[" << severity_attr << "]"                                                // 日志级别
        << " " << expr::smessage;                                                     // 日志内容
}

void add_ui_sink() {
    if (!g_logInfoWidget) return;

    if (global_config.useAsyncSink) {
        typedef sinks::asynchronous_sink<UILogSinkBackend> ui_sink_t;
        auto sink = boost::make_shared<ui_sink_t>(boost::make_shared<UILogSinkBackend>());
        sink->set_formatter(create_formatter());
        sink->set_filter(expr::attr<SeverityLevel>("Severity") >= global_config.uiMinLevel);
        logging::core::get()->add_sink(sink);
    } else {
        typedef sinks::synchronous_sink<UILogSinkBackend> ui_sink_t;
        auto sink = boost::make_shared<ui_sink_t>(boost::make_shared<UILogSinkBackend>());
        sink->set_formatter(create_formatter());
        sink->set_filter(expr::attr<SeverityLevel>("Severity") >= global_config.uiMinLevel);
        logging::core::get()->add_sink(sink);
    }
}

/**
 * @brief 创建控制台输出Sink
 * @details 根据配置选择同步/异步Sink，支持Debug模式强制刷新
 *          输出日志到标准输出（std::cout）
 *          Windows下同时输出到调试器（Qt Creator应用程序输出窗口）
 */
void create_console_sink() {
    // 创建文本输出流后端（用于控制台输出）
    auto backend = boost::make_shared<sinks::text_ostream_backend>();
    
#ifdef _WIN32
    // Windows下使用自定义调试输出流，同时输出到控制台和调试器
    backend->add_stream(boost::shared_ptr<std::ostream>(&g_debugStream, boost::null_deleter()));
#else
    // Linux/Mac下使用标准输出
    backend->add_stream(boost::shared_ptr<std::ostream>(&std::cout, boost::null_deleter()));
#endif
    
    backend->auto_flush(global_config.autoFlush); // 根据配置设置自动刷新

    // --- Debug模式特殊配置 ---
#ifdef _DEBUG
    // Debug模式下强制立即刷新，确保日志实时输出（避免缓冲区延迟）
    backend->auto_flush(true);
#endif

    // 根据配置选择同步/异步Sink类型
    if (global_config.useAsyncSink) {
        // 异步Sink：日志事件放入队列异步处理，不阻塞主线程
        typedef sinks::asynchronous_sink<sinks::text_ostream_backend> sink_t;
        auto sink = boost::make_shared<sink_t>(backend);
        sink->set_formatter(create_formatter()); // 设置日志格式
        // 过滤：仅输出级别>=全局最低级别的日志
        sink->set_filter(expr::attr<SeverityLevel>("Severity") >= global_config.minLevel);
        logging::core::get()->add_sink(sink); // 将Sink添加到日志核心
    } else {
        // 同步Sink：日志事件立即处理，阻塞主线程但实时性高
        typedef sinks::synchronous_sink<sinks::text_ostream_backend> sink_t;
        auto sink = boost::make_shared<sink_t>(backend);
        sink->set_formatter(create_formatter());
        sink->set_filter(expr::attr<SeverityLevel>("Severity") >= global_config.minLevel);
        logging::core::get()->add_sink(sink);
    }
}

/**
 * @brief 创建文件输出Sink（支持按大小/日期轮转）
 * @param filename 日志文件名前缀（不含扩展名）
 * @details 自动创建日志目录，支持按日期（每日）或大小轮转日志文件
 */
void create_file_sink(const std::string& filename) {
    // 确保日志目录存在（不存在则创建）
    fs::create_directories(global_config.logDir);
    // 拼接日志文件路径（目录 + 文件名前缀）
    fs::path log_path = fs::path(global_config.logDir) / filename;

    // 构建日志文件模式（支持按日期轮转）
    std::string file_pattern = log_path.string();
    if (global_config.rotateByDate) {
        file_pattern += "_%Y%m%d"; // 添加日期后缀（如app_20240520.log）
    }
    file_pattern += ".log"; // 添加扩展名

    // 创建文件输出后端（支持轮转和自动刷新）
    auto backend = boost::make_shared<sinks::text_file_backend>(
        logging::keywords::file_name = file_pattern,          // 日志文件名模式
        logging::keywords::rotation_size = static_cast<uintmax_t>(global_config.maxSize) * 1024 * 1024, // 轮转大小（MB转字节）
        logging::keywords::auto_flush = global_config.autoFlush // 自动刷新
    );

    // 若配置按日期轮转，设置每日0点轮转
    if (global_config.rotateByDate) {
        backend->set_time_based_rotation(sinks::file::rotation_at_time_point(0, 0, 0));
    }

    // 根据配置选择同步/异步Sink类型
    if (global_config.useAsyncSink) {
        typedef sinks::asynchronous_sink<sinks::text_file_backend> sink_t;
        auto sink = boost::make_shared<sink_t>(backend);
        sink->set_formatter(create_formatter());
        sink->set_filter(expr::attr<SeverityLevel>("Severity") >= global_config.minLevel);
        logging::core::get()->add_sink(sink);
    } else {
        typedef sinks::synchronous_sink<sinks::text_file_backend> sink_t;
        auto sink = boost::make_shared<sink_t>(backend);
        sink->set_formatter(create_formatter());
        sink->set_filter(expr::attr<SeverityLevel>("Severity") >= global_config.minLevel);
        logging::core::get()->add_sink(sink);
    }
}

/**
 * @brief 初始化日志系统（从配置文件加载配置）
 * @param config_path 配置文件路径
 * @return 初始化是否成功（true/false）
 */
bool init_logging(const std::string& config_path) {
    try {
        global_config = load_config(config_path); // 加载配置到全局对象
        return init_logging(global_config);       // 调用重载函数初始化
    } catch (...) {
        return false; // 捕获异常，返回初始化失败
    }
}

/**
 * @brief 创建UI日志Sink（转发日志到Qt界面）
 * @details 根据配置选择同步/异步Sink，仅转发符合UI级别过滤的日志
 *          需确保g_logInfoWidget已初始化
 */
void create_ui_sink() {
    if (!g_logInfoWidget) return; // UI界面未初始化，跳过创建

    // 根据配置选择同步/异步Sink类型
    if (global_config.useAsyncSink) {
        // 异步UI Sink：避免日志处理阻塞UI线程
        typedef sinks::asynchronous_sink<UILogSinkBackend> ui_sink_t;
        auto sink = boost::make_shared<ui_sink_t>(boost::make_shared<UILogSinkBackend>());
        sink->set_formatter(create_formatter()); // 复用全局日志格式
        // 过滤：仅转发级别>=UI最低级别的日志（如配置info则包含info/warning/error/fatal）
        sink->set_filter(expr::attr<SeverityLevel>("Severity") >= global_config.uiMinLevel);
        logging::core::get()->add_sink(sink); // 添加到日志核心
    } else {
        // 同步UI Sink：实时性高但可能阻塞UI线程
        typedef sinks::synchronous_sink<UILogSinkBackend> ui_sink_t;
        auto sink = boost::make_shared<ui_sink_t>(boost::make_shared<UILogSinkBackend>());
        sink->set_formatter(create_formatter());
        sink->set_filter(expr::attr<SeverityLevel>("Severity") >= global_config.uiMinLevel);
        logging::core::get()->add_sink(sink);
    }
}

/**
 * @brief 初始化日志系统（使用已加载的配置）
 * @param config 日志配置结构体
 * @return 初始化是否成功
 * @details 核心初始化函数：配置日志核心、创建控制台/文件/UI Sink
 */
bool init_logging(const LogConfig& config) {
    try {
        global_config = config; // 更新全局配置

#ifdef _WIN32
        // 测试 OutputDebugString 是否正常工作
        OutputDebugStringA("=== 测试 OutputDebugString ===\n");
#endif

        // 若配置为"none"，禁用日志输出
        if (config.mode == "none") {
            logging::core::get()->set_logging_enabled(false);
            return true;
        }

        // 启用日志输出，并添加通用属性（时间戳、级别等）
        logging::core::get()->set_logging_enabled(true);
        logging::add_common_attributes();

        // 设置全局本地化（支持中文输出，区分Windows/Linux）
#ifdef _WIN32
        std::locale::global(std::locale(".UTF8"));  // Windows系统UTF-8本地化
#else
        std::locale::global(std::locale("en_US.UTF-8"));  // Linux/Mac系统
#endif

        // 根据运行模式（debug/release）创建不同Sink
        if (config.mode == "debug") {
            // Debug模式：支持控制台+文件输出
            if (config.outputType == "console" || config.outputType == "both") {
                create_console_sink(); // 创建控制台Sink
            }
            if (config.outputType == "file" || config.outputType == "both") {
                create_file_sink(config.debugFileName); // 创建Debug文件Sink
            }
        } else if (config.mode == "release") {
            // Release模式：仅控制台或仅文件输出
            if (config.outputType == "console") {
                create_console_sink();
            } else {
                create_file_sink(config.fileName); // 创建Release文件Sink
            }
        }

        // 创建UI日志Sink（转发日志到Qt界面）
        create_ui_sink();

        // 不再设置全局日志级别过滤，避免覆盖 Sink 的独立过滤器
        // 每个Sink已经设置了自己的过滤器
        return true;
    } catch (const std::exception& e) {
        // 捕获并输出初始化异常信息
        std::cerr << "日志初始化失败: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 动态设置全局日志级别
 * @param level 新的最低日志级别
 * @note 会影响所有Sink的日志输出（控制台/文件/UI）
 */
void set_log_level(SeverityLevel level) {
    global_config.minLevel = level; // 更新全局配置
    // 设置日志核心的全局过滤条件
    logging::core::get()->set_filter(expr::attr<SeverityLevel>("Severity") >= level);
}

/**
 * @brief 动态设置UI界面显示的日志级别
 * @param level 新的UI最低日志级别
 * @note 仅影响UI Sink的日志转发，不影响控制台/文件输出
 */
void set_ui_log_level(SeverityLevel level) {
    global_config.uiMinLevel = level; // 更新全局配置中的UI级别
}

/**
 * @brief 关闭日志系统（清理资源）
 * @details 移除所有Sink，禁用日志输出，确保资源正确释放
 */
void shutdown_logging() {
    logging::core::get()->remove_all_sinks(); // 移除所有Sink（释放资源）
    logging::core::get()->set_logging_enabled(false); // 禁用日志输出
}
