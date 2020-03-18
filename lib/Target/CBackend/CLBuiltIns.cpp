#include "CLBuiltIns.h"

#include <algorithm>
#include <unordered_set>


namespace llvm {
  bool Matcher::match(const std::string &str) {
    auto postfixes = match_prefix(str);
    return postfixes.find(MatchInfo("")) != postfixes.end();
  }

  // FIXME: Use C++17 set.merge()
  static void merge(std::set<MatchInfo> &dst, std::set<MatchInfo> &&src) {
    for (const MatchInfo &info : src) {
      dst.insert(MatchInfo(info.tail, std::move(info.sync)));
    }
  }

  static void replace(std::string &str, const std::string &from, const std::string &to) {
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

  MatchInfo::MatchInfo(const std::string &tail) : tail(tail) {}
  MatchInfo::MatchInfo(const std::string &tail, std::map<const Matcher *, int> &&sync) :
    tail(tail), sync(std::move(sync))
  {}

  bool MatchInfo::operator<(const MatchInfo &other) const {
    return tail < other.tail;
  }

  class ConstMatcher : public Matcher {
  private:
    std::string value;
  public:
    ConstMatcher(const char *value) : value(value) {}
    ConstMatcher(std::string &&value) : value(std::move(value)) {}

    std::set<MatchInfo> match_prefix(MatchInfo &&info) const override {
      auto res = std::mismatch(value.begin(), value.end(), info.tail.begin());
      if (res.first == value.end()) {
        return std::set<MatchInfo>{
          MatchInfo(
            info.tail.substr(value.size(), info.tail.size() - value.size()),
            std::move(info.sync)
          )
        };
      } else {
        return std::set<MatchInfo>();
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

    ListMatcher() = default;
    template <typename ... Args>
    ListMatcher(Args &&...matchers) {
      add_matcher(std::forward<Args>(matchers)...);
    }
  };

  class OptionMatcher : public ListMatcher {
  public:
    OptionMatcher() = default;
    template <typename ... Args>
    OptionMatcher(Args &&...matchers) : ListMatcher(std::forward<Args>(matchers)...) {}

    std::set<MatchInfo> match_prefix(MatchInfo &&info) const override {
      std::set<MatchInfo> postfixes;
      for (const std::unique_ptr<Matcher> &matcher : matchers) {
        merge(postfixes, matcher->match_prefix(MatchInfo(info)));
      }
      return postfixes;
    }
  };

  class ProductMatcher : public ListMatcher {
  public:
    ProductMatcher() = default;
    template <typename ... Args>
    ProductMatcher(Args &&...matchers) : ListMatcher(std::forward<Args>(matchers)...) {}

    std::set<MatchInfo> match_prefix(MatchInfo &&info) const override {
      std::set<MatchInfo> postfixes{std::move(info)};
      for (const std::unique_ptr<Matcher> &matcher : matchers) {
        std::set<MatchInfo> new_postfixes;
        for (const MatchInfo &postfix : postfixes) {
          merge(new_postfixes, matcher->match_prefix(MatchInfo(postfix)));
        }
        postfixes = std::move(new_postfixes);
      }
      return postfixes;
    }
  };

  class SyncOptionMatcher : public OptionMatcher {
  public:
    SyncOptionMatcher() {}
    template <typename ... Args>
    SyncOptionMatcher(Args &&...matchers) : OptionMatcher(std::forward<Args>(matchers)...) {}

    std::set<MatchInfo> match_prefix(MatchInfo &&info) const override {
      auto it = info.sync.find(this);
      if (it == info.sync.end()) { 
        std::set<MatchInfo> postfixes;
        int i = 0;
        for (const std::unique_ptr<Matcher> &matcher : matchers) {
          std::map<const Matcher *, int> new_sync = info.sync;
          new_sync.insert(std::pair<const Matcher *, int>(this, i));
          merge(postfixes, matcher->match_prefix(MatchInfo(info.tail, std::move(new_sync))));
          i += 1;
        }
        return postfixes;
      } else {
        auto mit = matchers.begin();
        std::advance(mit, it->second);
        return (*mit)->match_prefix(std::move(info));
      }
    }
  };

  // FIXME: It is possible to create a loop using SyncMatcher
  //        which will result in memory leak
  class SyncMatcher : public Matcher {
  protected:
    std::shared_ptr<Matcher> matcher;
    SyncMatcher(const std::shared_ptr<Matcher> &matcher) : matcher(matcher) {}
  public:
    SyncMatcher(MatcherPtr &&matcher) : matcher(std::move(matcher)) {}

    std::set<MatchInfo> match_prefix(MatchInfo &&info) const override {
      return matcher->match_prefix(std::move(info));
    }

    std::unique_ptr<SyncMatcher> clone() const {
      return std::unique_ptr<SyncMatcher>(new SyncMatcher(matcher));
    }
    std::unique_ptr<SyncMatcher> operator()() const {
      return clone();
    }
  };

  template <typename ... Args>
  static MatcherPtr option(Args &&...matchers) {
    return make_unique<OptionMatcher>(std::forward<Args>(matchers)...);
  }

  template <typename ... Args>
  static MatcherPtr option_sync(Args &&...matchers) {
    return make_unique<SyncOptionMatcher>(std::forward<Args>(matchers)...);
  }

  template <typename ... Args>
  static MatcherPtr product(Args &&...matchers) {
    return make_unique<ProductMatcher>(std::forward<Args>(matchers)...);
  }

  CLBuiltIns::CLBuiltIns() {
    SyncMatcher dim(option_sync("2", "3", "4", "8", "16"));
    SyncMatcher gendim(option_sync("", "2", "3", "4", "8", "16"));
    SyncMatcher typei(option_sync("char", "uchar", "short", "ushort", "int", "uint", "long", "ulong"));
    SyncMatcher typef(option_sync("float", "double"));
    SyncMatcher type(option_sync(typei(), typef()));
    SyncMatcher vectype(product(type(), dim()));
    SyncMatcher vectypei(product(typei(), dim()));
    SyncMatcher vectypef(product(typef(), dim()));
    SyncMatcher gentype(product(type(), gendim()));
    SyncMatcher gentypei(product(typei(), gendim()));
    SyncMatcher gentypef(product(typef(), gendim()));
    SyncMatcher addrspace(product("AS", option_sync("0", "1", "2", "3")));
    matcher = option(
      // Work-Item Functions
      option(
        "get_work_dim()",
        "get_global_size(uint)",
        "get_global_id(uint)",
        "get_local_size(uint)",
        "get_enqueued_local_size(uint)",
        "get_local_id(uint)",
        "get_num_groups(uint)",
        "get_group_id(uint)",
        "get_global_offset(uint)",
        "get_global_linear_id()",
        "get_local_linear_id()"
      ),
      // Math Functions
      option(
        product("acos(", gentypef(), ")"),
        product("acosh(", gentypef(), ")"),
        product("acospi(", gentypef(), ")"),
        product("asin(", gentypef(), ")"),
        product("asinh(", gentypef(), ")"),
        product("asinpi(", gentypef(), ")"),
        product("atan(", gentypef(), ")"),
        product("atan2(", gentypef(), ", ", gentypef(), ")"),
        product("atanh(", gentypef(), ")"),
        product("atanpi(", gentypef(), ")"),
        product("atan2pi(", gentypef(), ", ", gentypef(), ")"),
        product("cbrt(", gentypef(), ")"),
        product("ceil(", gentypef(), ")"),
        product("copysign(", gentypef(), ", ", gentypef(), ")"),
        product("cos(", gentypef(), ")"),
        product("cosh(", gentypef(), ")"),
        product("cospi(", gentypef(), ")"),
        product("erfc(", gentypef(), ")"),
        product("erf(", gentypef(), ")"),
        product("exp(", gentypef(), ")"),
        product("exp2(", gentypef(), ")"),
        product("exp10(", gentypef(), ")"),
        product("expm1(", gentypef(), ")"),
        product("fabs(", gentypef(), ")"),
        product("fdim(", gentypef(), ", ", gentypef(), ")"),
        product("floor(", gentypef(), ")"),
        product("fma(", gentypef(), ", ", gentypef(), ", ", gentypef(), ")"),
        product("fmax(", gentypef(), ", ", gentypef(), ")"),
        product("fmax(", gentypef(), ", typef)"),
        product("fmin(", gentypef(), ", ", gentypef(), ")"),
        product("fmin(", gentypef(), ", typef)"),
        product("fmod(", gentypef(), ", ", gentypef(), ")"),
        product("fract(", gentypef(), ", ", gentypef(), "*)"),
        product("frexp(", gentypef(), ", ", product("int", gendim()), "*)"),
        product("hypot(", gentypef(), ", ", gentypef(), ")"),
        product("ilogb(", gentypef(), ")"),
        product("ldexp(", gentypef(), ", ", product("int", gendim()), ")"),
        product("lgamma(", gentypef(), ")"),
        product("lgamma_r(", gentypef(), ", ", product("int", gendim()), "*)"),
        product("log(", gentypef(), ")"),
        product("log2(", gentypef(), ")"),
        product("log10(", gentypef(), ")"),
        product("log1p(", gentypef(), ")"),
        product("logb(", gentypef(), ")"),
        product("mad(", gentypef(), ", ", gentypef(), ", ", gentypef(), ")"),
        product("maxmag(", gentypef(), ", ", gentypef(), ")"),
        product("minmag(", gentypef(), ", ", gentypef(), ")"),
        product("modf(", gentypef(), ", ", gentypef(), " *)"),
        product("nan(", product("uint", gendim()), ")"),
        product("nan(", product("ulong", gendim()), ")"),
        product("nextafter(", gentypef(), ", ", gentypef(), ")"),
        product("pow(", gentypef(), ", ", gentypef(), ")"),
        product("pown(", gentypef(), ", ", product("int", gendim()), ")"),
        product("powr(", gentypef(), ", ", gentypef(), ")"),
        product("remainder(", gentypef(), ", ", gentypef(), ")"),
        product("remquo(", gentypef(), ", ", gentypef(), ", ", product("int", gendim()), "*)"),
        product("rint(", gentypef(), ")"),
        product("rootn(", gentypef(), ", ", product("int", gendim()), ")"),
        product("round(", gentypef(), ")"),
        product("rsqrt(", gentypef(), ")"),
        product("sqrt(", gentypef(), ")"),
        product("sin(", gentypef(), ")"),
        product("sincos(", gentypef(), ", ", gentypef(), "*)"),
        product("sinh(", gentypef(), ")"),
        product("sinpi(", gentypef(), ")"),
        product("tan(", gentypef(), ")"),
        product("tanh(", gentypef(), ")"),
        product("tanpi(", gentypef(), ")"),
        product("tgamma(", gentypef(), ")"),
        product("trunc(", gentypef(), ")")
      ),
      product("vload", dim(), "(uint, ", type(), " const ", addrspace(), "*)"),
      product("vstore", dim(), "(", gentype(), ", uint, ", type(), " ", addrspace(), "*)"),
      product("length(float", dim(), ")")
    );
  }

  bool CLBuiltIns::isBuiltIn(const char *full_name) const {
    std::string name(full_name);
    replace(name, "unsigned ", "u");
    replace(name, " vector[", "");
    replace(name, "]", "");
    return matcher->match(name);
  }
} // namespace llvm
