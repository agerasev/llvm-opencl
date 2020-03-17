#include "CLBuiltIns.h"

#include <algorithm>
#include <unordered_set>


namespace llvm {
  bool Matcher::match(const std::string &str) {
    auto postfixes = match_prefix(str);
    return postfixes.find("") != postfixes.end();
  }

  // FIXME: Use C++17 set.merge()
  template <typename T>
  static void merge(std::set<T> &dst, const std::set<T> &src) {
    for (const std::string &str : src) {
      dst.insert(str);
    }
  }

  class ConstMatcher : public Matcher {
  private:
    std::string value;
  public:
    ConstMatcher(const char *value) : value(value) {}
    ConstMatcher(std::string &&value) : value(std::move(value)) {}
    std::set<std::string> match_prefix(const std::string &str) const override {
      auto res = std::mismatch(value.begin(), value.end(), str.begin());
      if (res.first == value.end()) {
        return std::set<std::string>{
          str.substr(value.size(), str.size() - value.size())
        };
      } else {
        return std::set<std::string>();
      }
    }
  };

  class ListMatcher : public Matcher {
  protected:
    std::list<MatcherPtr> matchers;
  public:
    void add_matcher(std::string &&value) {
      matchers.push_back(make_unique<ConstMatcher>(std::move(value)));
    }
    void add_matcher(MatcherPtr &&matcher) {
      matchers.push_back(std::move(matcher));
    }
    template <typename ... Args>
    void add_matcher(std::string &&value, Args &&...other_matchers) {
      add_matcher(std::move(value));
      add_matcher(std::forward<Args>(other_matchers)...);
    }
    template <typename ... Args>
    void add_matcher(MatcherPtr &&matcher, Args &&...other_matchers) {
      add_matcher(std::move(matcher));
      add_matcher(std::forward<Args>(other_matchers)...);
    }
    template <typename ... Args>
    ListMatcher(Args &&...matchers) {
      add_matcher(std::forward<Args>(matchers)...);
    }
  };

  class OptionMatcher : public ListMatcher {
  public:
    template <typename ... Args>
    OptionMatcher(Args &&...matchers) : ListMatcher(std::forward<Args>(matchers)...) {}
    std::set<std::string> match_prefix(const std::string &str) const override {
      std::set<std::string> postfixes;
      for (const std::unique_ptr<Matcher> &matcher : matchers) {
        merge(postfixes, std::move(matcher->match_prefix(str)));
      }
      return postfixes;
    }
  };

  /*
  class CartesianMatcher : public ListMatcher {
  public:
    CartesianMatcher(std::list<MatcherPtr> &&matchers);
    std::set<std::string> match_prefix(const std::string &str) const override;
  };

  CartesianMatcher::CartesianMatcher(std::list<MatcherPtr> &&matchers) : ListMatcher(std::move(matchers)) {}

  std::set<std::string> CartesianMatcher::match_prefix(const std::string &str) const {
    std::set<std::string> postfixes{str};
    for (const std::unique_ptr<Matcher> &matcher : matchers) {
      std::set<std::string> new_postfixes;
      for (const std::string &postfix : postfixes) {
        merge(new_postfixes, std::move(matcher->match_prefix(postfix)));
      }
      postfixes = std::move(new_postfixes);
    }
    return postfixes;
  }
  */

  /*
  static MatcherPtr make_const(const char *str) {
    return make_unique<ConstMatcher>(std::string(str));
  }
  static void add_to_list(std::list<MatcherPtr> &list, MatcherPtr &&matcher) {
    list.push_back(std::forward<MatcherPtr>(matcher));
  }
  template <typename ... Args>
  static void add_to_list(std::list<MatcherPtr> &list, MatcherPtr &&matcher, Args &&...other_matchers) {
    list.push_back(std::forward<MatcherPtr>(matcher));
    add_to_list(list, std::forward<Args>(other_matchers)...);
  }
  template <typename ... Args>
  static MatcherPtr make_option(Args &&...matchers) {
    std::list<MatcherPtr> list;
    add_to_list(list, std::forward<Args>(matchers)...);
    return make_unique<OptionMatcher>(std::move(list));
  }

  CLBuiltIns::CLBuiltIns() : matcher(make_option(
    make_const("get_global_id(unsigned int)"),
    make_const("vload2(unsigned int, float const AS1*)"),
    make_const("vload3(unsigned int, float const AS1*)"),
    make_const("vload4(unsigned int, float const AS1*)"),
    make_const("vstore2(float vector[2], unsigned int, float AS1*)"),
    make_const("vstore3(float vector[3], unsigned int, float AS1*)"),
    make_const("vstore4(float vector[4], unsigned int, float AS1*)")
  )) {}
  */
  CLBuiltIns::CLBuiltIns() : matcher(make_unique<OptionMatcher>(
    "get_global_id(unsigned int)",
    "vload2(unsigned int, float const AS1*)",
    "vload3(unsigned int, float const AS1*)",
    "vload4(unsigned int, float const AS1*)",
    "vstore2(float vector[2], unsigned int, loat AS1*)",
    "vstore3(float vector[3], unsigned int, float AS1*)",
    "vstore4(float vector[4], unsigned int, float AS1*)",
    "length(float vector[2])",
    "length(float vector[3])",
    "length(float vector[4])"
  )) {}

  bool CLBuiltIns::isBuiltIn(const char *full_name) const {
    return matcher->match(full_name);
  }

} // namespace llvm
