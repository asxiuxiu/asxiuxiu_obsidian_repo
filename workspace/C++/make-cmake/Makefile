# From note: C++/构建系统：make 与 CMake
# 变量定义
CXX    = g++
CFLAGS = -Wall -O2 -std=c++17 -Iinclude
TARGET = my_app
SRCS   = src/main.cpp src/utils.cpp src/math.cpp
OBJS   = $(SRCS:.cpp=.o)   # 字符串替换：将 .cpp 替换为 .o

# 默认目标
all: $(TARGET)

# 使用变量链接
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET)

# 模式规则：所有 .cpp → .o 的通用规则
%.o: %.cpp
	$(CXX) $(CFLAGS) -c $< -o $@
#                         ↑    ↑
#                    $< = 依赖的第一个文件（源文件）
#                    $@ = 目标文件名

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
