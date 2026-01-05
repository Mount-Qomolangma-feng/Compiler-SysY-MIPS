#include "MIPSGenerator.h"
#include <iostream>
#include <stdexcept>

MIPSGenerator::MIPSGenerator(IRGenerator& irGen, SymbolTable& symTab)
        : irGenerator(irGen), symbolTable(symTab), regAlloc(*this) {}

void MIPSGenerator::generate(const std::string& filename) {
    fout.open(filename);
    if (!fout.is_open()) {
        std::cerr << "MIPS Error: Cannot open file " << filename << std::endl;
        return;
    }

    // 1. 生成数据段 (全局变量 & 字符串常量)
    generateDataSegment();

    // 2. 生成代码段
    generateTextSegment();

    fout.close();
    std::cout << "MIPS 汇编已输出到: " << filename << std::endl;
}

// === 数据段生成 ===
// MIPSGenerator.cpp

void MIPSGenerator::generateDataSegment() {
    fout << ".data\n";

    // 1. 全局变量 & 静态变量
    auto allSymbols = symbolTable.getAllSymbols();
    for (const auto& sym : allSymbols) {
        // 筛选全局/静态变量，排除函数
        bool isGlobal = (sym.scope == 1);
        bool isStatic = (sym.type == SymbolType::StaticInt || sym.type == SymbolType::StaticIntArray);

        if ((isGlobal || isStatic) && !sym.isFunction()) {
            std::string label = sym.label.empty() ? sym.name : sym.label;

            // 【新增】强制 4 字节对齐，防止之前的 .asciiz 字符串导致地址错位
            fout << "    .align 2\n";
            // 【修改后】 使用 getMipsLabel
            fout << getMipsLabel(label) << ":\n";

            if (sym.isArray()) {
                // 数组处理
                if (sym.arrayInitValues.empty()) {
                    // 没有初始值信息 -> 全零空间
                    fout << "    .space " << sym.getByteSize() << "\n";
                } else {
                    // 有初始值 -> 生成 .word
                    for (int val : sym.arrayInitValues) {
                        fout << "    .word " << val << "\n";
                    }
                    // 计算剩余未初始化的空间 (总字节 - 已初始化字节)
                    int initializedSize = sym.arrayInitValues.size() * 4;
                    int remainingBytes = sym.getByteSize() - initializedSize;

                    if (remainingBytes > 0) {
                        fout << "    .space " << remainingBytes << "\n";
                    }
                }
            } else {
                // 标量初始化
                fout << "    .word " << sym.value << "\n";
            }
        }
    }

    // 2. 字符串常量
    const auto& strConsts = irGenerator.getStringConstants();
    for (const auto& kv : strConsts) {
        fout << kv.first << ": .asciiz \"";
        for (char c : kv.second) {
            switch (c) {
                case '\n': fout << "\\n"; break;
                case '\"': fout << "\\\""; break;
                case '\\': fout << "\\\\"; break;
                case '\t': fout << "\\t"; break;
                case '\0': break;
                default:   fout << c; break;
            }
        }
        fout << "\"\n";
    }
    fout << "\n";
}

// === 代码段生成 ===
void MIPSGenerator::generateTextSegment() {
    fout << ".text\n";

    // ================== 【新增逻辑开始】 ==================
    // 1. 生成程序入口跳转逻辑
    // 这是整个程序的入口点。即使 main 函数定义在文件的后面，
    // 我们也必须在这里显式跳转过去。
    fout << "# === Program Entry Point ===\n";

    // 使用 jal 跳转到 main，这样 $ra 会保存返回地址
    // 虽然你的 main 函数结尾使用了 syscall 10 退出，但使用 jal 是更标准的做法
    fout << "    jal main\n";

    // 2. 设置兜底退出逻辑
    // 如果 main 函数意外返回（通过 jr $ra），程序流会回到这里。
    // 我们必须在这里放置 exit 系统调用，防止程序跑飞 (Fall off the cliff)
    fout << "    li $v0, 10\n";
    fout << "    syscall\n";
    fout << "# ===========================\n\n";
    // ================== 【新增逻辑结束】 ==================

    // 1. 生成辅助函数
    generateSyscallHelpers();

    // 2. 遍历 IR 指令
    const auto& instructions = irGenerator.getInstructions();

    for (auto instr : instructions) {
        // 使用 toString() 方法获取指令的字符串表示
        std::string instrStr = instr->toString();

        std::cout << "[MIPSGen] >>> Processing: " << instrStr << std::endl;

        switch (instr->op) {
            case IROp::FUNC_ENTRY: visitFuncEntry(instr); break;
            case IROp::FUNC_EXIT:  visitFuncExit(instr); break;
            case IROp::ADD: case IROp::SUB: case IROp::MUL:
            case IROp::DIV: case IROp::MOD:
                // 映射算术指令
            {
                std::string mipsOp;
                // 【修改点 1】将 add/sub 改为 addu/subu
                // addu/subu 不会因为溢出而抛出异常，符合 C 语言 int 的 Wrap-around 行为
                if (instr->op == IROp::ADD) mipsOp = "addu";
                else if (instr->op == IROp::SUB) mipsOp = "subu";

                else if (instr->op == IROp::MUL) mipsOp = "mul";
                else if (instr->op == IROp::DIV) mipsOp = "div";
                else if (instr->op == IROp::MOD) mipsOp = "rem"; // MIPS 伪指令 rem
                visitBinaryOp(instr, mipsOp);
            }
                break;
            case IROp::NEG: case IROp::NOT:
                visitUnaryOp(instr); break;
            case IROp::GT: case IROp::GE: case IROp::LT:
            case IROp::LE: case IROp::EQ: case IROp::NEQ:
                // 比较指令在 MIPS 中通常用 slt 等实现，这里简化处理
                // 使用 seq, sne, sgt, sge, slt, sle 等伪指令
            {
                std::string mipsOp;
                if (instr->op == IROp::GT) mipsOp = "sgt";
                else if (instr->op == IROp::GE) mipsOp = "sge";
                else if (instr->op == IROp::LT) mipsOp = "slt";
                else if (instr->op == IROp::LE) mipsOp = "sle";
                else if (instr->op == IROp::EQ) mipsOp = "seq";
                else if (instr->op == IROp::NEQ) mipsOp = "sne";
                visitBinaryOp(instr, mipsOp);
            }
                break;
            case IROp::ASSIGN:
                // x = y -> move $reg(x), $reg(y)
            {
                MipsReg ry = regAlloc.getReg(instr->arg1);
                MipsReg rx = regAlloc.allocateReg(instr->result);
                emit("move " + MipsHelper::getRegName(rx) + ", " + MipsHelper::getRegName(ry));
            }
                break;
            case IROp::LABEL:
                // 1. 先把当前寄存器里的东西存回去 (属于上一个块的收尾工作)
                regAlloc.spillAll();

                // 2. 再输出 Label (标志着新块的开始)
                emitLabel(instr->result->name);

                // 3. 更新当前 Label 记录
                currentLabel = instr->result->name;
                break;
            case IROp::JUMP:
                regAlloc.spillAll(); // 跳转前清空寄存器
                // 【修改前】 emit("j " + instr->result->name);
                // 【修改后】
                emit("j " + getMipsLabel(instr->result->name));
                break;
            case IROp::BEQZ: visitBranch(instr); break;
            case IROp::LOAD:
            case IROp::STORE:
            case IROp::GET_ADDR:
                visitLoadStore(instr); break;
            case IROp::CALL:  visitCall(instr); break;
            case IROp::RET:   visitRet(instr); break;
            case IROp::PARAM:
                /* Param handled in CALL */
                visitParam(instr);
                break;
            case IROp::GETINT:
            case IROp::PRINTINT:
            case IROp::PRINTSTR:
                visitIO(instr); break;
            default: break;
        }

        std::cout << "[MIPSGen] <<< Finished: " << instrStr << std::endl;
    }
}

void MIPSGenerator::generateSyscallHelpers() {
    // 可以在这里生成 getint/putint 的具体实现，或者直接在指令中内联 syscall
    // 为了简单，我们使用内联 syscall
}

// === 核心逻辑：地址计算 ===
// 根据文档：局部变量位于栈帧 ($fp - offset)，全局变量使用 Label
// === 核心逻辑：地址计算 ===
// 修改目的：正确处理函数参数和局部变量，防止错误地回退为全局变量


std::string MIPSGenerator::getAddress(Operand* op, MipsReg tempReg) {
    CodeGenFunctionInfo* currentFuncInfo = getCurrentFuncInfo();

    if (op->type == OperandType::TEMP || op->type == OperandType::VAR) {
        // 1) 优先尝试在当前函数的栈帧表中查找
        if (currentFuncInfo) {
            auto it = currentFuncInfo->symbolMap.find(op->name);
            if (it != currentFuncInfo->symbolMap.end()) {
                int offset = it->second.offset;
                return "-" + std::to_string(offset) + "($fp)";
            }
        }

        // 2) 没在栈帧中找到，进行严格的错误检查
        if (op->symbol) {
            bool isStatic = (op->symbol->type == SymbolType::StaticInt ||
                             op->symbol->type == SymbolType::StaticIntArray);

            // 普通局部变量/参数不在栈帧表中属于致命错误
            if (!isStatic && (op->symbol->isParam || op->symbol->scope > 1)) {
                throw std::runtime_error(
                        "[MIPS getAddress] invalid address for operand: " + op->name
                );
            }
        }

        // 临时变量必须在栈帧中 -> 致命错误
        if (op->type == OperandType::TEMP) {
            throw std::runtime_error(
                    "[MIPS getAddress] invalid address for operand: " + op->name
            );
        }

        // 3) 确认为全局变量（Scope=1 或 Static）
        std::string regName = MipsHelper::getRegName(tempReg);

        // 优先使用 Symbol 的 label（如 static_map_cnt_19）
        std::string labelName = op->name;
        if (op->symbol && !op->symbol->label.empty()) {
            labelName = op->symbol->label;
        }

        emit("la " + regName + ", " + getMipsLabel(labelName));
        return "0(" + regName + ")";
    }

    // 非 VAR/TEMP 的 operand 不应调用 getAddress
    throw std::runtime_error(
            "[MIPS getAddress] invalid address for operand: " + op->name
    );
}


// [新增实现]
std::string MIPSGenerator::getMipsLabel(const std::string& name) {
    // main 函数是程序入口，必须保持原名，不能加前缀
    if (name == "main") {
        return "main";
    }
    // 其他所有标签（函数名、全局变量、BasicBlock Label）统一加下划线前缀
    return "_" + name;
}

// === 寄存器分配器实现 (FIFO) ===

MipsReg MIPSGenerator::RegisterManager::getReg(Operand* op, bool isLoad) {
    // 1. 如果是立即数，直接加载到保留寄存器 T8 并返回
    if (op->type == OperandType::IMM) {
        generator.emit("li $t8, " + std::to_string(op->value));
        return MipsReg::T8;
    }

    std::string varName = op->name;

    // ================= [调试代码新增] =================
    // 检查变量状态并打印
    bool inRegister = varToReg.count(varName);
    std::cout << "[RegAlloc] Requesting Var: \"" << varName << "\"";

    if (inRegister) {
        // 如果在寄存器中，打印是哪个寄存器
        MipsReg existingReg = varToReg[varName];
        std::cout << " -> [HIT] Already in " << MipsHelper::getRegName(existingReg) << std::endl;
    } else {
        // 如果不在，打印 MISS
        std::cout << " -> [MISS] Not in reg, allocating..." << std::endl;
    }
    // ================================================

    // 2. 检查变量是否已在寄存器中
    if (varToReg.count(varName)) {
        return varToReg[varName];
    }

    // 3. 不在寄存器中，需要分配
    MipsReg reg;

    if (!freeRegs.empty()) {
        // 3a. 有空闲寄存器，取出一个
        reg = freeRegs.front();
        freeRegs.pop_front();
        // 调试信息：分配空闲寄存器
        std::cout << "           > Allocated FREE reg: " << MipsHelper::getRegName(reg) << std::endl;
    } else {
        // 3b. 没有空闲寄存器，FIFO 淘汰最早使用的 (busyRegs.front)
        reg = busyRegs.front();
        busyRegs.pop_front();

        // 调试信息：发生溢出
        std::cout << "           > Registers FULL. Spilling: " << MipsHelper::getRegName(reg)
                  << " (Old Var: " << regToVar[reg] << ")" << std::endl;

        // 溢出 (Spill) 旧变量
        spillReg(reg);
    }

    // 4. 建立新映射
    varToReg[varName] = reg;
    regToVar[reg] = varName;

    // 【新增】保存 SymbolEntry 指针
    if (op->symbol) {
        varToSymbol[varName] = op->symbol;
    }

    busyRegs.push_back(reg); // 加入队尾 (最新使用)

    // 刚加载进来的寄存器，内容与内存一致，Dirty = false
    dirtyRegs.erase(reg);

    // 5. 如果需要，从内存加载值
    if (isLoad) {
        loadValue(reg, op);
    }

    return reg;
}

MipsReg MIPSGenerator::RegisterManager::allocateReg(Operand* result) {
    // 与 getReg 类似，但不从内存加载旧值 (用于目标寄存器)
    std::string varName = result->name;

    // 如果已经分配了寄存器，复用它
    if (varToReg.count(varName)) {
        MipsReg reg = varToReg[varName];
        // 我们将要覆盖这个寄存器，所以它变为 Dirty
        dirtyRegs.insert(reg);
        return reg;
    }

    // 否则分配新寄存器
    MipsReg reg;
    if (!freeRegs.empty()) {
        reg = freeRegs.front();
        freeRegs.pop_front();
    } else {
        reg = busyRegs.front();
        busyRegs.pop_front();
        spillReg(reg);
    }

    varToReg[varName] = reg;
    regToVar[reg] = varName;

    // 【新增】保存 SymbolEntry 指针
    if (result->symbol) {
        varToSymbol[varName] = result->symbol;
    }

    busyRegs.push_back(reg);

    // 分配给结果的寄存器肯定会被写入，Dirty = true
    dirtyRegs.insert(reg);

    return reg;
}

void MIPSGenerator::RegisterManager::spillReg(MipsReg reg) {
    if (regToVar.count(reg)) {
        std::string varName = regToVar[reg];

        // 【核心优化】只有当寄存器是 Dirty 的时候才写回内存
        if (dirtyRegs.count(reg)) {

            Operand tmpOp(varName, OperandType::VAR);

            // 2. 【核心修复】尝试恢复 SymbolEntry 信息
            if (varToSymbol.count(varName)) {
                tmpOp.symbol = varToSymbol[varName];
            }

            // [修改后] 显式使用 T9，防止覆盖 T8 (T8 可能正持有立即数)
            std::string addr = generator.getAddress(&tmpOp, MipsReg::T9);
            generator.emit("sw " + MipsHelper::getRegName(reg) + ", " + addr);
        }

        // 清理状态

        varToReg.erase(varName);
        regToVar.erase(reg);
        dirtyRegs.erase(reg); // 移除 Dirty 标记

        varToSymbol.erase(varName);
    }
}

void MIPSGenerator::RegisterManager::spillAll() {
    // 复制一份 keys 防止迭代器失效
    std::list<MipsReg> activeRegs = busyRegs;

    /*// ================= [调试代码开始] =================
    std::cout << "[RegAlloc] spillAll() triggered. Busy Registers ("
              << activeRegs.size() << "): ";

    if (activeRegs.empty()) {
        std::cout << "(None)";
    } else {
        for (MipsReg reg : activeRegs) {
            // 使用 MipsHelper::getRegName 获取寄存器字符串名称 (例如 "$t0")
            std::cout << MipsHelper::getRegName(reg) << " ";
        }
    }
    std::cout << std::endl;
    // ================= [调试代码结束] =================*/

    for (MipsReg reg : activeRegs) {
        /*// ================= [调试代码] =================
        // 打印当前正在处理的寄存器名字
        std::cout << "  > Spilling & Freeing: " << MipsHelper::getRegName(reg) << std::endl;
        // ============================================*/

        spillReg(reg);
        freeRegs.push_back(reg); // 回收为空闲
    }
    busyRegs.clear();
    dirtyRegs.clear(); // 确保清空
}

// 文件：MIPSGenerator.cpp
// 位置：建议放在 RegisterManager::getReg 函数附近

void MIPSGenerator::RegisterManager::mapParamToReg(const std::string& varName, MipsReg srcReg) {
    // 1. 分配一个目标寄存器 (类似 getReg 的分配逻辑)
    MipsReg destReg;

    if (!freeRegs.empty()) {
        // 有空闲，直接用
        destReg = freeRegs.front();
        freeRegs.pop_front();
    } else {
        // 没空闲，强制溢出最早的一个
        destReg = busyRegs.front();
        busyRegs.pop_front();
        spillReg(destReg);
    }

    // 2. 生成 move 指令：将值从 $aX 移动到 $tX
    // 例如：move $t0, $a0
    std::string srcName = MipsHelper::getRegName(srcReg);
    std::string destName = MipsHelper::getRegName(destReg);
    generator.emit("move " + destName + ", " + srcName);

    // 3. 建立映射关系
    varToReg[varName] = destReg;
    regToVar[destReg] = varName;
    busyRegs.push_back(destReg); // 加入 FIFO 队列尾部

    // 4. 设置脏位状态
    // 因为我们在 visitFuncEntry 中已经先执行了 sw $aX, stack，
    // 所以此时寄存器($tX)的值与栈内存是一致的。
    // Dirty = false 表示如果该寄存器被溢出，不需要写回内存（直接丢弃即可）。
    dirtyRegs.erase(destReg);

    // 调试信息 (可选)
    // std::cout << "[RegAlloc] Mapped Param: " << varName << " -> " << destName << std::endl;
}

// 文件: MIPSGenerator.cpp
// 建议位置: 放在 MIPSGenerator::RegisterManager::spillAll() 函数下方

void MIPSGenerator::RegisterManager::clearMap() {
    // 1. 回收所有忙碌寄存器到空闲队列
    // 我们将 busyRegs 中的寄存器追加到 freeRegs 尾部
    for (MipsReg reg : busyRegs) {
        freeRegs.push_back(reg);
    }
    busyRegs.clear();

    // 2. 清空映射表
    // 这样下次 getReg 请求变量时，会发现 map 为空，从而强制重新分配并从内存 lw
    varToReg.clear();
    regToVar.clear();

    varToSymbol.clear(); // 【新增】清空符号缓存

    dirtyRegs.clear();
}

void MIPSGenerator::RegisterManager::loadValue(MipsReg reg, Operand* op) {
    // 【优化】使用 $t9 作为地址计算的临时寄存器
    // 原因：getReg 对于立即数(IMM)会直接返回 $t8。
    // 如果指令是 "op = IMM + StaticVar"，arg1 会占用 $t8。
    // 如果我们在加载 arg2(StaticVar) 时也用 $t8 计算地址，就会覆盖 arg1 的值。
    // 所以这里强制指定用 $t9，避免冲突。
    std::string addr = generator.getAddress(op, MipsReg::T9);
    generator.emit("lw " + MipsHelper::getRegName(reg) + ", " + addr);
}

// === 指令具体实现 ===

void MIPSGenerator::visitFuncEntry(IRInstruction* instr) {
    regAlloc.clearMap();

    std::string funcName = currentLabel;

    // 获取栈帧信息
    auto& codeGenTable = irGenerator.getCodeGenTable();
    CodeGenFunctionInfo* funcInfo = nullptr;
    if (codeGenTable.count(funcName)) {
        funcInfo = (CodeGenFunctionInfo*)&codeGenTable.at(funcName);
    }

    pushFuncContext(funcName, funcInfo);
    int frameSize = funcInfo ? funcInfo->frameSize : 8;

    // === 1. 开辟栈空间 ===
    emit("subu $sp, $sp, " + std::to_string(frameSize));
    emit("sw $ra, " + std::to_string(frameSize - 4) + "($sp)");
    emit("sw $fp, " + std::to_string(frameSize - 8) + "($sp)");
    emit("addiu $fp, $sp, " + std::to_string(frameSize));

    // === 2. 参数初始化 (搬运参数) ===
    if (funcInfo) {
        int argIdx = 0;
        int totalParams = funcInfo->paramList.size(); // [新增] 获取参数总数

        for (const std::string& paramName : funcInfo->paramList) {
            if (funcInfo->symbolMap.count(paramName)) {
                // localOffset 是 IR 分配的、位于 FP 和 SP 之间的偏移 (正数，使用时取负)
                int localOffset = funcInfo->symbolMap[paramName].offset;

                if (argIdx < 4) {
                    // === 情况 A: 寄存器参数 ($a0-$a3) ===
                    // 这一步是将寄存器里的值，保存到 FP与SP 之间的坑位中
                    std::string aReg = "$a" + std::to_string(argIdx);
                    emit("sw " + aReg + ", -" + std::to_string(localOffset) + "($fp)");
                }
                else {
                    // === [核心修改] 情况 B: 栈参数 (Index >= 4) ===

                    // 1. 计算来源地址 (在 FP 上方，Caller 的栈空间)
                    // 依据 visitCall 的压栈顺序：ArgN 在 0($fp), ArgN-1 在 4($fp)...
                    int callerOffset = (totalParams - 1 - argIdx) * 4;

                    // 2. 搬运数据: Caller Frame -> Register -> Callee Frame (Local)
                    // 从上方读取
                    emit("lw $t8, " + std::to_string(callerOffset) + "($fp)");
                    // 存入下方 (FP与SP之间)
                    emit("sw $t8, -" + std::to_string(localOffset) + "($fp)");
                }
            }
            argIdx++;
        }
    }
}

void MIPSGenerator::visitFuncExit(IRInstruction* instr) {
    std::string currentFuncName = getCurrentFuncName();
    CodeGenFunctionInfo* currentFuncInfo = getCurrentFuncInfo();

    // 1. 生成函数结束标签
    // 所有的 visitRet 都会跳转到这里
    std::string exitLabel = "__end_" + currentFuncName;
    emitLabel(exitLabel);

    // 2. 再次 Spill (兜底)
    // 如果是 void 函数自然执行到结尾（没有显式 return），
    // 此时寄存器可能还有脏数据，必须写回。
    // 如果是从 visitRet 跳转过来的，这里 spill 是空操作（因为 visitRet 已经 spill 过了），很安全。
    regAlloc.spillAll();

    // 3. 生成统一的函数退出序列 (Epilogue)
    if (currentFuncName == "main") {
        // main 函数特殊处理：终止程序
        emit("li $v0, 10");
        emit("syscall");
    } else {
        // 普通函数：恢复栈帧并返回
        // 逻辑与 visitFuncEntry 中的入栈操作对应

        // 恢复 $ra (返回地址)
        // 注意：visitFuncEntry 中是 sw $ra, frameSize-4($sp)
        // 这里的 $fp 实际上等于当时的 $sp + frameSize
        emit("lw $ra, -4($fp)");

        // 恢复 $sp (栈指针)
        // 将 $sp 恢复到 $fp 的位置（即 Caller 的栈底/Callee 的栈顶）
        emit("move $sp, $fp");

        // 恢复 $fp (帧指针)
        // 对应 visitFuncEntry 中的 sw $fp, frameSize-8($sp)
        // 此时 $sp 已经指回了旧 $sp 的位置，所以直接取偏移量
        emit("lw $fp, -8($sp)");

        // 跳转回调用者
        emit("jr $ra");
    }

    emit(""); // 输出一个空行，美观

    // 4. 上下文出栈
    popFuncContext();
}

void MIPSGenerator::visitBinaryOp(IRInstruction* instr, const std::string& mipsOp) {
    /*// ================= [调试代码开始] =================
    // 引入 iostream (如果文件头没有包含，请确保 #include <iostream>)

    std::cout << "\n========== [DEBUG: visitBinaryOp] ==========" << std::endl;

    // 1. 打印指令操作符 OP
    // 假设 IRInstruction 有 getOpString() 方法，如果没有，可以直接打印 (int)instr->op
    std::cout << "Instruction OP: " << instr->getOpString() << " (MIPS: " << mipsOp << ")" << std::endl;

    // 定义一个 Lambda 辅助函数来打印 Operand 详情
    auto printOperandDebug = [](const std::string& label, Operand* op) {
        std::cout << "  > " << label << ": ";

        if (op == nullptr) {
            std::cout << "nullptr" << std::endl;
            return;
        }

        // 打印地址，方便确认指针是否相同
        std::cout << "Ptr(" << op << ") ";

        // 打印 OperandType
        std::string typeStr;
        switch (op->type) {
            case OperandType::VAR:   typeStr = "VAR";   break;
            case OperandType::TEMP:  typeStr = "TEMP";  break;
            case OperandType::IMM:   typeStr = "IMM";   break;
            case OperandType::LABEL: typeStr = "LABEL"; break;
            default:                 typeStr = "UNKNOWN"; break;
        }
        std::cout << "Type=" << typeStr << ", ";

        // 打印具体数据
        if (op->type == OperandType::IMM) {
            std::cout << "Value=" << op->value;
        } else {
            std::cout << "Name=\"" << op->name << "\"";
            // 如果是 VAR，尝试打印 SymbolEntry 信息
            if (op->type == OperandType::VAR) {
                if (op->symbol) {
                    std::cout << ", SymbolPtr=" << op->symbol
                              << " (Scope=" << op->symbol->scope << ")";
                } else {
                    std::cout << ", SymbolPtr=nullptr (WARNING!)";
                }
            }
        }
        std::cout << std::endl;
    };

    // 2. 分别打印三个操作数
    printOperandDebug("Result", instr->result);
    printOperandDebug("Arg1  ", instr->arg1);
    printOperandDebug("Arg2  ", instr->arg2);

    std::cout << "============================================" << std::endl;
    // ================= [调试代码结束] =================*/

    // 检查是否两个操作数都是立即数
    if (instr->arg1->type == OperandType::IMM && instr->arg2->type == OperandType::IMM) {
        // [修改后]
        // 1. 先分配目标寄存器。如果发生溢出(Spill)，它会使用 $t9 (步骤二已改)。
        //    此时我们还没加载立即数，所以 $t9 是安全的。
        MipsReg r_dest = regAlloc.allocateReg(instr->result);

        // 2. 再加载立即数。此时所有潜在的 Spill 都已处理完毕。
        emit("li $t8, " + std::to_string(instr->arg1->value));
        emit("li $t9, " + std::to_string(instr->arg2->value));

        emit(mipsOp + " " + MipsHelper::getRegName(r_dest) + ", $t8, $t9");
        return;
    }

    MipsReg r1 = regAlloc.getReg(instr->arg1);
    MipsReg r2 = regAlloc.getReg(instr->arg2);
    MipsReg r_dest = regAlloc.allocateReg(instr->result);

    emit(mipsOp + " " + MipsHelper::getRegName(r_dest) + ", " +
         MipsHelper::getRegName(r1) + ", " + MipsHelper::getRegName(r2));
}

void MIPSGenerator::visitUnaryOp(IRInstruction* instr) {
    MipsReg r1 = regAlloc.getReg(instr->arg1);
    MipsReg r_dest = regAlloc.allocateReg(instr->result);

    if (instr->op == IROp::NEG) {
        emit("neg " + MipsHelper::getRegName(r_dest) + ", " + MipsHelper::getRegName(r1));
    } else if (instr->op == IROp::NOT) {
        // seq $dest, $src, $zero (如果 src==0 则 dest=1，实现逻辑非)
        emit("seq " + MipsHelper::getRegName(r_dest) + ", " + MipsHelper::getRegName(r1) + ", $zero");
    }
}

void MIPSGenerator::visitBranch(IRInstruction* instr) {
    // BEQZ arg1, label

    // 1. 先获取条件变量的寄存器
    // 如果它已经在寄存器中，直接复用，避免了 spillAll 后的重载
    MipsReg r_cond = regAlloc.getReg(instr->arg1);
    std::string regName = MipsHelper::getRegName(r_cond);

    // 2. 将所有寄存器状态同步回内存 (Spill All)
    // 这会生成必要的 sw 指令，确保跳转目标的栈内存是干净的。
    // 注意：这会将 r_cond 也写回内存（如果是脏的），这是正确的，
    // 因为目标 Block 可能也需要从内存读取这个值。
    // 同时，这不会破坏 regName 对应的物理寄存器里的值。
    regAlloc.spillAll();

    // 3. 生成跳转指令
    // 使用刚才获取的寄存器名，数值依然有效
    // 【修改前】 emit("beqz " + regName + ", " + instr->result->name);
    // 【修改后】
    emit("beqz " + regName + ", " + getMipsLabel(instr->result->name));
}

// 文件: MIPSGenerator.cpp
// 定位到: visitParam 函数

void MIPSGenerator::visitParam(IRInstruction* instr) {
    // PARAM arg1(val), -, -
    Operand* op = instr->arg1;

    // 获取寄存器中的值
    MipsReg r_val = regAlloc.getReg(op);
    std::string regName = MipsHelper::getRegName(r_val);

    // 【核心修改】
    // 无论这是第几个参数，一律先压入栈中保存。
    // 这样做的好处是：即使参数计算过程中发生了函数调用（嵌套调用），
    // 之前计算好的参数值也已经安全地躺在栈上，不会被覆盖。

    emit("subu $sp, $sp, 4");
    emit("sw " + regName + ", 0($sp)");

    // 注意：我们不再这里维护 paramCounter 或 paramsStackSize
    // 这些逻辑全部移交到 visitCall 中根据函数签名动态处理
}

void MIPSGenerator::visitLoadStore(IRInstruction* instr) {
    if (instr->op == IROp::STORE) {
        Operand* valOp  = instr->result; // 要存的值
        Operand* baseOp = instr->arg1;   // 数组/指针基址
        Operand* offOp  = instr->arg2;   // 偏移量

        // step 1: 必须最先分配/获取所有需要的操作数寄存器
        // 这样做可以确保如果有任何变量需要从内存 load (使用 $t9)，
        // 都会在这个阶段完成，此时我们还没有开始使用 $t9 存基地址。

        MipsReg r_val = regAlloc.getReg(valOp);

        // 特殊处理偏移量：
        // 如果是立即数，不需要寄存器；如果是变量，现在就加载好
        MipsReg r_offset = MipsReg::ZERO;
        if (offOp->type != OperandType::IMM) {
            r_offset = regAlloc.getReg(offOp);
        }

        // step 2: 此时所有寄存器准备就绪，可以安全使用 $t9 计算基地址了
        std::string regAddr = "$t9";

        // 计算基地址 (逻辑保持不变)
        if (baseOp->type == OperandType::TEMP ||
            (baseOp->symbol && baseOp->symbol->isParam)) {
            // 指针/参数/临时变量：值即地址
            MipsReg r_base = regAlloc.getReg(baseOp);
            emit("move " + regAddr + ", " + MipsHelper::getRegName(r_base));
        }
        else {
            // 局部数组或全局数组
            CodeGenFunctionInfo* funcInfo = getCurrentFuncInfo();
            if (funcInfo && funcInfo->symbolMap.count(baseOp->name)) {
                // 局部数组
                int stackOffset = funcInfo->symbolMap[baseOp->name].offset;
                int size = funcInfo->symbolMap[baseOp->name].size;
                emit("addiu " + regAddr + ", $fp, -" + std::to_string(stackOffset + size - 4));
            } else {
                // 全局数组
                std::string label = baseOp->name;
                if (baseOp->symbol && !baseOp->symbol->label.empty()) {
                    label = baseOp->symbol->label;
                }
                emit("la " + regAddr + ", " + getMipsLabel(label));
            }
        }

        // step 3: 生成最终的 sw 指令
        // 此时 $t9 刚被写入基地址，且 r_val 和 r_offset 都在安全寄存器中
        if (offOp->type == OperandType::IMM) {
            emit("sw " + MipsHelper::getRegName(r_val) + ", " +
                 std::to_string(offOp->value) + "(" + regAddr + ")");
        }
        else {
            emit("addu " + regAddr + ", " + regAddr + ", " + MipsHelper::getRegName(r_offset));
            emit("sw " + MipsHelper::getRegName(r_val) + ", 0(" + regAddr + ")");
        }
    }
    else if (instr->op == IROp::LOAD) {
        // LOAD val(result), addr(arg1), offset(arg2)
        // LOAD 逻辑通常是正确的，因为 result 是目标寄存器，arg1 是源
        MipsReg r_base = regAlloc.getReg(instr->arg1);
        MipsReg r_dest = regAlloc.allocateReg(instr->result);
        int offset = instr->arg2->value;
        emit("lw " + MipsHelper::getRegName(r_dest) + ", " + std::to_string(offset) + "(" + MipsHelper::getRegName(r_base) + ")");
    }
    else if (instr->op == IROp::GET_ADDR) {
        // GET_ADDR result, base(arg1), offset(arg2)
        // GET_ADDR 逻辑也需要保持之前的修正 (使用 addiu/la 而不是 lw)
        Operand* baseOp = instr->arg1;
        Operand* offOp  = instr->arg2;
        Operand* resOp  = instr->result;

        MipsReg r_dest = regAlloc.allocateReg(resOp);

        if (baseOp->type == OperandType::TEMP ||
            (baseOp->symbol && baseOp->symbol->isParam)) {
            MipsReg r_base = regAlloc.getReg(baseOp);
            emit("move " + MipsHelper::getRegName(r_dest) + ", " + MipsHelper::getRegName(r_base));
        }
        else {
            CodeGenFunctionInfo* funcInfo = getCurrentFuncInfo();
            if (funcInfo && funcInfo->symbolMap.count(baseOp->name)) {
                // 局部数组
                // 【修正前】 int stackOffset = funcInfo->symbolMap[baseOp->name].offset;
                // 【修正前】 emit("addiu " + MipsHelper::getRegName(r_dest) + ", $fp, -" + std::to_string(stackOffset));

                // 【修正后】 同样需要减去 size
                int stackOffset = funcInfo->symbolMap[baseOp->name].offset;
                int size = funcInfo->symbolMap[baseOp->name].size;
                // 原代码
                // emit("addiu " + MipsHelper::getRegName(r_dest) + ", $fp, -" + std::to_string(stackOffset + size));

                // 【修复后】
                emit("addiu " + MipsHelper::getRegName(r_dest) + ", $fp, -" + std::to_string(stackOffset + size - 4));
            } else {
                std::string label = baseOp->name;
                if (baseOp->symbol && !baseOp->symbol->label.empty()) {
                    label = baseOp->symbol->label;
                }
                // 【修改前】 emit("la " + MipsHelper::getRegName(r_dest) + ", " + label);
                // 【修改后】
                emit("la " + MipsHelper::getRegName(r_dest) + ", " + getMipsLabel(label));
            }
        }

        if (offOp->type == OperandType::IMM) {
            if (offOp->value != 0) {
                emit("addiu " + MipsHelper::getRegName(r_dest) + ", " + MipsHelper::getRegName(r_dest) + ", " + std::to_string(offOp->value));
            }
        } else {
            MipsReg r_offset = regAlloc.getReg(offOp);

            // 【修改点 2】将 add 改为 addu
            // 指针运算不应该触发溢出异常
            emit("addu " + MipsHelper::getRegName(r_dest) + ", " + MipsHelper::getRegName(r_dest) + ", " + MipsHelper::getRegName(r_offset));
        }
    }
}

// 文件: MIPSGenerator.cpp
// 定位到: visitCall 函数

void MIPSGenerator::visitCall(IRInstruction* instr) {
    regAlloc.spillAll(); // 调用前保存现场

    std::string funcName = instr->arg1->name;

    // 1. 确定参数个数
    int paramCount = 0;
    if (irGenerator.getCodeGenTable().count(funcName)) {
        paramCount = irGenerator.getCodeGenTable().at(funcName).paramList.size();
    }

    // === 修正开始 ===

    // 逻辑分析：
    // 在 visitParam 阶段，参数已经按顺序压入栈中。
    // 假设有 N 个参数 (0 到 N-1)，压栈顺序是 P0, P1 ... P(N-1)。
    // 此时栈顶 ($sp) 存储的是 P(N-1)，栈底是 P0。
    //
    // MIPS 传参规则 (针对本编译器约定)：
    // 1. 前 4 个参数 (P0-P3) 放入 $a0-$a3。
    // 2. 后续参数 (P4...) 保留咋栈上。
    //
    // 我们不需要把它们“弹出”再“压入”，只需：
    // 1. 从栈深处把 P0-P3 读出来放到 $a0-$a3。
    // 2. 保持 P4...P(N-1) 在栈顶不动，供被调用函数直接使用。
    // 3. 调用结束后，一次性回收所有参数占用的栈空间。

    // 2. 填充寄存器参数 ($a0 - $a3)
    // 我们只需要处理前 4 个参数，它们目前在栈的深处（高地址）
    int regsToLoad = (paramCount > 4) ? 4 : paramCount;

    for (int i = 0; i < regsToLoad; ++i) {
        // 计算参数 i 在栈中的偏移量
        // 栈顶是第 (paramCount-1) 个参数，偏移为 0
        // 参数 i 的偏移 = (paramCount - 1 - i) * 4
        int offset = (paramCount - 1 - i) * 4;

        std::string regName = "$a" + std::to_string(i);

        // 使用 lw 直接从栈中通过偏移读取，而不是 pop
        emit("lw " + regName + ", " + std::to_string(offset) + "($sp)");
    }

    // 3. 执行跳转
    // 【修改前】 emit("jal " + funcName);
    // 【修改后】
    emit("jal " + getMipsLabel(funcName));

    // 4. 清理参数栈空间
    // 既然 visitParam 压入了 paramCount 个参数，调用结束后我们需要全部回收
    // 无论参数是传给了寄存器还是留在了栈上，这部分空间现在都可以释放了
    if (paramCount > 0) {
        emit("addiu $sp, $sp, " + std::to_string(paramCount * 4));
    }

    // === 修正结束 ===

    // 5. 处理返回值
    if (instr->result) {
        MipsReg r_dest = regAlloc.allocateReg(instr->result);
        emit("move " + MipsHelper::getRegName(r_dest) + ", $v0");
    }
}

void MIPSGenerator::visitRet(IRInstruction* instr) {
    // 1. 处理返回值
    if (instr->result) {
        MipsReg r_val = regAlloc.getReg(instr->result);
        emit("move $v0, " + MipsHelper::getRegName(r_val));
    }

    // 2. 核心：跳转前必须保存当前所有寄存器状态 (Spill)
    // 确保如果有变量在寄存器中被修改，它们的值被写回栈内存
    regAlloc.spillAll();

    // 3. 跳转到函数统一出口
    // 构造退出标签，例如 "__end_funcName"
    std::string currentFuncName = getCurrentFuncName();
    std::string exitLabel = "__end_" + currentFuncName;

    // 【修改前】 emit("j " + exitLabel);
    // 【修改后】
    emit("j " + getMipsLabel(exitLabel));
}

void MIPSGenerator::visitIO(IRInstruction* instr) {
    regAlloc.spillAll();

    if (instr->op == IROp::PRINTINT) {
        MipsReg r_val = regAlloc.getReg(instr->arg1); // 获取参数
        emit("move $a0, " + MipsHelper::getRegName(r_val));
        emit("li $v0, 1"); // syscall 1: print_int
        emit("syscall");
    }
    else if (instr->op == IROp::PRINTSTR) {
        // arg1 是 LABEL
        emit("la $a0, " + instr->arg1->name);
        emit("li $v0, 4"); // syscall 4: print_string
        emit("syscall");
    }
    else if (instr->op == IROp::GETINT) {
        emit("li $v0, 5"); // syscall 5: read_int
        emit("syscall");
        if (instr->result) {
            MipsReg r_dest = regAlloc.allocateReg(instr->result);
            emit("move " + MipsHelper::getRegName(r_dest) + ", $v0");
        }
    }
}

void MIPSGenerator::emit(const std::string& asmCmd) {
    fout << "    " << asmCmd << "\n";
}

void MIPSGenerator::emitLabel(const std::string& label) {
    // 【修改前】 fout << label << ":\n";
    // 【修改后】 统一添加前缀
    fout << getMipsLabel(label) << ":\n";
}