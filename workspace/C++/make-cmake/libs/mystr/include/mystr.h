#pragma once
#include <string>

namespace mystr {

// 将字符串转为大写
std::string to_upper(const std::string& s);

// 将字符串转为小写
std::string to_lower(const std::string& s);

// 统计字符 c 在字符串 s 中出现的次数
int count_char(const std::string& s, char c);

// 反转字符串
std::string reverse_str(const std::string& s);

} // namespace mystr
