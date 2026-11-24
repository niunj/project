#pragma once
/// @file IAlgoBase.h
/// @brief 算法模块公共基类接口。

#include <string>

namespace vsgraysim::algo {

/// 算法模块公共基类
class IAlgoBase {
public:
    virtual ~IAlgoBase() = default;

    /// 初始化算法 (加载参数、分配资源)
    virtual bool init() = 0;

    /// 重置到初始状态
    virtual void reset() = 0;

    /// 返回算法唯一标识名称
    virtual std::string name() const = 0;
};

} // namespace vsgraysim::algo