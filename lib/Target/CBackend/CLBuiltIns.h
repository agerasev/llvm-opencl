#ifndef CLBUILTINS_H
#define CLBUILTINS_H

#include <string>
#include <set>
#include <initializer_list>
#include <functional>

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Value.h"


namespace llvm_cbe {
  using namespace llvm;

  class Func {
  public:
    std::string name;
    std::string ret;
    std::vector<std::string> args;
    Func();
    Func(const std::string &ret, const std::string &name, const std::initializer_list<std::string> &args);
    Func(const std::string &signature);
    std::string to_string(bool with_ret=true);
    bool operator<(const Func &other) const;
  };

  class CLBuiltIns {
  private:
    std::set<Func> set;
    int add_functions(const std::initializer_list<Func> &list);
  public:
    CLBuiltIns();
    static int demangle(const char *mangled_name, Func *demangled);
    int find(Func &func) const;
    std::string getDef(
      const Func &func,
      Function *F, std::function<std::string(Value *)> GetName
    ) const;
  };
} // namespace llvm

#endif