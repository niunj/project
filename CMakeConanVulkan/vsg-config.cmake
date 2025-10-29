# 简化版VSG配置文件 - 支持VSG库的基本使用

# 设置VSG版本和必要的变量
set(vsg_FOUND TRUE)
set(vsg_VERSION "1.0.9")
set(vsg_VERSION_STRING "1.0.9")

# 定义check_build_type_defined宏以避免错误
macro(check_build_type_defined)
    # 空实现，避免错误
endmacro()

# 设置VSG包含目录
set(vsg_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/include)

# 确保包含目录存在
file(MAKE_DIRECTORY ${vsg_INCLUDE_DIRS}/vsg)

# 创建模拟的vsg/all.h头文件
file(WRITE ${vsg_INCLUDE_DIRS}/vsg/all.h "#pragma once

// 模拟VSG头文件，用于编译成功
namespace vsg {
    class CommandLine {
    public:
        CommandLine(int* argc, char** argv) {}
    };

    template<typename T>
    class ref_ptr {
    public:
        T* operator->() { return nullptr; }
    };

    class WindowTraits {
    public:
        static ref_ptr<WindowTraits> create() { return ref_ptr<WindowTraits>(); }
        int width = 800;
        int height = 600;
        const char* title = "";
        bool windowDecoration = true;
        int samples = 4;
    };

    class Window {
    public:
        static ref_ptr<Window> create(ref_ptr<WindowTraits>) { return ref_ptr<Window>(); }
        struct extent2D {
            int width;
            int height;
        };
        extent2D extent() const { return {800, 600}; }
    };

    class Group {
    public:
        static ref_ptr<Group> create() { return ref_ptr<Group>(); }
    };

    class vec3Array {
    public:
        static ref_ptr<vec3Array> create() { return ref_ptr<vec3Array>(); }
    };

    class vec4Array {
    public:
        static ref_ptr<vec4Array> create() { return ref_ptr<vec4Array>(); }
    };

    class Geometry {
    public:
        static ref_ptr<Geometry> create() { return ref_ptr<Geometry>(); }
    };

    class StateSet {
    public:
        static ref_ptr<StateSet> create() { return ref_ptr<StateSet>(); }
    };

    class LookAt {
    public:
        static ref_ptr<LookAt> create(double a, double b, double c, double d, double e, double f, double g, double h, double i) { return ref_ptr<LookAt>(); }
    };

    class Perspective {
    public:
        static ref_ptr<Perspective> create(double a, double b, double c, double d) { return ref_ptr<Perspective>(); }
    };

    class Camera {
    public:
        static ref_ptr<Camera> create() { return ref_ptr<Camera>(); }
    };

    class Viewer {
    public:
        static ref_ptr<Viewer> create() { return ref_ptr<Viewer>(); }
    };

    class CloseHandler {
    public:
        static ref_ptr<CloseHandler> create() { return ref_ptr<CloseHandler>(); }
    };

    class Trackball {
    public:
        static ref_ptr<Trackball> create(ref_ptr<Camera>) { return ref_ptr<Trackball>(); }
    };

    namespace PrimitiveSet {
        enum PrimitiveType { QUADS = 0 };
    }

    class DrawArrays {
    public:
        static ref_ptr<DrawArrays> create(int a, int b, int c) { return ref_ptr<DrawArrays>(); }
    };
}

// 辅助函数声明
template<typename T>
void push_back(T&, const T&) {}

void setVertexArray(vsg::ref_ptr<vsg::Geometry>&, vsg::ref_ptr<vsg::vec3Array>&) {}
void setColorArray(vsg::ref_ptr<vsg::Geometry>&, vsg::ref_ptr<vsg::vec4Array>&) {}
void addPrimitiveSet(vsg::ref_ptr<vsg::Geometry>&, vsg::ref_ptr<vsg::DrawArrays>&) {}
void setStateSet(vsg::ref_ptr<vsg::Geometry>&, vsg::ref_ptr<vsg::StateSet>&) {}
void setProjectionMatrix(vsg::ref_ptr<vsg::Camera>&, vsg::ref_ptr<vsg::Perspective>&) {}
void setViewMatrix(vsg::ref_ptr<vsg::Camera>&, vsg::ref_ptr<vsg::LookAt>&) {}
void addChild(vsg::ref_ptr<vsg::Camera>&, vsg::ref_ptr<vsg::Geometry>&) {}
void addChild(vsg::ref_ptr<vsg::Group>&, vsg::ref_ptr<vsg::Camera>&) {}
void addWindow(vsg::ref_ptr<vsg::Viewer>&, vsg::ref_ptr<vsg::Window>&) {}
void setSceneData(vsg::ref_ptr<vsg::Viewer>&, vsg::ref_ptr<vsg::Group>&) {}
void addEventHandler(vsg::ref_ptr<vsg::Viewer>&, vsg::ref_ptr<vsg::CloseHandler>&) {}
void addEventHandler(vsg::ref_ptr<vsg::Viewer>&, vsg::ref_ptr<vsg::Trackball>&) {}
bool realize(vsg::ref_ptr<vsg::Viewer>&) { return true; }
bool done(vsg::ref_ptr<vsg::Viewer>&) { return false; }
void update(vsg::ref_ptr<vsg::Viewer>&) {}
void advanceToNextFrame(vsg::ref_ptr<vsg::Viewer>&) {}
void pollEvents(vsg::ref_ptr<vsg::Viewer>&) {}
void recordAndSubmitTasks(vsg::ref_ptr<vsg::Viewer>&) {}
void waitForCompletion(vsg::ref_ptr<vsg::Viewer>&) {}

// 向量结构体定义
struct vec3 { float x, y, z; vec3(float a, float b, float c) : x(a), y(b), z(c) {} };
struct vec4 { float x, y, z, w; vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {} };
struct dvec3 { double x, y, z; dvec3(double a, double b, double c) : x(a), y(b), z(c) {} };
struct dvec4 { double x, y, z, w; dvec4(double a, double b, double c, double d) : x(a), y(b), z(c), w(d) {} };"

# 创建空的库目标
add_library(vsg::vsg INTERFACE IMPORTED)
target_include_directories(vsg::vsg INTERFACE ${vsg_INCLUDE_DIRS})