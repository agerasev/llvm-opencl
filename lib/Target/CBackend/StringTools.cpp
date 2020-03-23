#include "StringTools.h"

namespace llvm_cbe {
  std::string CBEMangle(const std::string &S) {
    std::string Result;

    for (auto c : S) {
      if (isalnum(c) || c == '_') {
        Result += c;
      } else if(c == '.') {
        Result += '_';  
      } else {
        Result += '_';
        Result += 'A' + (c & 15);
        Result += 'A' + ((c >> 4) & 15);
        Result += '_';
      }
    }

    return Result;
  }

  void replace(std::string &str, const std::string &from, const std::string &to) {
    size_t pos = 0;
    for (;;) {
      pos = str.find(from, pos);
      if (pos == std::string::npos) {
        break;
      }
      str.replace(pos, from.size(), to);
      pos += to.size();
    }
  }

  std::vector<std::string> split(const std::string &str, const std::string &sep) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < str.size()) {
      size_t new_pos = str.find(sep, pos);
      if (new_pos == std::string::npos) {
        new_pos = str.size();
      }
      out.push_back(str.substr(pos, new_pos - pos));
      pos = new_pos + sep.size();
    }
    return out;
  }
}
