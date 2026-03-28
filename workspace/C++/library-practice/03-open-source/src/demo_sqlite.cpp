// ============================================================
// demo_sqlite.cpp — 使用 SQLite3 C 接口
//
// SQLite 是世界上使用最广泛的嵌入式数据库，纯 C 接口经典动态库
// 安装方式：brew install sqlite3
// 文档：    https://sqlite.org/c3ref/intro.html
//
// 编译（手动）：
//   g++ -std=c++17 src/demo_sqlite.cpp \
//       -I/usr/local/include \
//       -L/usr/local/lib -lsqlite3 \
//       -o build/demo_sqlite
//   DYLD_LIBRARY_PATH=/usr/local/lib ./build/demo_sqlite
//
// 或者：make sqlite
// ============================================================

#include <sqlite3.h>
#include <iostream>
#include <string>

// sqlite3 回调函数：每行数据调用一次
// 参数说明：
//   data    → 用户传入的自定义数据（这里不用，传 nullptr）
//   argc    → 列数
//   argv    → 每列的值（字符串形式）
//   col_name → 每列的列名
static int callback(void* /*data*/, int argc, char** argv, char** col_name) {
    for (int i = 0; i < argc; ++i) {
        std::cout << "  " << col_name[i] << ": "
                  << (argv[i] ? argv[i] : "NULL") << "\n";
    }
    std::cout << "\n";
    return 0;
}

// 封装执行 SQL 并打印错误
bool exec_sql(sqlite3* db, const std::string& sql, const std::string& desc = "") {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), callback, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL 错误 [" << desc << "]: " << err_msg << "\n";
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

int main() {
    std::cout << "========== SQLite3 动态库演示 ==========\n\n";
    std::cout << "SQLite 版本: " << sqlite3_libversion() << "\n\n";

    // ----------------------------------------------------------
    // 1. 打开内存数据库（":memory:" 表示不写磁盘，进程退出即消失）
    // ----------------------------------------------------------
    sqlite3* db = nullptr;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "无法打开数据库: " << sqlite3_errmsg(db) << "\n";
        return 1;
    }
    std::cout << "[1] 内存数据库已打开\n\n";

    // ----------------------------------------------------------
    // 2. 建表
    // ----------------------------------------------------------
    const char* create_sql = R"(
        CREATE TABLE IF NOT EXISTS books (
            id      INTEGER PRIMARY KEY AUTOINCREMENT,
            title   TEXT    NOT NULL,
            author  TEXT    NOT NULL,
            year    INTEGER,
            rating  REAL
        );
    )";
    exec_sql(db, create_sql, "CREATE TABLE");
    std::cout << "[2] 表 books 创建成功\n\n";

    // ----------------------------------------------------------
    // 3. 插入数据（使用 Prepared Statement，防止 SQL 注入）
    // ----------------------------------------------------------
    const char* insert_sql =
        "INSERT INTO books (title, author, year, rating) VALUES (?, ?, ?, ?)";

    struct BookData { const char* title; const char* author; int year; double rating; };
    BookData books[] = {
        {"The C Programming Language", "Kernighan & Ritchie", 1978, 4.9},
        {"Effective C++",              "Scott Meyers",        1992, 4.7},
        {"Clean Code",                 "Robert Martin",       2008, 4.2},
        {"The Pragmatic Programmer",   "Hunt & Thomas",       1999, 4.6},
    };

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr);

    for (const auto& b : books) {
        sqlite3_bind_text(stmt, 1, b.title,  -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, b.author, -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 3, b.year);
        sqlite3_bind_double(stmt, 4, b.rating);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);  // 重置以便复用
    }
    sqlite3_finalize(stmt);
    std::cout << "[3] 插入 " << sizeof(books)/sizeof(books[0]) << " 条数据\n\n";

    // ----------------------------------------------------------
    // 4. 查询全部数据
    // ----------------------------------------------------------
    std::cout << "[4] 查询所有书籍：\n";
    exec_sql(db, "SELECT * FROM books ORDER BY rating DESC;", "SELECT ALL");

    // ----------------------------------------------------------
    // 5. 带条件查询（rating > 4.5）
    // ----------------------------------------------------------
    std::cout << "[5] 高分书籍（rating > 4.5）：\n";
    exec_sql(db, "SELECT title, rating FROM books WHERE rating > 4.5;", "SELECT WHERE");

    // ----------------------------------------------------------
    // 6. 聚合统计
    // ----------------------------------------------------------
    std::cout << "[6] 统计：\n";
    exec_sql(db, "SELECT COUNT(*) AS total, AVG(rating) AS avg_rating FROM books;", "AGGREGATE");

    // ----------------------------------------------------------
    // 7. 关闭数据库
    // ----------------------------------------------------------
    sqlite3_close(db);
    std::cout << "[7] 数据库已关闭\n";

    return 0;
}
