#include "mystr.h"
#include <algorithm>
#include <cctype>

namespace mystr {

std::string to_upper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

int count_char(const std::string& s, char c) {
    return static_cast<int>(std::count(s.begin(), s.end(), c));
}

std::string reverse_str(const std::string& s) {
    return std::string(s.rbegin(), s.rend());
}

} // namespace mystr
