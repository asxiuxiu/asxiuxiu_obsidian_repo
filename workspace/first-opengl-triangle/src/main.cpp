// 第一个三角形：OpenGL 4.6 Core Profile 练习
// 源自笔记：Notes/计算机图形学/GPU编程基础/第一个三角形.md

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

// 窗口大小变化回调
void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    (void)window; // 当前未使用，仅消除编译警告
    glViewport(0, 0, width, height);
}

// 编译单个 Shader 的辅助函数
unsigned int compileShader(const char *source, GLenum type)
{
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cout << "Shader 编译失败:\n" << infoLog << std::endl;
    }
    return shader;
}

int main()
{
    // ===== 1. 初始化窗口 =====
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(800, 600, "第一个三角形", NULL, NULL);
    if (!window)
    {
        std::cout << "创建窗口失败" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // 加载 OpenGL 函数指针
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "GLAD 加载失败" << std::endl;
        return -1;
    }

    // ===== 2. 定义顶点数据 =====
    float vertices[] = {
        // 位置              // 颜色
        -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, // 左下 红
        0.5f,  -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, // 右下 绿
        0.0f,  0.5f,  0.0f, 0.0f, 0.0f, 1.0f  // 顶部 蓝
    };

    // ===== 3. 创建 VAO、VBO =====
    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO); // 先绑 VAO，开始记录配置

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // 属性 0：位置
    // glVertexAttribPointer 告诉 OpenGL 如何解析 VBO 中的顶点数据：
    //   参数 0 (index)      : 顶点属性编号，对应 shader 里 `layout (location = 0)`
    //   参数 1 (size)       : 每个顶点位置由几个分量组成，这里是 3 (x, y, z)
    //   参数 2 (type)       : 每个分量的数据类型，GL_FLOAT 表示 32 位浮点数
    //   参数 3 (normalized) : 是否把整数归一化到 [0,1] 或 [-1,1]；浮点数据通常填 GL_FALSE
    //   参数 4 (stride)     : 相邻两个顶点之间的字节间隔；每个顶点 6 个 float，所以是 6 * sizeof(float)
    //   参数 5 (pointer)    : 当前属性在单个顶点内的字节偏移；位置从顶点开头开始，所以是 0
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    // 属性 1：颜色
    //   index=1 对应 shader 里 `layout (location = 1) in vec3 aColor`
    //   颜色同样是 3 个 float (r, g, b)
    //   stride 仍然是 6 * sizeof(float)，因为颜色与位置在同一个 VBO 里交错排列
    //   pointer=3 * sizeof(float)，表示颜色数据在每个顶点的第 4 个 float 开始
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0); // 解绑 VAO

    // ===== 4. 编译 Shader =====
    const char *vs = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
out vec3 vColor;
void main() {
    gl_Position = vec4(aPos, 1.0);
    vColor = aColor;
})";

    const char *fs = R"(#version 330 core
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
    while (!glfwWindowShouldClose(window))
    {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);

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
