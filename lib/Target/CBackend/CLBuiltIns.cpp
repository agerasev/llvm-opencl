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

  static void replace(std::string &str, const std::string &old, const std::string &new_) {
    size_t pos = 0;
    for (;;) {
      pos = str.find(old, pos);
      if (pos == std::string::npos) {
        break;
      }
      str.replace(pos, old.size(), new_);
      pos += old.size();
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

    void substitute(const std::string &from, const std::string &to) override {
      replace(value, from, to);
    }

    MatcherPtr clone() const override {
      return make_unique<ConstMatcher>(std::string(value));
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

    ListMatcher() = default;
    template <typename ... Args>
    ListMatcher(Args &&...matchers) {
      add_matcher(std::forward<Args>(matchers)...);
    }

    void substitute(const std::string &from, const std::string &to) override {
      for(MatcherPtr &matcher : matchers) {
        matcher->substitute(from, to);
      }
    }

    void clone_from(const ListMatcher &other) {
      matchers.clear();
      for (const MatcherPtr &matcher : other.matchers) {
        add_matcher(matcher->clone());
      }
    }
  };

  class OptionMatcher : public ListMatcher {
  public:
    OptionMatcher() = default;
    template <typename ... Args>
    OptionMatcher(Args &&...matchers) : ListMatcher(std::forward<Args>(matchers)...) {}

    std::set<std::string> match_prefix(const std::string &str) const override {
      std::set<std::string> postfixes;
      for (const std::unique_ptr<Matcher> &matcher : matchers) {
        merge(postfixes, std::move(matcher->match_prefix(str)));
      }
      return postfixes;
    }

    MatcherPtr clone() const override {
      auto new_matcher = make_unique<OptionMatcher>();
      new_matcher->clone_from(*this);
      return new_matcher;
    }
  };

  class ProductMatcher : public ListMatcher {
  public:
    ProductMatcher() = default;
    template <typename ... Args>
    ProductMatcher(Args &&...matchers) : ListMatcher(std::forward<Args>(matchers)...) {}

    std::set<std::string> match_prefix(const std::string &str) const override {
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

    MatcherPtr clone() const override {
      auto new_matcher = make_unique<OptionMatcher>();
      new_matcher->clone_from(*this);
      return new_matcher;
    }
  };

  template <typename ... Args>
  static MatcherPtr option(Args &&...matchers) {
    return make_unique<OptionMatcher>(std::forward<Args>(matchers)...);
  }

  template <typename ... Args>
  static MatcherPtr product(Args &&...matchers) {
    return make_unique<ProductMatcher>(std::forward<Args>(matchers)...);
  }

  CLBuiltIns::CLBuiltIns() {
    auto dims = [] () { return option("2", "3", "4", "8", "16"); };
    auto addrspace = [] () { return product("AS", option("0", "1", "2", "3")); };
    matcher = option(
      // Work-Item Functions
      option(
        "get_work_dim()",
        "get_global_size(unsigned int)",
        "get_global_id(unsigned int)",
        "get_local_size(unsigned int)",
        "get_enqueued_local_size(unsigned int)",
        "get_local_id(unsigned int)",
        "get_num_groups(unsigned int)",
        "get_group_id(unsigned int)",
        "get_global_offset(unsigned int)",
        "get_global_linear_id()",
        "get_local_linear_id()"
      ),
      product("vload", dims(), "(unsigned int, float const ", addrspace(), "*)"),
      "vstore2(float vector[2], unsigned int, float AS1*)",
      "vstore3(float vector[3], unsigned int, float AS1*)",
      "vstore4(float vector[4], unsigned int, float AS1*)",
      "length(float vector[2])",
      "length(float vector[3])",
      "length(float vector[4])"
    );
  }

  bool CLBuiltIns::isBuiltIn(const char *full_name) const {
    return matcher->match(full_name);
  }

} // namespace llvm
