#ifndef COMPILER_MIPS_H
#define COMPILER_MIPS_H

#include <string>
#include <vector>

// MIPS 寄存器枚举
enum class MipsReg {
    ZERO, // $0: 常数0
    AT,   // $1: 汇编保留
    V0, V1, // $2-$3: 返回值/系统调用
    A0, A1, A2, A3, // $4-$7: 参数
    T0, T1, T2, T3, T4, T5, T6, T7, // $8-$15: 临时寄存器 (Caller Saved) - 我们主要用这些做FIFO分配
    S0, S1, S2, S3, S4, S5, S6, S7, // $16-$23: 保存寄存器 (Callee Saved)
    T8, T9, // $24-$25: 临时寄存器 - 我们保留用于立即数加载和地址计算
    K0, K1, // $26-$27: 内核保留
    GP,     // $28: 全局指针
    SP,     // $29: 栈指针
    FP,     // $30: 帧指针
    RA      // $31: 返回地址
};

class MipsHelper {
public:
    static std::string getRegName(MipsReg reg) {
        static const std::string names[] = {
                "$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
                "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
                "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
                "$t8", "$t9", "$k0", "$k1", "$gp", "$sp", "$fp", "$ra"
        };
        return names[static_cast<int>(reg)];
    }
};

#endif // COMPILER_MIPS_H