CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Iinclude

# 动态库名：macOS 用 .dylib，Linux 用 .so
UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
    LIB     := build/libmystr.dylib
    RUNENV  := DYLD_LIBRARY_PATH=build
else
    LIB     := build/libmystr.so
    RUNENV  := LD_LIBRARY_PATH=build
endif

BIN  := build/demo
SRC  := src/mystr.cpp
OBJ  := build/mystr.o

.PHONY: all run clean

all: $(BIN)
	@echo ""
	@echo "✅ 构建完成，运行：$(RUNENV) ./$(BIN)"

# 链接最终程序，依赖动态库
$(BIN): main.cpp $(LIB)
	$(CXX) $(CXXFLAGS) main.cpp -Lbuild -lmystr -o $@

# 生成动态库（-fPIC 是关键）
$(LIB): $(OBJ) | build
	$(CXX) -shared $^ -o $@
	@echo "📦 动态库已生成：$@"

# 编译目标文件（-fPIC：位置无关代码）
$(OBJ): $(SRC) | build
	$(CXX) $(CXXFLAGS) -fPIC -c $< -o $@

# 一键构建并运行
run: all
	$(RUNENV) ./$(BIN)

build:
	mkdir -p build

clean:
	rm -rf build
