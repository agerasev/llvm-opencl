# LLVM-OpenCL

LLVM to OpenCL C Translator.

*Forked from [JuliaComputing/llvm-cbe](https://github.com/JuliaComputing/llvm-cbe).*

## Installation instructions

This version of the LLVM-OpenCL library works with LLVM 10.0. You will have to compile this version of LLVM before you try to use LLVM-OpenCL. This guide will walk you through the compilation and installation of both tools and show usage statements to verify that the LLVM-OpenCL library is compiled correctly.

### Building LLVM and Clang

LLVM-OpenCL relies on specific LLVM internals, and so it is best to use it with a specific revision of the LLVM development tree. Currently, LLVM-OpenCL works with the LLVM 10.0 release version and autotools.

Note: to convert OpenCL C to LLVM IR to run the tests, you will also need a OpenCL-to-LLVM compiler such as Clang. It is recommended to use Clang from the same LLVM repository revision in order to have compatible LLVM IR code.

The first step is to compile LLVM and Clang on your machine (this assumes an in-tree build, but out-of-tree will also work):

```bash
git clone https://github.com/llvm/llvm-project.git -b release/10.x
mkdir llvm/build
cd llvm/build
cmake .. -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_PROJECTS=clang
make
```

### Building LLVM-OpenCL

Next, download and compile LLVM-OpenCL from the same folder:

```bash
cd llvm-project/llvm/projects
git clone https://github.com/agerasev/llvm-opencl
cd ../build
cmake ..
make llvm-opencl
```

## Usage Examples

If LLVM-OpenCL compiles, you should be able to run example with the following commands.

```bash
# Add previously built LLVM-OpenCL and Clang to PATH
export PATH=$PWD/llvm-project/llvm/build/bin:$PATH

# Go to example program directory
cd llvm-opencl/test/examples/mandelbrot

# Generate LLVM IR code from OpenCL kernel source
clang -S -emit-llvm \
  --target=spir-unknown-unknown \
  -x cl -std=cl1.2 \
  -Xclang -finclude-default-header \
  -O3 \
  kernel.cl -o kernel.gen.ll

# Translate LLVM IR code back to OpenCL C
llvm-opencl kernel.gen.ll -o kernel.gen.cl

# Run translated code with OpenCL
python3 run.py kernel.gen.cl
# The rendered image should appear in the current directory
```

## Running tests

```bash
cd llvm-project/llvm/projects/llvm-opencl
python3 -m test
```
