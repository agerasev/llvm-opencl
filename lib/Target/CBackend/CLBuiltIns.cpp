#include "CLBuiltIns.h"

#include <iostream>
#include <algorithm>
#include <vector>
#include <initializer_list>
#include <sstream>
#include <functional>

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
    name = signature.substr(name_begin, name_end);
    if (name_begin > 1) {
      ret = signature.substr(0, name_begin - 1);
    } else {
      ret = "";
    }
    args = split(signature.substr(name_end + 1, signature.size() - name_end - 2), ", ");
  }

  std::string Func::to_string(bool with_ret) const {
    std::stringstream out;
    if (with_ret) {
      out << ret << " ";
    }
    out << name << "(";
    for (size_t i = 0; i < args.size(); ++i) {
      if (i > 0) {
        out << ", ";
      }
      out << args[i];
    }
    out << ")";
    return out.str();
  }

  bool Func::operator<(const Func &other) const {
    if (name < other.name) {
      return true;
    } else if (name > other.name) {
      return false;
    }
    if (args.size() < other.args.size()) {
      return true;
    } else if (args.size() > other.args.size()) {
      return false;
    }
    for (size_t i = 0; i < args.size(); ++i) {
      if (args[i] < other.args[i]) {
        return true;
      } else if (args[i] > other.args[i]) {
        return false;
      }
    }
    return false;
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
      replace(arg, "unsigned ", "u");
      replace(arg, " vector[", "");
      replace(arg, "]", "");

      replace(arg, "AS0", "__private");
      replace(arg, "AS1", "__global");
      replace(arg, "AS2", "__constant");
      replace(arg, "AS3", "__local");
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

  int CLBuiltIns::findMangled(const char *mangled_name, Func *demangled) {
    Func local_func;
    if (!demangled) {
      demangled = &local_func;
    }
    switch (demangle(mangled_name, demangled)) {
      case 0:
        return 0;
      case 1:
        return find(*demangled);
      case -1:
      default:
        return -1;
    }
  }

  std::string CLBuiltIns::getDef(
    const Func &func,
    Function *F,
    std::function<std::string(Value *)> GetValueName,
    std::function<std::string(Type *)> GetTypeName
  ) const {
    std::stringstream out;

    out << "return ";
    Type *Ty = F->getReturnType();
    if (dyn_cast<VectorType>(Ty)) {
      out << "convert_" << GetTypeName(Ty) << "(";
    } else {
      out << "(" << GetTypeName(Ty) << ")(";
    }
    out << func.name << "(";
    int i = 0;
    for (auto &a : F->args()) {
      if (i > 0) {
        out << ", ";
      }
      if (dyn_cast<VectorType>(a.getType())) {
        out << "convert_" << func.args[i];
      } else {
        out << "(" << func.args[i] << ")";
      }
      out << "(" << GetValueName(&a) << ")";
      i += 1;
    }
    out << "))";

    return out.str();
  }

  int CLBuiltIns::add_functions(const std::initializer_list<Func> &list) {
    int n = 0;
    for (const Func &f : list) {
      n += set.insert(f).second;
    }
    return n;
  }

  std::vector<std::string> operator+(const std::vector<std::string> &a, const std::vector<std::string> &b) {
    std::vector<std::string> r;
    r.reserve(a.size() + b.size());
    for (std::string x : a) {
      r.push_back(x);
    }
    for (std::string y : b) {
      r.push_back(y);
    }
    return r;
  }
  std::vector<std::string> operator+(const std::vector<std::string> &a, const std::string &b) {
    std::vector<std::string> r;
    r.reserve(a.size() + 1);
    for (std::string x : a) {
      r.push_back(x);
    }
    r.push_back(b);
    return r;
  }
  std::vector<std::string> operator+(const std::string &a, const std::vector<std::string> &b) {
    std::vector<std::string> r;
    r.reserve(1 + b.size());
    r.push_back(a);
    for (std::string y : b) {
      r.push_back(y);
    }
    return r;
  }

  std::vector<std::string> operator*(const std::vector<std::string> &a, const std::vector<std::string> &b) {
    std::vector<std::string> r;
    r.reserve(a.size()*b.size());
    for (std::string x : a) {
      for (std::string y : b) {
        r.push_back(x + y);
      }
    }
    return r;
  }
  std::vector<std::string> operator*(const std::vector<std::string> &a, const std::string &b) {
    std::vector<std::string> r;
    r.reserve(a.size());
    for (std::string x : a) {
      r.push_back(x + b);
    }
    return r;
  }
  std::vector<std::string> operator*(const std::string &a, const std::vector<std::string> &b) {
    std::vector<std::string> r;
    r.reserve(b.size());
    for (std::string y : b) {
      r.push_back(a + y);
    }
    return r;
  }

  CLBuiltIns::CLBuiltIns() {
    std::vector<std::string> dim{"2", "3", "4", "8", "16"};
    std::vector<std::string> gendim = "" + dim;
    std::vector<std::string> typeis{"char", "short", "int", "long"};
    std::vector<std::string> typeiu = "u"*typeis;
    std::vector<std::string> typei = typeis + typeiu;
    std::vector<std::string> typef{"float", "double"};
    std::vector<std::string> type = typei + typef;
    std::vector<std::string> addrspace{"__private", "__global", "__constant", "__local"};

    // Work-Item Functions
    add_functions({
      Func("uint", "get_work_dim", {}),
      Func("size_t", "get_global_size", {"uint"}),
      Func("size_t", "get_global_id", {"uint"}),
      Func("size_t", "get_local_size", {"uint"}),
      Func("size_t", "get_enqueued_local_size", {"uint"}),
      Func("size_t", "get_local_id", {"uint"}),
      Func("size_t", "get_num_groups", {"uint"}),
      Func("size_t", "get_group_id", {"uint"}),
      Func("size_t", "get_global_offset", {"uint"}),
      Func("size_t", "get_global_linear_id", {}),
      Func("size_t", "get_local_linear_id", {}),
    });

    // Math Functions
    for (std::string gd : gendim) {
      for (std::string tf : typef) {
        std::string gtf = tf + gd;
        add_functions({
          Func(gtf, "acos", {gtf}),
          Func(gtf, "acosh", {gtf}),
          Func(gtf, "acospi", {gtf}),
          Func(gtf, "asin", {gtf}),
          Func(gtf, "asinh", {gtf}),
          Func(gtf, "asinpi", {gtf}),
          Func(gtf, "atan", {gtf}),
          Func(gtf, "atan2", {gtf, gtf}),
          Func(gtf, "atanh", {gtf}),
          Func(gtf, "atanpi", {gtf}),
          Func(gtf, "atan2pi", {gtf, gtf}),
          Func(gtf, "cbrt", {gtf}),
          Func(gtf, "ceil", {gtf}),
          Func(gtf, "copysign", {gtf, gtf}),
          Func(gtf, "cos", {gtf}),
          Func(gtf, "cosh", {gtf}),
          Func(gtf, "cospi", {gtf}),
          Func(gtf, "erfc", {gtf}),
          Func(gtf, "erf", {gtf}),
          Func(gtf, "exp", {gtf}),
          Func(gtf, "exp2", {gtf}),
          Func(gtf, "exp10", {gtf}),
          Func(gtf, "expm1", {gtf}),
          Func(gtf, "fabs", {gtf}),
          Func(gtf, "fdim", {gtf, gtf}),
          Func(gtf, "floor", {gtf}),
          Func(gtf, "fma", {gtf, gtf, gtf}),
          Func(gtf, "fmax", {gtf, gtf}),
          Func(gtf, "fmax", {gtf, tf}),
          Func(gtf, "fmin", {gtf, gtf}),
          Func(gtf, "fmin", {gtf, tf}),
          Func(gtf, "fmod", {gtf, gtf}),
          Func(gtf, "fract", {gtf, gtf+"*"}),
          Func(gtf, "frexp", {gtf, "int"+gd+"*"}),
          Func(gtf, "hypot", {gtf, gtf}),
          Func("int"+gd, "ilogb", {gtf}),
          Func(gtf, "ldexp", {gtf, "int"+gd}),
          Func(gtf, "lgamma", {gtf}),
          Func(gtf, "lgamma_r", {gtf, "int"+gd+"*"}),
          Func(gtf, "log", {gtf}),
          Func(gtf, "log2", {gtf}),
          Func(gtf, "log10", {gtf}),
          Func(gtf, "log1p", {gtf}),
          Func(gtf, "logb", {gtf}),
          Func(gtf, "mad", {gtf, gtf, gtf}),
          Func(gtf, "maxmag", {gtf, gtf}),
          Func(gtf, "minmag", {gtf, gtf}),
          Func(gtf, "modf", {gtf, gtf+"*"}),
          Func(gtf, "nan", {"uint"+gd}),
          Func(gtf, "nan", {"ulong"+gd}),
          Func(gtf, "nextafter", {gtf, gtf}),
          Func(gtf, "pow", {gtf, gtf}),
          Func(gtf, "pown", {gtf, "int"+gd}),
          Func(gtf, "powr", {gtf, gtf}),
          Func(gtf, "remainder", {gtf, gtf}),
          Func(gtf, "remquo", {gtf, gtf, "int"+gd+"*"}),
          Func(gtf, "rint", {gtf}),
          Func(gtf, "rootn", {gtf, "int"+gd}),
          Func(gtf, "round", {gtf}),
          Func(gtf, "rsqrt", {gtf}),
          Func(gtf, "sqrt", {gtf}),
          Func(gtf, "sin", {gtf}),
          Func(gtf, "sincos", {gtf, gtf+"*"}),
          Func(gtf, "sinh", {gtf}),
          Func(gtf, "sinpi", {gtf}),
          Func(gtf, "tan", {gtf}),
          Func(gtf, "tanh", {gtf}),
          Func(gtf, "tanpi", {gtf}),
          Func(gtf, "tgamma", {gtf}),
          Func(gtf, "trunc", {gtf}),
        });
      }
    }

    // Integer Functions
    for (std::string gd : gendim) {
      for (std::string ti : typei) {
        add_functions({
          Func(ti+gd, "add_sat", { ti+gd, ti+gd }),
          Func(ti+gd, "hadd", { ti+gd, ti+gd }),
          Func(ti+gd, "rhadd", { ti+gd, ti+gd }),
          Func(ti+gd, "clamp", { ti+gd, ti+gd, ti+gd }),
          Func(ti+gd, "clz", { ti+gd }),
          Func(ti+gd, "ctz", { ti+gd }),
          Func(ti+gd, "mad_hi", { ti+gd, ti+gd, ti+gd }),
          Func(ti+gd, "mad_sat", { ti+gd, ti+gd, ti+gd }),
          Func(ti+gd, "max", { ti+gd, ti+gd }),
          Func(ti+gd, "min", { ti+gd, ti+gd }),
          Func(ti+gd, "mul_hi", { ti+gd, ti+gd }),
          Func(ti+gd, "rotate", { ti+gd, ti+gd }),
          Func(ti+gd, "sub_sat", { ti+gd, ti+gd }),
          Func(ti+gd, "popcount", { ti+gd }),
        });
      }
      for (std::string s : {"", "u"}) {
        add_functions({
          Func(s+"short"+gd, "upsample", { s+"char"+gd, "uchar"+gd }),
          Func(s+"int"+gd, "upsample", { s+"short"+gd, "ushort"+gd }),
          Func(s+"long"+gd, "upsample", { s+"int"+gd, "uint"+gd }),
        });
      }
      for (std::string tis : typeis) {
        std::string tiu = "u"+tis;
        add_functions({
          Func(tiu+gd, "abs", { tis+gd }),
          Func(tiu+gd, "abs_diff", { tis+gd, tis+gd }),
          Func(tiu+gd, "clamp", { tiu+gd, tis+gd, tis+gd }),
          Func(tiu+gd, "max", { tiu+gd, tis+gd }),
          Func(tiu+gd, "min", { tiu+gd, tis+gd }),
        });
      }
    }

    for (std::string t : type) {
      for (std::string d : dim) {
        std::string vt = t + d;
        for (std::string as : addrspace) {
          add_functions({
            Func(vt, "vload"+d, {"uint", t+" const "+as+"*"}),
            Func("void", "vstore"+d, {vt, "uint", t+" "+as+"*"}),
          });
        }
      }
    }
    for (std::string tf : typef) {
      for (std::string d : dim) {
        std::string vtf = tf + d;
        add_functions({
          Func(tf, "length", {vtf}),
        });
      }
    }
  }
} // namespace llvm
