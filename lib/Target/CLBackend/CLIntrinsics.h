#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>

#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>

namespace llvm_opencl {
  using namespace llvm;
  
  class CLIntrinsic {
  public:
    virtual ~CLIntrinsic() = default;
    virtual void printDefinition(
      raw_ostream &Out, FunctionType *funT,
      std::string OpName, std::function<void(raw_ostream&, Type*, bool)> printTy
    ) const = 0;
  };

  class CLIntrinsicMap {
  private:
    std::unordered_map<unsigned, std::unique_ptr<CLIntrinsic>> intrinsics;
    void insert(std::pair<unsigned, std::unique_ptr<CLIntrinsic>> &&value);
  public:
    CLIntrinsicMap();
    const CLIntrinsic *get(unsigned Opcode) const;
    bool hasImpl(unsigned Opcode) const;
  };

} // namespace llvm_opencl
