#ifndef CLBUILTINS_H
#define CLBUILTINS_H

#include <string>
#include <map>
#include <set>
#include <initializer_list>
#include <functional>

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Value.h"


namespace llvm_opencl {
  using namespace llvm;

  class Func {
  public:
    std::string name;
    std::string ret;
    std::vector<std::string> args;
    Func();
    Func(const std::string &ret, const std::string &name, const std::initializer_list<std::string> &args);
    Func(const std::string &signature);
    std::string to_string(bool with_ret=true) const;
    bool operator<(const Func &other) const;
  };

  class CLBuiltIns {
  private:
    std::map<std::string, std::string> commons;
    std::set<Func> wrappers;
    
    bool add_common(const std::string &name, const std::string &body);
    bool add_wrapper(const Func &func);
    int add_wrappers(const std::initializer_list<Func> &list);

    static int demangle(const char *name, Func *func);
    int find_common(const char *name) const;
    int find_wrapper(Func &func) const;
  public:
    CLBuiltIns();
    int find(const char *name, Func *func);
    bool printDefinition(
      raw_ostream &Out,
      const Func &func,
      Function *F,
      std::function<std::string(int)> GetValueName,
      std::function<std::string(Type *)> GetTypeName
    ) const;
  };
} // namespace llvm

#endif