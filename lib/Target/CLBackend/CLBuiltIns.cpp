#include "CLBuiltIns.h"

#include <algorithm>
#include <vector>
#include <initializer_list>
#include <sstream>
#include <functional>

#include "llvm/Demangle/Demangle.h"

#include "StringTools.h"


namespace llvm_opencl {
  using namespace llvm;

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
      //errs() << "Cannot demangle function '" << mangled_name << "'\n";
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

  int CLBuiltIns::find_common(const char *name) const {
    auto it = commons.find(name);
    if (it != commons.end()) {
      return 1;
    } else {
      return 0;
    }
  }

  int CLBuiltIns::find_wrapper(Func &func) const {
    auto it = wrappers.find(func);
    if (it != wrappers.end()) {
      func.ret = it->ret;
      return 1;
    } else {
      return 0;
    }
  }

  int CLBuiltIns::find(const char *name, Func *func) {
    Func local_func;
    if (!func) {
      func = &local_func;
    }
    switch (demangle(name, func)) {
      case 0:
        if (find_common(name)) {
          func->name = name;
          return 1;
        } else {
          return 0;
        }
      case 1:
        //outs() << func->to_string() << "\n";
        return find_wrapper(*func);
      case -1:
      default:
        return -1;
    }
  }

  bool CLBuiltIns::printDefinition(
    raw_ostream &Out,
    const Func &func,
    Function *F,
    std::function<std::string(int)> GetValueName,
    std::function<std::string(Type *)> GetTypeName
  ) const {
    auto cf = commons.find(func.name);
    if (cf != commons.end()) {
      // Common function
      Out << cf->second;
      return true;
    }

    // OpenCL builtin
    if (wrappers.find(func) == wrappers.end() || F->arg_size() != func.args.size()) {
      return false;
    }
    Out << "  return ";
    Type *Ty = F->getReturnType();
    if (dyn_cast<VectorType>(Ty)) {
      Out << "convert_" << GetTypeName(Ty) << "(";
    } else {
      Out << "(" << GetTypeName(Ty) << ")(";
    }
    Out << func.name << "(";
    int i = 0;
    for (auto &a : F->args()) {
      if (i > 0) {
        Out << ", ";
      }
      if (dyn_cast<VectorType>(a.getType())) {
        Out << "convert_" << func.args[i];
      } else {
        Out << "(" << func.args[i] << ")";
      }
      Out << "(" << GetValueName(i) << ")";
      i += 1;
    }
    Out << "));\n";

    return true;
  }

  bool CLBuiltIns::add_common(const std::string &name, const std::string &body) {
    return commons.insert(std::make_pair(name, body)).second;
  }

  bool CLBuiltIns::add_wrapper(const Func &func) {
    return wrappers.insert(func).second;
  }

  int CLBuiltIns::add_wrappers(const std::initializer_list<Func> &list) {
    int n = 0;
    for (const Func &f : list) {
      n += add_wrapper(f);
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

  std::vector<std::pair<std::string, std::string>> zip(
    const std::vector<std::string> &a,
    const std::vector<std::string> &b
  ) {
    int n = std::min(a.size(), b.size());
    std::vector<std::pair<std::string, std::string>> r;
    r.reserve(n);
    for (int i = 0; i < n; ++i) {
      r.push_back(std::make_pair(a[i], b[i]));
    }
    return r;
  }

  CLBuiltIns::CLBuiltIns() {
    // Common functions
    add_common(
      "memset",
      "  for (uint i = 0; i < c; ++i) {\n"
      "    a[i] = b;\n"
      "  }\n"
      "  return a;\n"
    );
    add_common(
      "memcpy",
      "  for (uint i = 0; i < c; ++i) {\n"
      "    a[i] = b[i];\n"
      "  }\n"
      "  return a;\n"
    );
    add_common(
      "mod",
      // FIXME: Handle negative numbers
      "  return a % b;\n"
    );

    // OpenCL built-ins wrappers
    std::vector<std::string> dim{"2", "3", "4", "8", "16"};
    std::vector<std::string> gendim = "" + dim;
    std::vector<std::string> typeis{"char", "short", "int", "long"};
    std::vector<std::string> typeiu = "u"*typeis;
    std::vector<std::string> typei = typeis + typeiu;
    std::vector<std::string> typef{"float", "double"};
    std::vector<std::string> type = typei + typef;
    std::vector<std::string> addrspace{" __private", " __global", " __constant", " __local", ""};

    // Work-Item Functions
    add_wrappers({
      Func("uint", "get_work_dim", {}),
      Func("size_t", "get_global_size", { "uint" }),
      Func("size_t", "get_global_id", { "uint" }),
      Func("size_t", "get_local_size", { "uint" }),
      Func("size_t", "get_enqueued_local_size", { "uint" }),
      Func("size_t", "get_local_id", { "uint" }),
      Func("size_t", "get_num_groups", { "uint" }),
      Func("size_t", "get_group_id", { "uint" }),
      Func("size_t", "get_global_offset", { "uint" }),
      Func("size_t", "get_global_linear_id", {}),
      Func("size_t", "get_local_linear_id", {}),
    });

    // Math Functions
    for (std::string gd : gendim) {
      for (std::string tf : typef) {
        std::string gtf = tf + gd;
        add_wrappers({
          Func(gtf, "acos", { gtf }),
          Func(gtf, "acosh", { gtf }),
          Func(gtf, "acospi", { gtf }),
          Func(gtf, "asin", { gtf }),
          Func(gtf, "asinh", { gtf }),
          Func(gtf, "asinpi", { gtf }),
          Func(gtf, "atan", { gtf }),
          Func(gtf, "atan2", { gtf, gtf }),
          Func(gtf, "atanh", { gtf }),
          Func(gtf, "atanpi", { gtf }),
          Func(gtf, "atan2pi", { gtf, gtf }),
          Func(gtf, "cbrt", { gtf }),
          Func(gtf, "ceil", { gtf }),
          Func(gtf, "copysign", { gtf, gtf }),
          Func(gtf, "cos", { gtf }),
          Func(gtf, "cosh", { gtf }),
          Func(gtf, "cospi", { gtf }),
          Func(gtf, "erfc", { gtf }),
          Func(gtf, "erf", { gtf }),
          Func(gtf, "exp", { gtf }),
          Func(gtf, "exp2", { gtf }),
          Func(gtf, "exp10", { gtf }),
          Func(gtf, "expm1", { gtf }),
          Func(gtf, "fabs", { gtf }),
          Func(gtf, "fdim", { gtf, gtf }),
          Func(gtf, "floor", { gtf }),
          Func(gtf, "fma", { gtf, gtf, gtf }),
          Func(gtf, "fmax", { gtf, gtf }),
          Func(gtf, "fmax", { gtf, tf }),
          Func(gtf, "fmin", { gtf, gtf }),
          Func(gtf, "fmin", { gtf, tf }),
          Func(gtf, "fmod", { gtf, gtf }),
          Func(gtf, "fract", { gtf, gtf+"*" }),
          Func(gtf, "frexp", { gtf, "int"+gd+"*" }),
          Func(gtf, "hypot", { gtf, gtf }),
          Func("int"+gd, "ilogb", { gtf }),
          Func(gtf, "ldexp", { gtf, "int"+gd }),
          Func(gtf, "lgamma", { gtf }),
          Func(gtf, "lgamma_r", { gtf, "int"+gd+"*" }),
          Func(gtf, "log", { gtf }),
          Func(gtf, "log2", { gtf }),
          Func(gtf, "log10", { gtf }),
          Func(gtf, "log1p", { gtf }),
          Func(gtf, "logb", { gtf }),
          Func(gtf, "mad", { gtf, gtf, gtf }),
          Func(gtf, "maxmag", { gtf, gtf }),
          Func(gtf, "minmag", { gtf, gtf }),
          Func(gtf, "modf", { gtf, gtf+"*" }),
          Func(gtf, "nan", { "uint"+gd }),
          Func(gtf, "nan", { "ulong"+gd }),
          Func(gtf, "nextafter", { gtf, gtf }),
          Func(gtf, "pow", { gtf, gtf }),
          Func(gtf, "pown", { gtf, "int"+gd }),
          Func(gtf, "powr", { gtf, gtf }),
          Func(gtf, "remainder", { gtf, gtf }),
          Func(gtf, "remquo", { gtf, gtf, "int"+gd+"*" }),
          Func(gtf, "rint", { gtf }),
          Func(gtf, "rootn", { gtf, "int"+gd }),
          Func(gtf, "round", { gtf }),
          Func(gtf, "rsqrt", { gtf }),
          Func(gtf, "sqrt", { gtf }),
          Func(gtf, "sin", { gtf }),
          Func(gtf, "sincos", { gtf, gtf+"*" }),
          Func(gtf, "sinh", { gtf }),
          Func(gtf, "sinpi", { gtf }),
          Func(gtf, "tan", { gtf }),
          Func(gtf, "tanh", { gtf }),
          Func(gtf, "tanpi", { gtf }),
          Func(gtf, "tgamma", { gtf }),
          Func(gtf, "trunc", { gtf }),
        });
      }
    }

    // Integer Functions
    for (std::string gd : gendim) {
      for (std::string tis : typeis) {
        std::string tiu = "u"+tis;
        add_wrappers({
          Func(tiu+gd, "abs", { tiu+gd }),
          Func(tiu+gd, "abs", { tis+gd }),
          Func(tiu+gd, "abs_diff", { tiu+gd, tiu+gd }),
          Func(tiu+gd, "abs_diff", { tis+gd, tis+gd }),
        });
      }
      for (std::string ti : typei) {
        add_wrappers({
          Func(ti+gd, "add_sat", { ti+gd, ti+gd }),
          Func(ti+gd, "hadd", { ti+gd, ti+gd }),
          Func(ti+gd, "rhadd", { ti+gd, ti+gd }),
          Func(ti+gd, "clamp", { ti+gd, ti+gd, ti+gd }),
          Func(ti+gd, "clamp", { ti+gd, ti+gd, ti }),
          Func(ti+gd, "clamp", { ti+gd, ti, ti+gd }),
          Func(ti+gd, "clamp", { ti+gd, ti, ti }),
          Func(ti+gd, "clz", { ti+gd }),
          Func(ti+gd, "ctz", { ti+gd }),
          Func(ti+gd, "mad_hi", { ti+gd, ti+gd, ti+gd }),
          Func(ti+gd, "mad_sat", { ti+gd, ti+gd, ti+gd }),
          Func(ti+gd, "max", { ti+gd, ti+gd }),
          Func(ti+gd, "max", { ti+gd, ti }),
          Func(ti+gd, "min", { ti+gd, ti+gd }),
          Func(ti+gd, "min", { ti+gd, ti }),
          Func(ti+gd, "mul_hi", { ti+gd, ti+gd }),
          Func(ti+gd, "rotate", { ti+gd, ti+gd }),
          Func(ti+gd, "sub_sat", { ti+gd, ti+gd }),
          Func(ti+gd, "popcount", { ti+gd }),
        });
      }
      for (std::string s : {"", "u"}) {
        add_wrappers({
          Func(s+"short"+gd, "upsample", { s+"char"+gd, "uchar"+gd }),
          Func(s+"int"+gd, "upsample", { s+"short"+gd, "ushort"+gd }),
          Func(s+"long"+gd, "upsample", { s+"int"+gd, "uint"+gd }),
        });
      }
    }

    // Common Functions
    for (std::string gd : gendim) {
      for (std::string tf : typef) {
        add_wrappers({
          Func(tf+gd, "clamp", { tf+gd, tf+gd, tf+gd }),
          Func(tf+gd, "clamp", { tf+gd, tf, tf }),
          Func(tf+gd, "degrees", { tf+gd }),
          Func(tf+gd, "max", { tf+gd, tf+gd }),
          Func(tf+gd, "max", { tf+gd, tf }),
          Func(tf+gd, "min", { tf+gd, tf+gd }),
          Func(tf+gd, "min", { tf+gd, tf }),
          Func(tf+gd, "mix", { tf+gd, tf+gd, tf+gd }),
          Func(tf+gd, "mix", { tf+gd, tf+gd, tf }),
          Func(tf+gd, "radians", { tf+gd }),
          Func(tf+gd, "step", { tf+gd, tf+gd }),
          Func(tf+gd, "step", { tf, tf+gd }),
          Func(tf+gd, "smoothstep", { tf+gd, tf+gd, tf+gd }),
          Func(tf+gd, "smoothstep", { tf, tf, tf+gd }),
          Func(tf+gd, "sign", { tf+gd }),
        });
      }
    }

    // Geometric Functions
    for (std::string tf : typef) {
      for (std::string d : {"3", "4"}) {
        add_wrappers({
          Func(tf+d, "cross", { tf+d, tf+d }),
        });
      }
      for (std::string d : dim) {
        add_wrappers({
          Func(tf, "dot", { tf+d, tf+d }),
          Func(tf, "distance", { tf+d, tf+d }),
          Func(tf, "length", { tf+d }),
          Func(tf+d, "normalize", { tf+d }),
          Func(tf, "fast_distance", { tf+d, tf+d }),
          Func(tf, "fast_length", { tf+d }),
          Func(tf+d, "fast_normalize", { tf+d }),
        });
      }
    }

    // Relational Function
    for (std::string d : dim) {
      for (auto p : zip({"int", "long"}, {"float", "double"})) {
        add_wrappers({
          Func(p.first+d, "isequal", { p.second+d, p.second+d }),
          Func(p.first+d, "isnotequal", { p.second+d, p.second+d }),
          Func(p.first+d, "isgreater", { p.second+d, p.second+d }),
          Func(p.first+d, "isgreaterequal", { p.second+d, p.second+d }),
          Func(p.first+d, "isless", { p.second+d, p.second+d }),
          Func(p.first+d, "islessequal", { p.second+d, p.second+d }),
          Func(p.first+d, "islessgreater", { p.second+d, p.second+d }),
          Func(p.first+d, "isfinite", { p.second+d }),
          Func(p.first+d, "isinf", { p.second+d }),
          Func(p.first+d, "isnan", { p.second+d }),
          Func(p.first+d, "isnormal", { p.second+d }),
          Func(p.first+d, "isordered", { p.second+d, p.second+d }),
          Func(p.first+d, "isunordered", { p.second+d, p.second+d }),
          Func(p.first+d, "signbit", { p.second+d }),
        });
      }
    }
    for (std::string tf : typef) {
      add_wrappers({
        Func("int", "isequal", { tf, tf }),
        Func("int", "isnotequal", { tf, tf }),
        Func("int", "isgreater", { tf, tf }),
        Func("int", "isgreaterequal", { tf, tf }),
        Func("int", "isless", { tf, tf }),
        Func("int", "islessequal", { tf, tf }),
        Func("int", "islessgreater", { tf, tf }),
        Func("int", "isfinite", { tf }),
        Func("int", "isinf", { tf }),
        Func("int", "isnan", { tf }),
        Func("int", "isnormal", { tf }),
        Func("int", "isordered", { tf, tf }),
        Func("int", "isunordered", { tf, tf }),
        Func("int", "signbit", { tf }),
      });
    }
    for(std::string gtis : typeis*gendim) {
      add_wrappers({
        Func("int", "any", { gtis }),
        Func("int", "all", { gtis }),
      });
    }
    for (std::string gd : gendim) {
      for (std::string t : type) {
        add_wrappers({
          Func(t+gd, "bitselect", { t+gd, t+gd, t+gd }),
        });
        for (std::string ti : typei) {
          add_wrappers({
            Func(t+gd, "select", { t+gd, t+gd, ti+gd }),
          });
        }
      }
    }

    // Vector Data Load and Store Functions
    for (std::string t : type) {
      for (std::string d : dim) {
        std::string vt = t + d;
        for (std::string as : addrspace) {
          add_wrappers({
            Func(vt, "vload"+d, {"uint", t+" const"+as+"*"}),
            Func("void", "vstore"+d, {vt, "uint", t+as+"*"}),
          });
        }
      }
    }

    // Explicit conversions
    for (std::string d : dim) {
      for (std::string sgt : type) {
        for (std::string dgt : type) {
          add_wrappers({
            Func(dgt+d, "convert_"+dgt+d, { sgt+d }),
          });
          // TODO: `_sat`, {`_rte`, `_rtz`, `_rtp`, `_rtn`}
        }
      }
    }

    // TODO:
    // Synchronization Functions
    // Async Copy and Prefetch
    // Atomic Functions
    // Miscellaneous Vector Functions
    // Image Read and Write Functions
    // Work-group Functions
    // Reinterpreting Types
  }
} // namespace llvm
