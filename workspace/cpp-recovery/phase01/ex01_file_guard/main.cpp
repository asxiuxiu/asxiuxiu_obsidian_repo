#include <cstdio>
#include <cstdlib>
#include <cstring>

// 练习 1.1：手写 RAII 文件句柄类 FileGuard
// 要求：封装 FILE*，构造时打开文件，析构时关闭文件，支持查询是否打开。

namespace cpp_recovery::phase01 {

class FileGuard {
public:
    explicit FileGuard(const char* path, const char* mode);
    ~FileGuard();

    bool IsOpen() const;
    FILE* Get() const;

    // TODO: 考虑是否允许拷贝/移动

private:
    FILE* file_;
};

FileGuard::FileGuard(const char* path, const char* mode)
    : file_(nullptr)
{
    // TODO: 在此实现构造函数
    (void)path;
    (void)mode;
}

FileGuard::~FileGuard()
{
    // TODO: 在此实现析构函数
}

bool FileGuard::IsOpen() const
{
    // TODO: 在此实现 IsOpen
    return false;
}

FILE* FileGuard::Get() const
{
    // TODO: 在此实现 Get
    return file_;
}

} // namespace cpp_recovery::phase01

// ---------------------------------------------------------------------------
// 测试用例
// ---------------------------------------------------------------------------

static int tests_run = 0;
static int tests_passed = 0;

#define CHECK(cond)                                              \
    do {                                                         \
        ++tests_run;                                             \
        if (!(cond)) {                                           \
            std::printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                            \
        }                                                        \
        ++tests_passed;                                          \
    } while (0)

int test_open_existing_file()
{
    const char* path = "test_input.txt";
    std::FILE* raw = std::fopen(path, "w");
    CHECK(raw != nullptr);
    std::fputs("hello", raw);
    std::fclose(raw);

    {
        cpp_recovery::phase01::FileGuard guard(path, "r");
        CHECK(guard.IsOpen());
        CHECK(guard.Get() != nullptr);
    }

    std::remove(path);
    return 0;
}

int test_open_missing_file()
{
    cpp_recovery::phase01::FileGuard guard("this_file_should_not_exist.txt", "r");
    CHECK(!guard.IsOpen());
    CHECK(guard.Get() == nullptr);
    return 0;
}

int test_scope_closes_file()
{
    const char* path = "test_scope.txt";
    {
        cpp_recovery::phase01::FileGuard guard(path, "w");
        CHECK(guard.IsOpen());
    }

    // 析构后文件应已关闭且可删除
    CHECK(std::remove(path) == 0);
    return 0;
}

int test_scope_with_exception()
{
    const char* path = "test_exception.txt";
    try {
        cpp_recovery::phase01::FileGuard guard(path, "w");
        CHECK(guard.IsOpen());
        throw 1;
    } catch (...) {
        // guard 应在异常抛出时正确析构
    }

    CHECK(std::remove(path) == 0);
    return 0;
}

int main()
{
    int failures = 0;
    failures += test_open_existing_file();
    failures += test_open_missing_file();
    failures += test_scope_closes_file();
    failures += test_scope_with_exception();

    std::printf("%d/%d tests passed\n", tests_passed, tests_run);
    return failures;
}
