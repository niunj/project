#include <iostream>
#include <GLFW/glfw3.h>

// 错误回调函数
static void error_callback(int error, const char* description) {
    std::cerr << "Error: " << description << std::endl;
}

// 窗口大小变化回调
static void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// 渲染函数
void render() {
    // 设置清除颜色为蓝色
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // 绘制一个简单的三角形
    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.0f, 0.0f); // 红色
    glVertex3f(-0.5f, -0.5f, 0.0f);
    glColor3f(0.0f, 1.0f, 0.0f); // 绿色
    glVertex3f(0.5f, -0.5f, 0.0f);
    glColor3f(0.0f, 0.0f, 1.0f); // 蓝色
    glVertex3f(0.0f, 0.5f, 0.0f);
    glEnd();
}

int main() {
    std::cout << "ConanTest project with GLFW and OpenGL" << std::endl;
    
    // 初始化GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    
    // 设置错误回调
    glfwSetErrorCallback(error_callback);
    
    // 创建窗口
    GLFWwindow* window = glfwCreateWindow(800, 600, "GLFW OpenGL Example", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    
    // 设置为当前上下文
    glfwMakeContextCurrent(window);
    
    // 设置窗口大小变化回调
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    
    // 打印OpenGL版本信息
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    
    // 主循环
    while (!glfwWindowShouldClose(window)) {
        // 渲染
        render();
        
        // 交换缓冲区并轮询事件
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    // 清理
    glfwDestroyWindow(window);
    glfwTerminate();
    
    return 0;
}