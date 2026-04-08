#pragma once
#include <string>
#include <vector>

// ============================================================
// libmystr — 自写动态库练习
// 提供字符串工具函数
// ============================================================

namespace mystr {

// 将字符串按分隔符切分
std::vector<std::string> split(const std::string& s, char delim);

// 去除首尾空白字符
std::string trim(const std::string& s);

// 全部转大写
std::string to_upper(std::string s);

// 全部转小写
std::string to_lower(std::string s);

// 判断是否以某前缀开头
bool starts_with(const std::string& s, const std::string& prefix);

// 判断是否以某后缀结尾
bool ends_with(const std::string& s, const std::string& suffix);

// 替换所有出现的子串
std::string replace_all(std::string s,
                        const std::string& from,
                        const std::string& to);

// 重复字符串 n 次
std::string repeat(const std::string& s, int n);

} // namespace mystr
