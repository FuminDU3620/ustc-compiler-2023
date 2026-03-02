#include "BasicBlock.hpp"
#include "Constant.hpp"
#include "Function.hpp"
#include "IRBuilder.hpp"
#include "Module.hpp"
#include "Type.hpp"
#include "Value.hpp"

#include <iostream>
#include <memory>
#include <vector>

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
    
    std::vector<Type *> Int(1, Int32Type);
    auto calleeFunTy = FunctionType::get(Int32Type, Int);
    auto calleeFun = Function::create(calleeFunTy, "callee", module);
    auto bb = BasicBlock::create(module, "entry", calleeFun);
    builder->set_insert_point(bb);

    auto aAlloca = builder->create_alloca(Int32Type);

    std::vector<Value *> args;
    for (auto &arg : calleeFun->get_args()) {
        args.push_back(&arg);
    }

    builder->create_store(args[0], aAlloca);
    auto aLoad = builder->create_load(aAlloca);
    auto mul = builder->create_imul(aLoad, CONST_INT(2));

    builder->create_ret(mul);

    // 接下来创建 main 函数
    auto mainFun =
        Function::create(FunctionType::get(Int32Type, {}), "main", module);
    // 创建 main 函数的起始 bb
    bb = BasicBlock::create(module, "entry", mainFun);
    // 将 builder 插入指令的位置调整至 main 函数起始 bb 上
    builder->set_insert_point(bb);

    auto call = builder->create_call(calleeFun, {CONST_INT(110)});
    builder->create_ret(call);

    std::cout << module->print();
    delete module;
    return 0;
}
