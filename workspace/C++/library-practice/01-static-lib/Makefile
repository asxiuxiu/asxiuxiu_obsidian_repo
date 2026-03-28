CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Iinclude

SRC      := src/power.cpp src/gcd_lcm.cpp src/prime.cpp src/fibonacci.cpp
OBJ      := $(patsubst src/%.cpp, build/%.o, $(SRC))
LIB      := build/libmymath.a
BIN      := build/demo

.PHONY: all clean

all: $(BIN)
	@echo ""
	@echo "✅ 构建完成，运行：./$(BIN)"

# 链接：main.cpp + 静态库
$(BIN): main.cpp $(LIB)
	$(CXX) $(CXXFLAGS) main.cpp -Lbuild -lmymath -o $@

# 打包静态库
$(LIB): $(OBJ)
	ar rcs $@ $^
	@echo "📦 静态库已生成：$@"

# 编译各目标文件
build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build
