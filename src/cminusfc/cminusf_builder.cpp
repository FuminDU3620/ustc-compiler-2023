#include "cminusf_builder.hpp"
#include "BasicBlock.hpp"
#include "Constant.hpp"
#include "Function.hpp"
#include "GlobalVariable.hpp"
#include "Instruction.hpp"
#include "Type.hpp"
#include "Value.hpp"
#include "ast.hpp"
#include "logging.hpp"
#include <cmath>
#include <cstddef>
#include <endian.h>
#include <locale>
#include <stdexcept>

#define CONST_FP(num) ConstantFP::get((float)num, module.get())
#define CONST_INT(num) ConstantInt::get(num, module.get())

// types
Type *VOID_T;
Type *INT1_T;
Type *INT32_T;
Type *INT32PTR_T;
Type *FLOAT_T;
Type *FLOATPTR_T;

/*
 * use CMinusfBuilder::Scope to construct scopes
 * scope.enter: enter a new scope
 * scope.exit: exit current scope
 * scope.push: add a new binding to current scope
 * scope.find: find and return the value bound to the name
 */

 bool promote(IRBuilder *builder, Value **l_val_p, Value **r_val_p) {
    bool is_int = false;
    auto &l_val = *l_val_p;
    auto &r_val = *r_val_p;
    if (l_val->get_type() == r_val->get_type()) {
        is_int = l_val->get_type()->is_integer_type();
    } else {
        if (l_val->get_type()->is_integer_type()) {
            l_val = builder->create_sitofp(l_val, FLOAT_T);
        } else {
            r_val = builder->create_sitofp(r_val, FLOAT_T);
        }
    }
    return is_int;
}

Value* CminusfBuilder::visit(ASTProgram &node) {
    VOID_T = module->get_void_type();
    INT1_T = module->get_int1_type();
    INT32_T = module->get_int32_type();
    INT32PTR_T = module->get_int32_ptr_type();
    FLOAT_T = module->get_float_type();
    FLOATPTR_T = module->get_float_ptr_type();

    Value *ret_val = nullptr;
    for (auto &decl : node.declarations) {
        ret_val = decl->accept(*this);
    }
    return ret_val;
}

Value* CminusfBuilder::visit(ASTNum &node) {
    // TODO: This function is empty now.
    // Add some code here.
    if (node.type == TYPE_INT) {
        return CONST_INT(node.i_val);
    } else {
        return CONST_FP(node.f_val);
    }
    return nullptr;
}

Value* CminusfBuilder::visit(ASTVarDeclaration &node) {
    Type *base_type = (node.type == TYPE_INT) 
        ? static_cast<Type*>(module->get_int32_type()) 
        : static_cast<Type*>(module->get_float_type());

    if (node.num == nullptr) {
        if (scope.in_global()) {
            Constant *init = ConstantZero::get(base_type, module.get());
            GlobalVariable *gvar = GlobalVariable::create(node.id, module.get(), base_type, false, init);
            scope.push(node.id, gvar);
        } else {
            AllocaInst *local = builder->create_alloca(base_type);
            scope.push(node.id, local);
        }
    } else {
        ArrayType *arr_type = ArrayType::get(base_type, node.num->i_val);
        if (scope.in_global()) {
            Constant *init = ConstantZero::get(arr_type, module.get());
            GlobalVariable *gvar = GlobalVariable::create(node.id, module.get(), arr_type, false, init);
            scope.push(node.id, gvar);
        } else {
            AllocaInst *local = builder->create_alloca(arr_type);
            scope.push(node.id, local);
        }
    }
    return nullptr;
}

Value* CminusfBuilder::visit(ASTFunDeclaration &node) {
    FunctionType *fun_type;
    Type *ret_type;
    std::vector<Type *> param_types;
    if (node.type == TYPE_INT)
        ret_type = INT32_T;
    else if (node.type == TYPE_FLOAT)
        ret_type = FLOAT_T;
    else
        ret_type = VOID_T;

    for (auto &param : node.params) {
        if (param->type == TYPE_INT) {
            if (param->isarray) {
                param_types.push_back(INT32PTR_T);
            } else {
                param_types.push_back(INT32_T);
            }
        } else {
            if (param->isarray) {
                param_types.push_back(FLOATPTR_T);
            } else {
                param_types.push_back(FLOAT_T);
    }
        }
    }

    fun_type = FunctionType::get(ret_type, param_types);
    auto func = Function::create(fun_type, node.id, module.get());
    scope.push(node.id, func);
    context.func = func;
    auto funBB = BasicBlock::create(module.get(), "entry", func);
    builder->set_insert_point(funBB);
    scope.enter();
    context.entered_scope = true;
    std::vector<Value *> args;
    for (auto &arg : func->get_args()) {
        args.push_back(&arg);
    }
    for (unsigned int i = 0; i < node.params.size(); ++i) {
        Value *alloc = nullptr;
        if (node.params[i]->isarray) {
            alloc = builder->create_alloca(
                node.params[i]->type == TYPE_INT ? INT32PTR_T : FLOATPTR_T
            );
        } else {
            alloc = builder->create_alloca(
                node.params[i]->type == TYPE_INT ? INT32_T : FLOAT_T
            );
        }
        builder->create_store(args[i], alloc);
        scope.push(node.params[i]->id, alloc);
    }
    node.compound_stmt->accept(*this);
    if (!builder->get_insert_block()->is_terminated())
    {
        if (context.func->get_return_type()->is_void_type())
            builder->create_void_ret();
        else if (context.func->get_return_type()->is_float_type())
            builder->create_ret(CONST_FP(0.));
        else
            builder->create_ret(CONST_INT(0));
    }
    scope.exit();
    return nullptr;
}

Value* CminusfBuilder::visit(ASTParam &node) {
    // TODO: This function is empty now.
    // Add some code here.
    // 检查1：Cminusf不支持void类型的参数
    if (node.type == TYPE_VOID) {
        // 抛出语义错误（可替换为你项目中的错误处理逻辑）
        std::cerr << "Error: void type is not allowed for parameters (param name: " 
                  << node.id << ")" << std::endl;
        // 终止编译或返回错误标记（根据项目框架调整）
        exit(1);
    }

    // 检查2：参数名不能为空（语法合法性检查）
    if (node.id.empty()) {
        std::cerr << "Error: parameter name cannot be empty" << std::endl;
        exit(1);
    }
    return nullptr;
}

Value* CminusfBuilder::visit(ASTCompoundStmt &node) {
    bool should_exit = !context.entered_scope;
    if (!context.entered_scope) {
        scope.enter();
    } else {
        context.entered_scope = false;
    }

    for (auto &decl : node.local_declarations) {
        decl->accept(*this);
    }

    for (auto &stmt : node.statement_list) {
        stmt->accept(*this);
        if (builder->get_insert_block()->is_terminated()) {
            break;
        }
    }

    if (should_exit) {
        scope.exit();
    }
    return nullptr;
}

Value* CminusfBuilder::visit(ASTExpressionStmt &node) {
    // TODO: This function is empty now.
    // Add some code here.
    if (node.expression != nullptr) {
        node.expression->accept(*this);
    }

    return nullptr;
}

Value* CminusfBuilder::visit(ASTSelectionStmt &node) {
    Value *ret_val  = node.expression->accept(*this);
    if (ret_val  == nullptr) {
        std::cerr << "Error: Selection statement condition is invalid (null value)" << std::endl;
        exit(1);
    }

    Value *cond_val = nullptr;
    BasicBlock* trueBB = BasicBlock::create(module.get(), "if.true", context.func);
    BasicBlock* elseBB = nullptr;
    BasicBlock* mergeBB = BasicBlock::create(module.get(), "if.merge", context.func);

    Type* cond_type = ret_val->get_type();
    if (cond_type->is_integer_type()) {
        cond_val = builder->create_icmp_ne(ret_val, CONST_INT(0));
    } else if (cond_type->is_float_type()) {
        cond_val = builder->create_fcmp_ne(ret_val, CONST_FP(0.));
    } else {
        std::cerr << "Error: Selection statement condition must be int/float" << std::endl;
        exit(1);
    }

    if (!cond_val) {
        std::cerr << "Error: Failed to create condition value for selection statement" << std::endl;
        exit(1);
    }

    if (node.else_statement == nullptr) {
        builder->create_cond_br(cond_val, trueBB, mergeBB);
    } else {
        elseBB = BasicBlock::create(module.get(), "if.else", context.func);
        builder->create_cond_br(cond_val, trueBB, elseBB);
    }

    builder->set_insert_point(trueBB);
    node.if_statement->accept(*this);
    if (!builder->get_insert_block()->is_terminated()) {
        builder->create_br(mergeBB);
    }

    if (node.else_statement != nullptr) {
        builder->set_insert_point(elseBB);
        node.else_statement->accept(*this);
        if (!builder->get_insert_block()->is_terminated()) {
            builder->create_br(mergeBB);
        }
    }

    builder->set_insert_point(mergeBB);
    return nullptr;
}

Value* CminusfBuilder::visit(ASTIterationStmt &node) {
    BasicBlock* condBB = BasicBlock::create(module.get(), "while_cond", context.func);

    if (!builder->get_insert_block()->is_terminated()) {
        builder->create_br(condBB);
    }
    builder->set_insert_point(condBB);

    Value* expr_val = node.expression->accept(*this);
    BasicBlock* bodyBB = BasicBlock::create(module.get(), "while_body", context.func);
    BasicBlock* exitBB = BasicBlock::create(module.get(), "while_exit", context.func);

    Value* cond_val;
    if (expr_val->get_type()->is_integer_type()) {
        cond_val = builder->create_icmp_ne(expr_val, CONST_INT(0));
    } else {
        cond_val = builder->create_fcmp_ne(expr_val, CONST_FP(0.));
    }

    builder->create_cond_br(cond_val, bodyBB, exitBB);

    builder->set_insert_point(bodyBB);
    node.statement->accept(*this);

    if (!builder->get_insert_block()->is_terminated()) {
        builder->create_br(condBB);
    }

    builder->set_insert_point(exitBB);
    return nullptr;
}

Value* CminusfBuilder::visit(ASTReturnStmt &node) {
    if (node.expression == nullptr) {
        builder->create_void_ret();
        return nullptr;
    } else {
        // TODO: The given code is incomplete.
        // You need to solve other return cases (e.g. return an integer).
        Value* ret_val = node.expression->accept(*this);
        if (ret_val == nullptr) {
            std::cerr << "Error: Return expression generates invalid value (param name: " << std::endl;
            exit(1);
        }

        Type* func_ret_type = context.func->get_return_type();

        if (func_ret_type != ret_val->get_type()) {
            if (func_ret_type->is_integer_type()) {
                ret_val = builder->create_fptosi(ret_val, INT32_T);
            } else {
                ret_val = builder->create_sitofp(ret_val, FLOAT_T);
            }
        }
        builder->create_ret(ret_val);
    }
    return nullptr;
}

Value* CminusfBuilder::visit(ASTVar &node) {
    Value* baseAddr = this->scope.find(node.id);
    Type* alloctype = nullptr;
    
    if (auto* alloca = dynamic_cast<AllocaInst*>(baseAddr)) {
        alloctype = alloca->get_alloca_type();
    } else if (auto* global = dynamic_cast<GlobalVariable*>(baseAddr)) {
        alloctype = global->get_type()->get_pointer_element_type();
    } else {
        assert(false && "Unexpected variable base address type");
        return nullptr;
    }

    if(node.expression) {
        bool original_require_lvalue = context.require_lvalue;
        context.require_lvalue = false;
        auto idx = node.expression->accept(*this);
        context.require_lvalue = original_require_lvalue;

        if (idx->get_type()->is_float_type()) {
            idx = builder->create_fptosi(idx, INT32_T);
        } else if(idx->get_type()->is_int1_type()){
            idx = builder->create_zext(idx, INT32_T);
        }
        auto right_bb = BasicBlock::create(module.get(), "", context.func);
        auto wrong_bb = BasicBlock::create(module.get(), "", context.func);
        
        auto cond_neg = builder->create_icmp_ge(idx, CONST_INT(0));
        builder->create_cond_br(cond_neg,right_bb, wrong_bb);

        auto wrong = scope.find("neg_idx_except");
        builder->set_insert_point(wrong_bb);
        builder->create_call(wrong, {});
        builder->create_br(right_bb);
        builder->set_insert_point(right_bb);
        
        if(context.require_lvalue) {
            if(alloctype->is_pointer_type()) {
                baseAddr = builder->create_load(baseAddr);
                baseAddr = builder->create_gep(baseAddr,{idx});
            } else if(alloctype->is_array_type()){ 
                baseAddr = builder->create_gep(baseAddr,{CONST_INT(0),idx});
            }
            context.require_lvalue = false;
            return baseAddr;
        } else {
            if(alloctype->is_pointer_type()){
                baseAddr = builder->create_load(baseAddr);
                baseAddr = builder->create_gep(baseAddr,{idx});
            } else if(alloctype->is_array_type()){ 
                baseAddr = builder->create_gep(baseAddr,{CONST_INT(0),idx});
            }
            baseAddr = builder->create_load(baseAddr);
            return baseAddr;
        }
    } else {
        if (context.require_lvalue) {
            context.require_lvalue = false;
            return baseAddr;
        } else {
            if(alloctype->is_array_type()){
                return builder->create_gep(baseAddr, {CONST_INT(0),CONST_INT(0)});
            } else {
                return builder->create_load(baseAddr);
            }
            
        }
    }
    return nullptr;
}

Value* CminusfBuilder::visit(ASTAssignExpression &node) {
    auto *expr_result = node.expression->accept(*this);
    context.require_lvalue = true;
    auto *var_addr = node.var->accept(*this);
    if (var_addr->get_type()->get_pointer_element_type() !=
        expr_result->get_type()) {
        if (expr_result->get_type() == INT32_T) {
            expr_result = builder->create_sitofp(expr_result, FLOAT_T);
        } else {
            expr_result = builder->create_fptosi(expr_result, INT32_T);
        }
    }
    builder->create_store(expr_result, var_addr);
    return expr_result;
}

Value* CminusfBuilder::visit(ASTSimpleExpression &node) {
    if (node.additive_expression_r == nullptr) {
        return node.additive_expression_l->accept(*this);
    }

    Value *left = node.additive_expression_l->accept(*this);
    Value *right = node.additive_expression_r->accept(*this);
    bool is_integer = promote(&*builder, &left, &right);
    Value *result = nullptr;

    switch (node.op) {
    case OP_LT:
        result = is_integer ? static_cast<Value*>(builder->create_icmp_lt(left, right))
                            : (builder->create_fcmp_lt(left, right));
        break;
    case OP_LE:
        result = is_integer ? static_cast<Value*>(builder->create_icmp_le(left, right))
                            : builder->create_fcmp_le(left, right);
        break;
    case OP_GE:
        result = is_integer ? static_cast<Value*>(builder->create_icmp_ge(left, right))
                            : builder->create_fcmp_ge(left, right);
        break;
    case OP_GT:
        result = is_integer ? static_cast<Value*>(builder->create_icmp_gt(left, right))
                            : builder->create_fcmp_gt(left, right);
        break;
    case OP_EQ:
        result = is_integer ? static_cast<Value*>(builder->create_icmp_eq(left, right))
                            : builder->create_fcmp_eq(left, right);
        break;
    case OP_NEQ:
        result = is_integer ? static_cast<Value*>(builder->create_icmp_ne(left, right))
                            : builder->create_fcmp_ne(left, right);
        break;
    }

    return builder->create_zext(result, INT32_T);
}

Value* CminusfBuilder::visit(ASTAdditiveExpression &node) {
    if (node.additive_expression == nullptr) {
        return node.term->accept(*this);
    }

    auto *l_val = node.additive_expression->accept(*this);
    auto *r_val = node.term->accept(*this);
    bool is_int = promote(&*builder, &l_val, &r_val);
    Value *ret_val = nullptr;
    switch (node.op) {
    case OP_PLUS:
        if (is_int) {
            ret_val = builder->create_iadd(l_val, r_val);
        } else {
            ret_val = builder->create_fadd(l_val, r_val);
        }
        break;
    case OP_MINUS:
        if (is_int) {
            ret_val = builder->create_isub(l_val, r_val);
        } else {
            ret_val = builder->create_fsub(l_val, r_val);
        }
        break;
    }
    return ret_val;
}

Value* CminusfBuilder::visit(ASTTerm &node) {
    if (node.term == nullptr) {
        return node.factor->accept(*this);
    }

    auto *l_val = node.term->accept(*this);
    auto *r_val = node.factor->accept(*this);
    bool is_int = promote(&*builder, &l_val, &r_val);

    Value *ret_val = nullptr;
    switch (node.op) {
    case OP_MUL:
        if (is_int) {
            ret_val = builder->create_imul(l_val, r_val);
        } else {
            ret_val = builder->create_fmul(l_val, r_val);
        }
        break;
    case OP_DIV:
        if (is_int) {
            ret_val = builder->create_isdiv(l_val, r_val);
        } else {
            ret_val = builder->create_fdiv(l_val, r_val);
        }
        break;
    }
    return ret_val;
}

Value* CminusfBuilder::visit(ASTCall &node) {
    auto *func = dynamic_cast<Function *>(scope.find(node.id));
    std::vector<Value *> args;
    auto param_type = func->get_function_type()->param_begin();
    for (auto &arg : node.args) {
        auto *arg_val = arg->accept(*this);
        // 仅支持 int 与 float 间的隐式类型转换
        if (!arg_val->get_type()->is_pointer_type() && *param_type != arg_val->get_type()) {
            if (arg_val->get_type()->is_integer_type()) {
                arg_val = builder->create_sitofp(arg_val, FLOAT_T);
            } else {
                arg_val = builder->create_fptosi(arg_val, INT32_T);
            }
        }
        args.push_back(arg_val);
        param_type++;
    }

    return builder->create_call(static_cast<Function *>(func), args);
}
