#include "ASMInstruction.hpp"
#include "CodeGen.hpp"
#include "Module.hpp"

#include <iostream>
#include <memory>
#include <unordered_map>

void translate_main(CodeGen *codegen); // 将 main 函数翻译为汇编代码

int main() {
    auto *module = new Module();
    auto *codegen = new CodeGen(module);

    // 告诉汇编器将汇编放到代码段
    codegen->append_inst(".text", ASMInstruction::Atrribute);

    translate_main(codegen);

    std::cout << codegen->print();
    delete codegen;
    delete module;
    return 0;
}

// TODO: 按照提示补全
void translate_main(CodeGen *codegen) {
    std::unordered_map<std::string, int> offset_map;

    /* 声明 main 函数 */
    codegen->append_inst(".globl main", ASMInstruction::Atrribute);
    codegen->append_inst(".type main, @function", ASMInstruction::Atrribute);

    /* main 函数开始 */
    codegen->append_inst("main", ASMInstruction::Label); // main 函数标签

    /* main 函数的 Prologue (序言) */
    // 保存返回地址
    codegen->append_inst("st.d $ra, $sp, -8");
    // 保存老的 fp
    codegen->append_inst("st.d $fp, $sp, -16");
    // 设置新的 fp
    codegen->append_inst("addi.d $fp, $sp, 0");
    // 为栈帧分配空间. 为什么是 32 字节?
    // ra(-8)、fp(-16)、%op0(-20)、%op1(-24)、%op2(-28)，总计 28 字节，向上按 16/32 对齐取 32
    codegen->append_inst("addi.d $sp, $sp, -32");

    /* main 函数的 label_entry */
    codegen->append_inst(".main_label_entry", ASMInstruction::Label);

    /* %op0 = fcmp ugt float 0x4016000000000000, 0x3ff0000000000000 */
    // 在汇编中写入注释, 方便 debug
    codegen->append_inst(
        "%op0 = fcmp ugt float 0x4016000000000000, 0x3ff0000000000000",
        ASMInstruction::Comment);

    // %op0 按 i1 落栈，这里用 4 字节槽保存 0/1
    offset_map["%op0"] = -20;

    // 5.5f = 0x40b00000
    codegen->append_inst("lu12i.w $t0, 0x40b00");
    codegen->append_inst("movgr2fr.w $ft0, $t0");

    // 1.0f = 0x3f800000
    codegen->append_inst("lu12i.w $t1, 0x3f800");
    codegen->append_inst("movgr2fr.w $ft1, $t1");

    // ugt: unordered greater than
    // 这里两个操作数都不是 NaN，所以等价于 >
    // 用 1.0 < 5.5 来实现
    codegen->append_inst("fcmp.slt.s $fcc0, $ft1, $ft0");

    // 先默认 %op0 = 0
    codegen->append_inst("addi.w $t2, $zero, 0");
    codegen->append_inst("st.w",
                         {"$t2", "$fp", std::to_string(offset_map["%op0"])});
    // 若比较为真，则跳到设置 1
    codegen->append_inst("bcnez $fcc0, .main_set_op0_true");
    codegen->append_inst("b .main_set_op0_end");
    codegen->append_inst(".main_set_op0_true", ASMInstruction::Label);
    codegen->append_inst("addi.w $t2, $zero, 1");
    codegen->append_inst("st.w",
                         {"$t2", "$fp", std::to_string(offset_map["%op0"])});
    codegen->append_inst(".main_set_op0_end", ASMInstruction::Label);

    /* %op1 = zext i1 %op0 to i32 */
    codegen->append_inst("%op1 = zext i1 %op0 to i32", ASMInstruction::Comment);
    offset_map["%op1"] = -24;
    // %op0 栈里本来就按 0/1 的 32 位整数存放，zext 到 i32 可直接拷贝
    codegen->append_inst("ld.w",
                         {"$t0", "$fp", std::to_string(offset_map["%op0"])});
    codegen->append_inst("st.w",
                         {"$t0", "$fp", std::to_string(offset_map["%op1"])});

    /* %op2 = icmp ne i32 %op1, 0 */
    codegen->append_inst("%op2 = icmp ne i32 %op1, 0", ASMInstruction::Comment);
    offset_map["%op2"] = -28;

    // %op2 = (%op1 != 0)
    // 可不用跳转：sltu rd, $zero, rs
    // 当 rs != 0 时结果为 1，否则为 0
    // TODO: 获得 %op1 的值, 然后进行比较, 最后将结果写入 %op2 
    // 思考: 如何比较? 能否不使用跳转指令计算结果? 
    // 提示: 尝试使用 xor/xori 和 slt/sltu/slti/sltui 计算比较结果
    codegen->append_inst("ld.w",
                         {"$t0", "$fp", std::to_string(offset_map["%op1"])});
    // x != 0
    codegen->append_inst("xori $t0, $t0, 0");
    codegen->append_inst("sltu $t1, $zero, $t0");
    codegen->append_inst("st.w",
                         {"$t1", "$fp", std::to_string(offset_map["%op2"])});

    /* br i1 %op2, label %label3, label %label4 */
    codegen->append_inst("br i1 %op2, label %label3, label %label4",
                         ASMInstruction::Comment);
    codegen->append_inst("ld.w",
                         {"$t0", "$fp", std::to_string(offset_map["%op2"])});
    codegen->append_inst("bnez $t0, .main_label3");
    codegen->append_inst("b .main_label4");

    /* label3: */
    codegen->append_inst(".main_label3", ASMInstruction::Label);

    /* ret i32 233 */
    codegen->append_inst("ret i32 233", ASMInstruction::Comment);
    codegen->append_inst("addi.w $a0, $zero, 233");
    codegen->append_inst("b main_exit");

    /* label4: */
    codegen->append_inst(".main_label4", ASMInstruction::Label);

    /* ret i32 0 */
    codegen->append_inst("ret i32 0", ASMInstruction::Comment);
    codegen->append_inst("addi.w $a0, $zero, 0");
    codegen->append_inst("b main_exit");

    /* main 函数的 Epilogue (收尾) */
    codegen->append_inst("main_exit", ASMInstruction::Label);
    // 释放栈帧空间
    codegen->append_inst("addi.d $sp, $sp, 32");
    // 恢复 ra
    codegen->append_inst("ld.d $ra, $sp, -8");
    // 恢复 fp
    codegen->append_inst("ld.d $fp, $sp, -16");
    // 返回
    codegen->append_inst("jr $ra");
}
