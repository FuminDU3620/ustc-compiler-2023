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
    // 从 Module 处取出 32 位整形 type 的实例
    Type *Int32Type = module->get_int32_type();

    auto mainFunTy = FunctionType::get(Int32Type, {});
    auto mainFun = Function::create(mainFunTy, "main", module);

    auto bb = BasicBlock::create(module, "entry", mainFun);
    builder->set_insert_point(bb);

    auto retAlloca = builder->create_alloca(Int32Type);
    // main 函数默认 ret 0
    builder->create_store(CONST_INT(0), retAlloca);

    auto aAlloca = builder->create_alloca(Int32Type);
    auto iAlloca = builder->create_alloca(Int32Type);

    builder->create_store(CONST_INT(10), aAlloca);
    builder->create_store(CONST_INT(0), iAlloca);

    auto iLoad = builder->create_load(iAlloca);
    auto icmp = builder->create_icmp_lt(iLoad, CONST_INT(10));
    auto trueBB = BasicBlock::create(module, "trueBB", mainFun);
    auto retBB = BasicBlock::create(module, "retBB", mainFun);
    builder->create_cond_br(icmp, trueBB, retBB);

    builder->set_insert_point(trueBB);
    iLoad = builder->create_load(iAlloca);
    auto add = builder->create_iadd(iLoad, CONST_INT(1));
    builder->create_store(add, iAlloca);

    iLoad = builder->create_load(iAlloca);
    auto aLoad = builder->create_load(aAlloca);
    add = builder->create_iadd(aLoad, iLoad);
    builder->create_store(add, aAlloca);
    
    iLoad = builder->create_load(iAlloca);
    icmp = builder->create_icmp_lt(iLoad, CONST_INT(10));
    builder->create_cond_br(icmp, trueBB, retBB);

    builder->set_insert_point(retBB);
    aLoad = builder->create_load(aAlloca);
    builder->create_store(aLoad, retAlloca);
    auto retVal = builder->create_load(retAlloca);
    builder->create_ret(retVal);

    std::cout << module->print();
    delete module;
    return 0;
}
