CXX      := g++
CXXFLAGS := -std=c++17 -Wall

# Homebrew 在 Intel Mac 用 /usr/local，Apple Silicon 用 /opt/homebrew
BREW_PREFIX := $(shell brew --prefix)
INC  := -I$(BREW_PREFIX)/include
LIBS := -L$(BREW_PREFIX)/lib

FMT_BIN    := build/demo_fmt
SQLITE_BIN := build/demo_sqlite

.PHONY: all fmt sqlite run clean

all: fmt sqlite
	@echo ""
	@echo "✅ 全部构建完成"
	@echo "   运行 fmt:    DYLD_LIBRARY_PATH=$(BREW_PREFIX)/lib ./$(FMT_BIN)"
	@echo "   运行 sqlite: DYLD_LIBRARY_PATH=$(BREW_PREFIX)/lib ./$(SQLITE_BIN)"

# --- fmt ---
fmt: $(FMT_BIN)

$(FMT_BIN): src/demo_fmt.cpp | build
	$(CXX) $(CXXFLAGS) $(INC) $< $(LIBS) -lfmt -o $@
	@echo "📦 fmt demo 已构建：$@"

# --- sqlite3 ---
sqlite: $(SQLITE_BIN)

$(SQLITE_BIN): src/demo_sqlite.cpp | build
	$(CXX) $(CXXFLAGS) $(INC) $< $(LIBS) -lsqlite3 -o $@
	@echo "📦 sqlite demo 已构建：$@"

# 一键构建并运行
run: all
	@echo "\n========== 运行 fmt demo ==========\n"
	DYLD_LIBRARY_PATH=$(BREW_PREFIX)/lib ./$(FMT_BIN)
	@echo "\n========== 运行 sqlite demo ==========\n"
	DYLD_LIBRARY_PATH=$(BREW_PREFIX)/lib ./$(SQLITE_BIN)

build:
	mkdir -p build

clean:
	rm -rf build
