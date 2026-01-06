#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <string>
#include <iostream>

// 1. 先定义运算符重载（建议放在公共头文件）
#include <ostream>
#include <QString>
#include <QStringList>



// 仅声明，不定义
std::ostream& operator<<(std::ostream& os, const QString& str);
std::ostream& operator<<(std::ostream& os, const QStringList& list);

// 定义日志级别类型
using SeverityLevel = boost::log::trivial::severity_level;

// 日志配置结构体
struct LogConfig {
    std::string mode = "release";
    std::string outputType = "file";
    std::string logDir = "./logs";
    std::string fileName = "app.log";
    std::string debugFileName = "debug.log";
    bool rotateByDate = true;
    bool autoFlush = true;
    int maxSize = 10;
    int maxDays = 7;
    SeverityLevel minLevel = boost::log::trivial::info;
    SeverityLevel uiMinLevel = boost::log::trivial::info;     // 界面显示的最低级别（新增）
    bool useAsyncSink = false;
};

// 定义属性关键字
BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp_attr, "TimeStamp", boost::posix_time::ptime)
BOOST_LOG_ATTRIBUTE_KEYWORD(severity_attr, "Severity", SeverityLevel)
BOOST_LOG_ATTRIBUTE_KEYWORD(thread_id_attr, "ThreadID", boost::log::attributes::current_thread_id::value_type)

// 极简日志宏（兼容所有Boost版本）
#define LOG_TRACE   BOOST_LOG_TRIVIAL(trace)
#define LOG_DEBUG   BOOST_LOG_TRIVIAL(debug)
#define LOG_INFO    BOOST_LOG_TRIVIAL(info)
#define LOG_WARNING BOOST_LOG_TRIVIAL(warning)
#define LOG_ERROR   BOOST_LOG_TRIVIAL(error)
#define LOG_FATAL   BOOST_LOG_TRIVIAL(fatal)


// log_manager.h 中添加
class LogInfo; // 前向声明

// 全局接口：设置日志显示界面
void setLogInfoWidget(LogInfo* widget);

// 初始化日志系统
bool init_logging(const std::string& config_path = "./log_config.ini");
bool init_logging(const LogConfig& config);

// 设置日志级别
void set_log_level(SeverityLevel level);
// log_manager.h 中添加接口
void set_ui_log_level(SeverityLevel level);

// 关闭日志系统
void shutdown_logging();

#endif // LOG_MANAGER_H
