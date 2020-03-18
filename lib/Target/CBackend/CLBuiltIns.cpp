#include "CLBuiltIns.h"

#include <algorithm>
#include <vector>
#include <initializer_list>
#include <sstream>

#include "llvm/Demangle/Demangle.h"


namespace llvm_cbe {
  using namespace llvm;

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

  static std::vector<std::string> split(const std::string &str, const std::string &sep) {
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

  Func::Func() {}
  Func::Func(const std::string &ret, const std::string &name, const std::initializer_list<std::string> &args) :
    name(name), ret(ret), args(args)
  {}

  Func::Func(const std::string &signature) {
    size_t name_end = signature.find('(');
    size_t name_begin = signature.rfind(' ', name_end);
    if (name_begin == std::string::npos) {
      name_begin = 0;
    }
    name = std::move(signature.substr(name_begin, name_end));
    ret = std::move(signature.substr(0, name_begin - 1));
    args = std::move(split(signature.substr(name_end + 1, signature.size() - 1), ", "));
  }

  std::string Func::to_string(bool with_ret=true) {
    std::stringstream out;
    if (with_ret) {
      out << ret << " ";
    }
    out << name << "(";
    for (int i = 0; i < args.size(); ++i) {
      if (i > 0) {
        out << ", ";
      }
      out << args[i];
    }
    out << ")";
  }

  bool Func::operator<(const Func &other) const {
    if (name < other.name) {
      return true;
    } else if (name > other.name) {
      return false;
    } else {
      if (args.size() < other.args.size()) {
        return true;
      } else if (args.size() < other.args.size()) {
        return false;
      }
      for (int i = 0; i < args.size(); ++i) {
        if (args[i] < other.args[i]) {
          return true;
        } else if (args[i] > other.args[i]) {
          return false;
        }
      }
    }
  }

  int CLBuiltIns::demangle(const char *mangled_name, Func *demangled) {
    std::string signature;

    ItaniumPartialDemangler dmg;
    if (dmg.partialDemangle(mangled_name)) {
#ifndef NDEBUG
      //errs() << "Cannot demangle function '" << mangled_name << "'\n";
#endif
      return 0;
    } else {
      size_t size = 0;
      char *buf = dmg.finishDemangle(nullptr, &size);
      if (buf == nullptr) {
        return -1;
      }
      signature = std::string(buf);
      std::free(buf);
    }

    Func func(signature);

    for (std::string &arg : func.args) {
      replace(signature, "unsigned ", "u");
      replace(signature, " vector[", "");
      replace(signature, "]", "");

      replace(signature, "AS0", "__private");
      replace(signature, "AS1", "__global");
      replace(signature, "AS2", "__constant");
      replace(signature, "AS3", "__local");
    }

    if (demangled) {
      *demangled = func;
    }

    return 1;
  }

  int CLBuiltIns::find(Func &func) const {
    auto it = set.find(func);
    if (it != set.end()) {
      func.ret = it->ret;
      return 1;
    } else {
      return 0;
    }
  }

  std::string CLBuiltIns::getBuiltInDef(
    const Func &func,
    Function *F, std::function<std::string(Value *)> GetName
  ) const {
    std::string _out;
    raw_string_ostream out(_out);

    auto sp = split(demangled, "(");
    std::string name = sp[0], stypes = sp[1];
    replace(stypes, ")", "");
    std::vector<std::string> types = split(stypes, ", ");

    out << "return ";
    // FIXME: Also cast return type
    /*
    if (dyn_cast<VectorType>(F->getReturnType())) {
      
    } else {

    }
    */
    out << name << "(";
    int i = 0;
    for (auto &a : F->args()) {
      if (i > 0) {
        out << ", ";
      }
      if (dyn_cast<VectorType>(a.getType())) {
        out << "convert_" << types[i];
      } else {
        out << "(" << types[i] << ")";
      }
      out << "(" << GetName(&a) << ")";
      i += 1;
    }
    out << ")";

    return out.str();
  }

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
} // namespace llvm
