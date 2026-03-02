#include "BasicBlock.hpp"
#include "Constant.hpp"
#include "Function.hpp"
#include "IRBuilder.hpp"
#include "Module.hpp"
#include "Type.hpp"

#include <iostream>
#include <memory>

// 定义一个从常数值获取/创建 ConstantInt 类实例化的宏，方便多次调用
#define CONST_INT(num) ConstantInt::get(num, module)

// 定义一个从常数值获取/创建 ConstantFP 类实例化的宏，方便多次调用
#define CONST_FP(num) ConstantFP::get(num, module)

int main() {
    // 创建一个 Module 实例
    auto module = new Module();
    // 创建一个 IRBuilder 实例（后续创建指令均使用此实例操作）
    auto builder = new IRBuilder(nullptr, module);
    Type *Int32Type = module->get_int32_type();
    Type *FloatType = module->get_float_type();

    auto mainFun =
        Function::create(FunctionType::get(Int32Type, {}), "main", module);
    // 创建 main 函数的起始 bb
    auto bb = BasicBlock::create(module, "entry", mainFun);
    // 将 builder 插入指令的位置调整至 main 函数起始 bb 上
    builder->set_insert_point(bb);
    // 用 builder 创建 alloca 指令，为函数返回值分配空间
    auto retAlloca = builder->create_alloca(Int32Type);
    // main 函数默认 ret 0
    builder->create_store(CONST_INT(0), retAlloca);

    auto aAlloca = builder->create_alloca(FloatType);

    builder->create_store(CONST_FP(-5.555), aAlloca);
    auto aLoad = builder->create_load(aAlloca);

    auto fcmp = builder->create_fcmp_gt(aLoad, CONST_FP(1));
    
    auto trueBB = BasicBlock::create(module, "trueBB", mainFun);
    auto retBB = BasicBlock::create(module, "", mainFun);

    builder->create_cond_br(fcmp, trueBB, retBB);

    builder->set_insert_point(trueBB);
    builder->create_store(CONST_INT(233), retAlloca);
    builder->create_br(retBB);

    builder->set_insert_point(retBB);
    auto retVal = builder->create_load(retAlloca);
    builder->create_ret(retVal);

    std::cout << module->print();
    delete module;
    return 0;
}
