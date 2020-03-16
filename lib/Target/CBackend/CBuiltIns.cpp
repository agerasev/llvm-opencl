#include "CBuiltIns.h"

static const char * const _builtins[] = {
  "get_global_id(unsigned int)",
  "vload2(unsigned int, float const AS1*)",
  "vload3(unsigned int, float const AS1*)",
  "vload4(unsigned int, float const AS1*)",
  "vstore2(float vector[2], unsigned int, float AS1*)",
  "vstore3(float vector[3], unsigned int, float AS1*)",
  "vstore4(float vector[4], unsigned int, float AS1*)",
};

namespace llvm {

  CBuiltIns::CBuiltIns() {
    for (size_t i = 0; i < sizeof(_builtins)/sizeof(_builtins[0]); ++i) {
      set.insert(std::string(_builtins[i]));
    }
  }

  bool CBuiltIns::isBuiltIn(const char *full_name) const {
    return set.find(full_name) != set.end();
  }

} // namespace llvm
