// #include "llvm/IR/IRBuilder.h"
// #include "llvm/IR/LLVMContext.h"
// #include "llvm/IR/Module.h"
// #include "llvm/IR/Type.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include <cstdio>
#include <cstdint>

// static std::unique_ptr<llvm::LLVMContext> TheContext;
// static std::unique_ptr<llvm::Module> TheModule;
// static std::unique_ptr<llvm::IRBuilder<>> Builder;

llvm::ExitOnError ExitOnErr;

// clang++ hello.cpp $(llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native) -o hello
int main()
{
  uint32_t a = 1;
  uint32_t b = 1;
  uint32_t c = 0;

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();

  auto TheContext = std::make_unique<llvm::LLVMContext>();
  auto TheModule = std::make_unique<llvm::Module>("hello world jit", *TheContext);
  auto Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);
  auto FN = llvm::Function::Create(
       llvm::FunctionType::get(llvm::Type::getVoidTy(*TheContext), {}, false),
       llvm::Function::ExternalLinkage, "fn", TheModule.get());
  auto BB = llvm::BasicBlock::Create(*TheContext, "block");

  FN->getBasicBlockList().push_back(BB);
  Builder->SetInsertPoint(BB);

  // Code
  auto LocationA = llvm::ConstantInt::get(*TheContext, llvm::APInt(sizeof(uint32_t *)*8, reinterpret_cast<uint64_t>(&a), false));
  auto LocationB = llvm::ConstantInt::get(*TheContext, llvm::APInt(sizeof(uint32_t *)*8, reinterpret_cast<uint64_t>(&b), false));
  auto LocationC = llvm::ConstantInt::get(*TheContext, llvm::APInt(sizeof(uint32_t *)*8, reinterpret_cast<uint64_t>(&c), false));
  auto PtrA = Builder->CreateIntToPtr(LocationA, llvm::Type::getInt32PtrTy(*TheContext));
  auto PtrB = Builder->CreateIntToPtr(LocationB, llvm::Type::getInt32PtrTy(*TheContext));
  auto PtrC = Builder->CreateIntToPtr(LocationC, llvm::Type::getInt32PtrTy(*TheContext));
  auto A = Builder->CreateLoad(llvm::Type::getInt32Ty(*TheContext), PtrA);
  auto B = Builder->CreateLoad(llvm::Type::getInt32Ty(*TheContext), PtrB);
  auto C = Builder->CreateAdd(A, B);
  Builder->CreateStore(C, PtrC);
  Builder->CreateRet(nullptr);

  // Generate native code
  auto TheJIT = ExitOnErr(llvm::orc::LLJITBuilder().create());
  auto TSM = llvm::orc::ThreadSafeModule(std::move(TheModule), std::move(TheContext));
  ExitOnErr(TheJIT->addIRModule(std::move(TSM)));

  // Call newly-generated code
  auto fn_sym = ExitOnErr(TheJIT->lookup("fn"));
  void (*fn)() = (void (*)(void))(fn_sym.getAddress());
  fn();

  fprintf(stdout, "%d + %d = %d\n", a, b, c);
  return 0;
}
