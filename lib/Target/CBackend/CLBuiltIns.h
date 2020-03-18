#ifndef CLBUILTINS_H
#define CLBUILTINS_H

#include <llvm/ADT/STLExtras.h>

#include <string>
#include <memory>
#include <list>
#include <set>


namespace llvm {
  class Matcher;
  
  typedef std::unique_ptr<Matcher> MatcherPtr;

  class Matcher {
  public:
    Matcher() = default;
    virtual ~Matcher() = default;

    virtual std::set<std::string> match_prefix(const std::string &str) const = 0;
    virtual void substitute(const std::string &from, const std::string &to) = 0;
    virtual MatcherPtr clone() const = 0;

    bool match(const std::string &str);
  };


  class CLBuiltIns {
  private:
    MatcherPtr matcher;
  public:
    CLBuiltIns();
    bool isBuiltIn(const char *func_name_and_sig) const;
  };
} // namespace llvm

#endif