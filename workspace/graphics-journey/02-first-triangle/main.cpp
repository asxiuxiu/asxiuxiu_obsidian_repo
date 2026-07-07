// Graphics Journey - 阶段二：第一个三角形
// 完整可运行代码（OpenGL 4.6 Core Profile）
//
// 依赖：GLFW + GLAD
// 编译示例（Git Bash / MSYS2）：
//   g++ main.cpp -std=c++17 -I/path/to/glad/include -I/path/to/glfw/include \
//       glad.c -lglfw3 -lopengl32 -lgdi32 -o first-triangle

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

// 窗口大小调整回调
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// 编译 Shader 的辅助函数
unsigned int compileShader(const char* source, GLenum type) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cout << "Shader 编译失败:\n" << infoLog << std::endl;
    }
    return shader;
}

int main() {
    // ===== 1. 初始化窗口 =====
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(800, 600, "第一个三角形", NULL, NULL);
    if (!window) {
        std::cout << "创建窗口失败" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "GLAD 加载失败" << std::endl;
        return -1;
    }

    // ===== 2. 定义顶点数据 =====
    float vertices[] = {
        // 位置              // 颜色
        -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,  // 左下 红
         0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,  // 右下 绿
         0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f   // 顶部 蓝
    };

    // ===== 3. 创建 VAO、VBO =====
    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);  // 绑定 VAO，开始记录配置

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // 属性 0：位置
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // 属性 1：颜色
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);  // 解绑 VAO

    // ===== 4. 编译 Shader =====
    const char* vs = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
out vec3 vColor;
void main() {
    gl_Position = vec4(aPos, 1.0);
    vColor = aColor;
})";

    const char* fs = R"(#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
})";

    unsigned int shaderProgram = glCreateProgram();
    unsigned int vsObj = compileShader(vs, GL_VERTEX_SHADER);
    unsigned int fsObj = compileShader(fs, GL_FRAGMENT_SHADER);
    glAttachShader(shaderProgram, vsObj);
    glAttachShader(shaderProgram, fsObj);
    glLinkProgram(shaderProgram);
    glDeleteShader(vsObj);
    glDeleteShader(fsObj);

    // ===== 5. 渲染循环 =====
    while (!glfwWindowShouldClose(window)) {
        // 输入
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // 渲染
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // 交换缓冲
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ===== 6. 清理 =====
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);
    glfwTerminate();
    return 0;
}
