#ifndef CLBUILTINS_H
#define CLBUILTINS_H

#include <llvm/ADT/STLExtras.h>

#include <string>
#include <memory>
#include <list>
#include <set>
#include <map>


namespace llvm {
  class Matcher;
  
  typedef std::unique_ptr<Matcher> MatcherPtr;

  class MatchInfo {
  public:
    std::string tail;
    mutable std::map<const Matcher *, int> sync;

    MatchInfo(const std::string &tail);
    MatchInfo(const std::string &tail, std::map<const Matcher *, int> &&sync);

    MatchInfo(const MatchInfo &) = default;
    MatchInfo &operator=(const MatchInfo &) = default;

    MatchInfo(MatchInfo &&) = default;
    MatchInfo &operator=(MatchInfo &&) = default;

    bool operator<(const MatchInfo &other) const;
  };

  class Matcher {
  public:
    Matcher() = default;
    virtual ~Matcher() = default;

    virtual std::set<MatchInfo> match_prefix(MatchInfo &&info) const = 0;
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