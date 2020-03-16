#ifndef CBUILTINS_H
#define CBUILTINS_H

#include <string>
#include <unordered_set>

namespace llvm {

  class CBuiltIns {
  private:
    // FIXME: const char *
    std::unordered_set<std::string> set;
  public:
    CBuiltIns();
    bool isBuiltIn(const char *func_name_and_sig) const;
  };

} // namespace llvm

#endif