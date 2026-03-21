#include "CodeGen.hpp"

#include "CodeGenUtil.hpp"
#include "Register.hpp"

#include <cstring>

void CodeGen::allocate() {
    // 备份 $ra $fp
    unsigned offset = PROLOGUE_OFFSET_BASE;

    // 为每个参数分配栈空间
    for (auto &arg : context.func->get_args()) {
        auto size = arg.get_type()->get_size();
        offset = offset + size;
        context.offset_map[&arg] = -static_cast<int>(offset);
    }

    // 为指令结果分配栈空间
    for (auto &bb : context.func->get_basic_blocks()) {
        for (auto &instr : bb.get_instructions()) {
            // 每个非 void 的定值都分配栈空间
            if (not instr.is_void()) {
                auto size = instr.get_type()->get_size();
                offset = offset + size;
                context.offset_map[&instr] = -static_cast<int>(offset);
            }
            // alloca 的副作用：分配额外空间
            if (instr.is_alloca()) {
                auto *alloca_inst = static_cast<AllocaInst *>(&instr);
                auto alloc_size = alloca_inst->get_alloca_type()->get_size();
                offset += alloc_size;
            }
        }
    }

    // 分配栈空间，需要是 16 的整数倍
    context.frame_size = ALIGN(offset, PROLOGUE_ALIGN);
}

void CodeGen::copy_stmt() {
    for (auto &succ : context.bb->get_succ_basic_blocks()) {
        for (auto &inst : succ->get_instructions()) {
            if (inst.is_phi()) {
                // 遍历后继块中 phi 的定值 bb
                for (unsigned i = 1; i < inst.get_operands().size(); i += 2) {
                    // phi 的定值 bb 是当前翻译块
                    if (inst.get_operand(i) == context.bb) {
                        auto *lvalue = inst.get_operand(i - 1);
                        if (lvalue->get_type()->is_float_type()) {
                            load_to_freg(lvalue, FReg::fa(0));
                            store_from_freg(&inst, FReg::fa(0));
                        } else {
                            load_to_greg(lvalue, Reg::a(0));
                            store_from_greg(&inst, Reg::a(0));
                        }
                        break;
                    }
                    // 如果没有找到当前翻译块，说明是 undef，无事可做
                }
            } else {
                break;
            }
        }
    }
}

void CodeGen::load_to_greg(Value *val, const Reg &reg) {
    assert(val->get_type()->is_integer_type() ||
           val->get_type()->is_pointer_type());

    if (auto *constant = dynamic_cast<ConstantInt *>(val)) {
        int32_t val = constant->get_value();
        if (IS_IMM_12(val)) {
            append_inst(ADDI WORD, {reg.print(), "$zero", std::to_string(val)});
        } else {
            load_large_int32(val, reg);
        }
    } else if (auto *global = dynamic_cast<GlobalVariable *>(val)) {
        append_inst(LOAD_ADDR, {reg.print(), global->get_name()});
    } else {
        load_from_stack_to_greg(val, reg);
    }
}

void CodeGen::load_large_int32(int32_t val, const Reg &reg) {
    int32_t high_20 = val >> 12; // si20
    uint32_t low_12 = val & LOW_12_MASK;
    append_inst(LU12I_W, {reg.print(), std::to_string(high_20)});
    append_inst(ORI, {reg.print(), reg.print(), std::to_string(low_12)});
}

void CodeGen::load_large_int64(int64_t val, const Reg &reg) {
    auto low_32 = static_cast<int32_t>(val & LOW_32_MASK);
    load_large_int32(low_32, reg);

    auto high_32 = static_cast<int32_t>(val >> 32);
    int32_t high_32_low_20 = (high_32 << 12) >> 12; // si20
    int32_t high_32_high_12 = high_32 >> 20;        // si12
    append_inst(LU32I_D, {reg.print(), std::to_string(high_32_low_20)});
    append_inst(LU52I_D,
                {reg.print(), reg.print(), std::to_string(high_32_high_12)});
}

void CodeGen::load_from_stack_to_greg(Value *val, const Reg &reg) {
    auto offset = context.offset_map.at(val);
    auto offset_str = std::to_string(offset);
    auto *type = val->get_type();
    if (IS_IMM_12(offset)) {
        if (type->is_int1_type()) {
            append_inst(LOAD BYTE, {reg.print(), "$fp", offset_str});
            append_inst("andi", {reg.print(), reg.print(), "1"});
        } else if (type->is_int32_type()) {
            append_inst(LOAD WORD, {reg.print(), "$fp", offset_str});
        } else { // Pointer
            append_inst(LOAD DOUBLE, {reg.print(), "$fp", offset_str});
        }
    } else {
        load_large_int64(offset, reg);
        append_inst(ADD DOUBLE, {reg.print(), "$fp", reg.print()});
        if (type->is_int1_type()) {
            append_inst(LOAD BYTE, {reg.print(), reg.print(), "0"});
            append_inst("andi", {reg.print(), reg.print(), "1"});
        } else if (type->is_int32_type()) {
            append_inst(LOAD WORD, {reg.print(), reg.print(), "0"});
        } else { // Pointer
            append_inst(LOAD DOUBLE, {reg.print(), reg.print(), "0"});
        }
    }
}

void CodeGen::store_from_greg(Value *val, const Reg &reg) {
    auto offset = context.offset_map.at(val);
    auto offset_str = std::to_string(offset);
    auto *type = val->get_type();
    if (IS_IMM_12(offset)) {
        if (type->is_int1_type()) {
            append_inst(STORE BYTE, {reg.print(), "$fp", offset_str});
        } else if (type->is_int32_type()) {
            append_inst(STORE WORD, {reg.print(), "$fp", offset_str});
        } else { // Pointer
            append_inst(STORE DOUBLE, {reg.print(), "$fp", offset_str});
        }
    } else {
        auto addr = Reg::t(8);
        load_large_int64(offset, addr);
        append_inst(ADD DOUBLE, {addr.print(), "$fp", addr.print()});
        if (type->is_int1_type()) {
            append_inst(STORE BYTE, {reg.print(), addr.print(), "0"});
        } else if (type->is_int32_type()) {
            append_inst(STORE WORD, {reg.print(), addr.print(), "0"});
        } else { // Pointer
            append_inst(STORE DOUBLE, {reg.print(), addr.print(), "0"});
        }
    }
}

void CodeGen::load_to_freg(Value *val, const FReg &freg) {
    assert(val->get_type()->is_float_type());
    if (auto *constant = dynamic_cast<ConstantFP *>(val)) {
        float val = constant->get_value();
        load_float_imm(val, freg);
    } else {
        auto offset = context.offset_map.at(val);
        auto offset_str = std::to_string(offset);
        if (IS_IMM_12(offset)) {
            append_inst(FLOAD SINGLE, {freg.print(), "$fp", offset_str});
        } else {
            auto addr = Reg::t(8);
            load_large_int64(offset, addr);
            append_inst(ADD DOUBLE, {addr.print(), "$fp", addr.print()});
            append_inst(FLOAD SINGLE, {freg.print(), addr.print(), "0"});
        }
    }
}

void CodeGen::load_float_imm(float val, const FReg &r) {
    int32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(val), "unexpected float size");
    std::memcpy(&bits, &val, sizeof(bits));
    load_large_int32(bits, Reg::t(8));
    append_inst(GR2FR WORD, {r.print(), Reg::t(8).print()});
}

void CodeGen::store_from_freg(Value *val, const FReg &r) {
    auto offset = context.offset_map.at(val);
    if (IS_IMM_12(offset)) {
        auto offset_str = std::to_string(offset);
        append_inst(FSTORE SINGLE, {r.print(), "$fp", offset_str});
    } else {
        auto addr = Reg::t(8);
        load_large_int64(offset, addr);
        append_inst(ADD DOUBLE, {addr.print(), "$fp", addr.print()});
        append_inst(FSTORE SINGLE, {r.print(), addr.print(), "0"});
    }
}

void CodeGen::gen_prologue() {
    // 先把旧 fp 存到临时寄存器
    append_inst("addi.d $t1, $fp, 0");

    if (IS_IMM_12(-static_cast<int>(context.frame_size))) {
        // 先分配栈帧
        append_inst("addi.d $sp, $sp, " +
                    std::to_string(-static_cast<int>(context.frame_size)));
        // fp = old sp
        append_inst("addi.d $fp, $sp, " +
                    std::to_string(static_cast<int>(context.frame_size)));
    } else {
        load_large_int64(context.frame_size, Reg::t(0));
        append_inst("sub.d $sp, $sp, $t0");
        append_inst("add.d $fp, $sp, $t0"); // fp = old sp
    }

    // 现在再把 ra / old fp 存到已经分配好的栈帧里
    append_inst("st.d $ra, $fp, -8");
    append_inst("st.d $t1, $fp, -16");

    int garg_cnt = 0;
    int farg_cnt = 0;
    for (auto &arg : context.func->get_args()) {
        if (arg.get_type()->is_float_type()) {
            store_from_freg(&arg, FReg::fa(farg_cnt++));
        } else {
            store_from_greg(&arg, Reg::a(garg_cnt++));
        }
    }
}

void CodeGen::gen_epilogue() {
    // 从当前栈帧中恢复 ra 和 old fp
    append_inst("ld.d $ra, $fp, -8");
    append_inst("ld.d $t0, $fp, -16");

    // sp 恢复成 old sp
    append_inst("addi.d $sp, $fp, 0");

    // fp 恢复成 old fp
    append_inst("addi.d $fp, $t0, 0");

    append_inst("jr $ra");
}

void CodeGen::gen_ret() {
    // TODO 函数返回，思考如何处理返回值、寄存器备份，如何返回调用者地址
    auto *retInst = static_cast<ReturnInst *>(context.inst);
    if (retInst->is_void_ret()) {
        if (context.func->get_name() == "main") {
            append_inst(ADDI WORD, {Reg::a(0).print(), "$zero", "0"});
        }
    } else {
        auto *retVal = retInst->get_operand(0);
        if (retVal->get_type()->is_float_type()) {
            load_to_freg(retVal, FReg::fa(0));
        } else {
            load_to_greg(retVal, Reg::a(0));
        }
    }
    append_inst("b " + func_exit_label_name(context.func));
}

void CodeGen::gen_br() {
    auto *branchInst = static_cast<BranchInst *>(context.inst);
    if (branchInst->is_cond_br()) {
        // 条件跳转：br i1 %cond, label %true_block, label %false_block
        auto *cond = branchInst->get_operand(0);
        auto *true_bb = static_cast<BasicBlock *>(branchInst->get_operand(1));
        auto *false_bb = static_cast<BasicBlock *>(branchInst->get_operand(2));
        load_to_greg(cond, Reg::t(0));
        append_inst("bnez $t0, " + label_name(true_bb));
        append_inst("b " + label_name(false_bb));
    } else {
        auto *branchbb = static_cast<BasicBlock *>(branchInst->get_operand(0));
        append_inst("b " + label_name(branchbb));
    }
}

void CodeGen::gen_binary() {
    load_to_greg(context.inst->get_operand(0), Reg::t(0));
    load_to_greg(context.inst->get_operand(1), Reg::t(1));
    switch (context.inst->get_instr_type()) {
    case Instruction::add:
        output.emplace_back("add.w $t2, $t0, $t1");
        break;
    case Instruction::sub:
        output.emplace_back("sub.w $t2, $t0, $t1");
        break;
    case Instruction::mul:
        output.emplace_back("mul.w $t2, $t0, $t1");
        break;
    case Instruction::sdiv:
        output.emplace_back("div.w $t2, $t0, $t1");
        break;
    default:
        assert(false);
    }
    store_from_greg(context.inst, Reg::t(2));
}

void CodeGen::gen_float_binary() {
    // TODO 浮点类型的二元指令
    load_to_freg(context.inst->get_operand(0), FReg::ft(0));
    load_to_freg(context.inst->get_operand(1), FReg::ft(1));
    switch (context.inst->get_instr_type()) {
    case Instruction::fadd:
        output.emplace_back("fadd.s $ft2, $ft0, $ft1");
        break;
    case Instruction::fsub:
        output.emplace_back("fsub.s $ft2, $ft0, $ft1");
        break;
    case Instruction::fmul:
        output.emplace_back("fmul.s $ft2, $ft0, $ft1");
        break;
    case Instruction::fdiv:
        output.emplace_back("fdiv.s $ft2, $ft0, $ft1");
        break;
    default:
        assert(false);
    }
    store_from_freg(context.inst, FReg::ft(2));
}

void CodeGen::gen_alloca() {
    /* 我们已经为 alloca 的内容分配空间，在此我们还需保存 alloca
     * 指令自身产生的定值，即指向 alloca 空间起始地址的指针
     */
    // TODO 将 alloca 出空间的起始地址保存在栈帧上
    auto *allocaInst = static_cast<AllocaInst *>(context.inst);
    auto value_offset = context.offset_map.at(allocaInst);
    auto alloca_type = allocaInst->get_alloca_type();
    auto alloca_size = alloca_type->get_size();
    auto alloca_space_offset = value_offset - static_cast<int>(alloca_size);

    if (IS_IMM_12(alloca_space_offset)) {
        append_inst("addi.d $t0, $fp, " + std::to_string(alloca_space_offset));
    } else {
        // 大偏移情况：分步加载
        load_large_int64(alloca_space_offset, Reg::t(0));
        append_inst("add.d $t0, $fp, $t0");
    }

    // 不能复用 store_from_greg 函数，因为 store_from_greg 中
    // auto *type = val->get_type(); 查询"要存储的值"的类型（如 i32*）
    // 但在 alloca 指令中，"要存储的值"是指向 alloca 空间起始地址的指针
    if (IS_IMM_12(value_offset)) {
        // $t0 中已有指针值，直接存储
        append_inst("st.d $t0, $fp, " + std::to_string(value_offset));
    } else {
        // 存储位置也需要大偏移处理
        load_large_int64(value_offset, Reg::t(8));
        append_inst("add.d $t1, $fp, $t8");
        append_inst("st.d $t0, $t1, 0");
    }
}

void CodeGen::gen_load() {
    // %p = load i32*, i32** %ptr_addr    ; 先加载指针 p
    // %x = load i32, i32* %p             ; 再通过指针加载值

    auto *ptr = context.inst->get_operand(0);
    auto *type = context.inst->get_type();
    load_to_greg(ptr, Reg::t(0));

    if (type->is_float_type()) {
        append_inst("fld.s $ft0, $t0, 0");
        store_from_freg(context.inst, FReg::ft(0));
    } else {
        // TODO load 整数类型的数据
        // 整数/指针类型：根据类型选择加载指令
        if (type->is_int1_type()) {
            append_inst("ld.b $t0, $t0, 0");
            append_inst("andi $t0, $t0, 1");
        } else if (type->is_int32_type()) {
            // 32位整数
            append_inst("ld.w $t0, $t0, 0");
        } else {
            // 64位整数或指针
            append_inst("ld.d $t0, $t0, 0");
        }
        // 存储加载的值
        store_from_greg(context.inst, Reg::t(0));
    }
}

void CodeGen::gen_store() {
    // store 指令格式: store <type> <value>, <type>* <ptr>

    auto *value = context.inst->get_operand(0);
    auto *ptr = context.inst->get_operand(1);
    auto *value_type = value->get_type();
    
    load_to_greg(ptr, Reg::t(0));

    if (value_type->is_float_type()) {
        // 浮点数存储
        load_to_freg(value, FReg::ft(0));
        append_inst("fst.s $ft0, $t0, 0");
    } else {
        // 整数/指针存储
        load_to_greg(value, Reg::t(1));
        
        if (value_type->is_int1_type()) {
            append_inst("st.b $t1, $t0, 0");
        } else if (value_type->is_int32_type()) {
            append_inst("st.w $t1, $t0, 0");
        } else {
            append_inst("st.d $t1, $t0, 0");
        }
    }
}

void CodeGen::gen_icmp() {
    auto *lhs = context.inst->get_operand(0);
    auto *rhs = context.inst->get_operand(1);
    load_to_greg(lhs, Reg::t(0));
    load_to_greg(rhs, Reg::t(1));

    switch (context.inst->get_instr_type()) {
    case Instruction::ge:
        // >= 比较
        // !($t0 < $t1) = !slt($t0, $t1)
        append_inst("slt $t2, $t0, $t1");
        append_inst("xori $t2, $t2, 1");
        break;
    case Instruction::gt:
        // > 比较
        append_inst("slt $t2, $t1, $t0");
        break;
    case Instruction::le:
        // <= 比较
        // !($t0 > $t1) = !slt($t1, $t0)
        append_inst("slt $t2, $t1, $t0");
        append_inst("xori $t2, $t2, 1");
        break;
    case Instruction::lt:
        // < 比较
        append_inst("slt $t2, $t0, $t1");
        break;
    case Instruction::eq:
        // == 比较
        append_inst("xor $t2, $t0, $t1");
        append_inst("sltui $t2, $t2, 1");
        break;
    case Instruction::ne:
        // != 比较
        append_inst("xor $t2, $t0, $t1");
        append_inst("sltu $t2, $zero, $t2");
        break;
    default:
        assert(false && "Unknown comparison operator");
    }
    store_from_greg(context.inst, Reg::t(2));
}

void CodeGen::gen_fcmp() {
    // TODO 处理各种浮点数比较的情况
    auto *lhs = context.inst->get_operand(0);
    auto *rhs = context.inst->get_operand(1);
    load_to_freg(lhs, FReg::ft(0));
    load_to_freg(rhs, FReg::ft(1));

    switch (context.inst->get_instr_type()) {
    case Instruction::fge:
        // >= 比较
        append_inst("fcmp.sle.s $fcc0, $ft1, $ft0");
        break;
    case Instruction::fgt:
        // > 比较
        append_inst("fcmp.slt.s $fcc0, $ft1, $ft0");
        break;
    case Instruction::fle:
        // <= 比较
        append_inst("fcmp.sle.s $fcc0, $ft0, $ft1");
        break;
    case Instruction::flt:
        // < 比较
        append_inst("fcmp.slt.s $fcc0, $ft0, $ft1");
        break;
    case Instruction::feq:
        // == 比较
        append_inst("fcmp.seq.s $fcc0, $ft0, $ft1");
        break;
    case Instruction::fne:
        // != 比较
        append_inst("fcmp.sne.s $fcc0, $ft0, $ft1");
        break;
    default:
        assert(false && "Unknown comparison operator");
    }
    
    auto base = fcmp_label_name(context.bb, context.fcmp_cnt++);
    auto true_label = base + "_true";
    auto end_label = base + "_end";

    append_inst("addi.d", {Reg::t(2).print(), "$zero", "0"});
    append_inst("bcnez", {"$fcc0", true_label});
    append_inst("b", {end_label});
    append_inst(true_label, ASMInstruction::Label);
    append_inst("addi.d", {Reg::t(2).print(), "$zero", "1"});
    append_inst(end_label, ASMInstruction::Label);

    store_from_greg(context.inst, Reg::t(2));
}

void CodeGen::gen_zext() {
    // TODO 将窄位宽的整数数据进行零扩展
    // zext i1 %x to i32  -> 将 1 位扩展为 32 位
    auto *src = context.inst->get_operand(0);
    auto *src_type = src->get_type();
    auto *dst_type = context.inst->get_type();

    load_to_greg(src, Reg::t(0));

    if (src_type->is_int1_type()) {
        if (dst_type->is_int32_type()) {
            append_inst("andi $t0, $t0, 1");
        }
    } else if (src_type->is_int32_type()) {
        append_inst("bstrpick.d $t0, $t0, 31, 0");
    }
    
    // 存储扩展后的结果
    store_from_greg(context.inst, Reg::t(0));
}

void CodeGen::gen_call() {
    auto *callInst = static_cast<CallInst *>(context.inst);

    // operand 0 是被调用函数
    auto *callee = dynamic_cast<Function *>(callInst->get_operand(0));
    assert(callee && "direct call expects callee to be a Function");

    int garg_cnt = 0;
    int farg_cnt = 0;

    // 从 operand 1 开始才是真正参数
    for (unsigned i = 1; i < callInst->get_operands().size(); ++i) {
        auto *arg = callInst->get_operand(i);
        auto *arg_type = arg->get_type();

        if (arg_type->is_float_type()) {
            if (farg_cnt < 8) {
                load_to_freg(arg, FReg::fa(farg_cnt));
                ++farg_cnt;
            }
        } else {
            if (garg_cnt < 8) {
                load_to_greg(arg, Reg::a(garg_cnt));
                ++garg_cnt;
            }
        }
    }

    append_inst("bl " + callee->get_name());

    if (!callInst->is_void()) {
        auto *ret_type = callInst->get_type();
        if (ret_type->is_float_type()) {
            store_from_freg(context.inst, FReg::fa(0));
        } else {
            store_from_greg(context.inst, Reg::a(0));
        }
    }
}

/*
 * %op = getelementptr [10 x i32], [10 x i32]* %op, i32 0, i32 %op
 * %op = getelementptr        i32,        i32* %op, i32 %op
 *
 * Memory layout
 *       -            ^
 * +-----------+      | Smaller address
 * |  arg ptr  |---+  |
 * +-----------+   |  |
 * |           |   |  |
 * +-----------+   /  |
 * |           |<--   |
 * |           |   \  |
 * |           |   |  |
 * |   Array   |   |  |
 * |           |   |  |
 * |           |   |  |
 * |           |   |  |
 * +-----------+   |  |
 * |  Pointer  |---+  |
 * +-----------+      |
 * |           |      |
 * +-----------+      |
 * |           |      |
 * +-----------+      |
 * |           |      |
 * +-----------+      | Larger address
 *       +
 */
void CodeGen::gen_gep() {
    // 计算内存地址
    // getelementptr [10 x i32], [10 x i32]* %ptr, i32 0, i32 %idx
    // getelementptr i32, i32* %ptr, i32 %idx
    
    auto *gepInst = static_cast<GetElementPtrInst *>(context.inst);
    auto *ptr = gepInst->get_operand(0);  // 基指针
    auto &indices = gepInst->get_operands();  // 所有操作数（包括基指针）
    
    // 加载基指针到 $t0
    load_to_greg(ptr, Reg::t(0));
    
    auto *element_type = gepInst->get_element_type();
    
    // 从第 1 个索引开始处理（第 0 个是基指针）
    for (unsigned i = 1; i < indices.size(); ++i) {
        auto *idx = indices[i];
        
        // 计算当前维度的元素大小
        auto element_size = element_type->get_size();
        
        // 加载索引到 $t1
        load_to_greg(idx, Reg::t(1));
        
        // 计算偏移：offset = idx * element_size
        if (element_size == 1) {
            // 元素大小为 1，直接相加
            append_inst("add.d $t0, $t0, $t1");
        } else if (element_size > 0 && (element_size & (element_size - 1)) == 0) {
            // 元素大小是 2 的幂，使用移位优化
            int shift = __builtin_ctz(element_size);  // 计算 log2(element_size)
            append_inst("slli.d $t1, $t1, " + std::to_string(shift));
            append_inst("add.d $t0, $t0, $t1");
        } else {
            // 元素大小是任意值，使用乘法
            load_large_int64(element_size, Reg::t(2));
            append_inst("mul.d $t1, $t1, $t2");
            append_inst("add.d $t0, $t0, $t1");
        }

        // 更新 element_type 为下一维的元素类型
        if (element_type->is_array_type()) {
            element_type = element_type->get_array_element_type();
        } else if (element_type->is_pointer_type()) {
            element_type = element_type->get_pointer_element_type();
        }
    }
    // 将计算结果（最终地址）存储到栈帧
    store_from_greg(context.inst, Reg::t(0));
}

void CodeGen::gen_sitofp() {
    // 整数转向浮点数
    // sitofp i32 %x to float -> 将有符号 32 位整数转换为单精度浮点数
    auto *src = context.inst->get_operand(0);
    
    // 加载源整数到整数寄存器 $t0
    load_to_greg(src, Reg::t(0));
    // 将整数从整数寄存器转移到浮点寄存器
    // gr2fr.w $ft0, $t0：将 32 位整数转移到浮点寄存器
    append_inst("movgr2fr.w $ft0, $t0");
    // 执行整数到浮点数的转换
    append_inst("ffint.s.w $ft0, $ft0");
    // 将转换结果存储到栈帧
    store_from_freg(context.inst, FReg::ft(0));
}

void CodeGen::gen_fptosi() {
    // TODO 浮点数转向整数，注意向下取整(round to zero)
    auto *src = context.inst->get_operand(0);
    // 加载源浮点数到浮点寄存器 $ft0
    load_to_freg(src, FReg::ft(0));
    // 执行浮点数到整数的转换，向下取整
    append_inst("ftintrz.w.s $ft0, $ft0");
    // 将整数从浮点数寄存器转移到整数寄存器
    append_inst("movfr2gr.s $t0, $ft0");
    // 将转换结果存储到栈帧
    store_from_greg(context.inst, Reg::t(0));
}

void CodeGen::run() {
    // 确保每个函数中基本块的名字都被设置好
    m->set_print_name();

    /* 使用 GNU 伪指令为全局变量分配空间
     * 你可以使用 `la.local` 指令将标签 (全局变量) 的地址载入寄存器中, 比如
     * 要将 `a` 的地址载入 $t0, 只需要 `la.local $t0, a`
     */
    if (!m->get_global_variable().empty()) {
        append_inst("Global variables", ASMInstruction::Comment);
        /* 虽然下面两条伪指令可以简化为一条 `.bss` 伪指令, 但是我们还是选择使用
         * `.section` 将全局变量放到可执行文件的 BSS 段, 原因如下:
         * - 尽可能对齐交叉编译器 loongarch64-unknown-linux-gnu-gcc 的行为
         * - 支持更旧版本的 GNU 汇编器, 因为 `.bss` 伪指令是应该相对较新的指令,
         *   GNU 汇编器在 2023 年 2 月的 2.37 版本才将其引入
         */
        append_inst(".text", ASMInstruction::Atrribute);
        append_inst(".section", {".bss", "\"aw\"", "@nobits"},
                    ASMInstruction::Atrribute);
        for (auto &global : m->get_global_variable()) {
            auto size =
                global.get_type()->get_pointer_element_type()->get_size();
            append_inst(".globl", {global.get_name()},
                        ASMInstruction::Atrribute);
            append_inst(".type", {global.get_name(), "@object"},
                        ASMInstruction::Atrribute);
            append_inst(".size", {global.get_name(), std::to_string(size)},
                        ASMInstruction::Atrribute);
            append_inst(global.get_name(), ASMInstruction::Label);
            append_inst(".space", {std::to_string(size)},
                        ASMInstruction::Atrribute);
        }
    }

    // 函数代码段
    output.emplace_back(".text", ASMInstruction::Atrribute);
    for (auto &func : m->get_functions()) {
        if (not func.is_declaration()) {
            // 更新 context
            context.clear();
            context.func = &func;

            // 函数信息
            append_inst(".globl", {func.get_name()}, ASMInstruction::Atrribute);
            append_inst(".type", {func.get_name(), "@function"},
                        ASMInstruction::Atrribute);
            append_inst(func.get_name(), ASMInstruction::Label);

            // 分配函数栈帧
            allocate();
            // 生成 prologue
            gen_prologue();

            for (auto &bb : func.get_basic_blocks()) {
                context.bb = &bb;
                append_inst(label_name(context.bb), ASMInstruction::Label);
                for (auto &instr : bb.get_instructions()) {
                    // For debug
                    append_inst(instr.print(), ASMInstruction::Comment);
                    context.inst = &instr; // 更新 context
                    switch (instr.get_instr_type()) {
                    case Instruction::ret:
                        gen_ret();
                        break;
                    case Instruction::br:
                        copy_stmt();
                        gen_br();
                        break;
                    case Instruction::add:
                    case Instruction::sub:
                    case Instruction::mul:
                    case Instruction::sdiv:
                        gen_binary();
                        break;
                    case Instruction::fadd:
                    case Instruction::fsub:
                    case Instruction::fmul:
                    case Instruction::fdiv:
                        gen_float_binary();
                        break;
                    case Instruction::alloca:
                        /* 对于 alloca 指令，我们已经为 alloca
                         * 的内容分配空间，在此我们还需保存 alloca
                         * 指令自身产生的定值，即指向 alloca 空间起始地址的指针
                         */
                        gen_alloca();
                        break;
                    case Instruction::load:
                        gen_load();
                        break;
                    case Instruction::store:
                        gen_store();
                        break;
                    case Instruction::ge:
                    case Instruction::gt:
                    case Instruction::le:
                    case Instruction::lt:
                    case Instruction::eq:
                    case Instruction::ne:
                        gen_icmp();
                        break;
                    case Instruction::fge:
                    case Instruction::fgt:
                    case Instruction::fle:
                    case Instruction::flt:
                    case Instruction::feq:
                    case Instruction::fne:
                        gen_fcmp();
                        break;
                    case Instruction::phi:
                        /* for phi, just convert to a series of
                         * copy-stmts */
                        /* we can collect all phi and deal them at
                         * the end */
                        break;
                    case Instruction::call:
                        gen_call();
                        break;
                    case Instruction::getelementptr:
                        gen_gep();
                        break;
                    case Instruction::zext:
                        gen_zext();
                        break;
                    case Instruction::fptosi:
                        gen_fptosi();
                        break;
                    case Instruction::sitofp:
                        gen_sitofp();
                        break;
                    }
                }
            }
            append_inst(func_exit_label_name(&func), ASMInstruction::Label);
            // 生成 epilogue
            gen_epilogue();
        }
    }
}

std::string CodeGen::print() const {
    std::string result;
    for (const auto &inst : output) {
        result += inst.format();
    }
    return result;
}
