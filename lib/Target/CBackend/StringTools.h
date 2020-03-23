#pragma once

#include <vector>
#include <string>

namespace llvm_cbe {
  class OutModifier {
  public:
    size_t last_size;
    std::string &out;
    OutModifier(std::string &out);
    std::string cut_tail();
  };

  std::string CBEMangle(const std::string &S);
  void replace(std::string &str, const std::string &from, const std::string &to);
  std::vector<std::string> split(const std::string &str, const std::string &sep);
}
