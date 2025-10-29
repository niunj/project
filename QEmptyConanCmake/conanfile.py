from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout

class VSGConan(ConanFile):
    name = "vsg"
    version = "0.1.0"
    
    # 生成器设置
    generators = "CMakeDeps", "CMakeToolchain"
    
    # 依赖项
    requires = "spdlog/1.14.1"
    
    # 构建设置
    settings = "os", "compiler", "build_type", "arch"
    
    def layout(self):
        cmake_layout(self)
    
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()