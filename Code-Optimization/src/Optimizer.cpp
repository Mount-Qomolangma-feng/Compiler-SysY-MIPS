#include "Optimizer.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <unordered_map>

Optimizer::Optimizer(std::vector<IRInstruction*>& ir) : originalIR(ir) {}

void Optimizer::execute() {
    splitFunctions();

    for (auto func : functions) {
        buildCFG(func);

        bool changed = true;
        int passCount = 0;

        // 迭代执行直到不动点 (或者限制次数防止死循环)
        while (changed && passCount < 10) {
            changed = false;
            passCount++;

            // 块内优化
            for (auto block : func->blocks) {
                // 顺序很重要：先折叠常量，再做代数化简，再做CSE
                if (passConstantFolding(block)) changed = true;
                if (passAlgebraicSimplification(block)) changed = true;
                if (passLocalCSE(block)) changed = true;
                if (passCopyPropagation(block)) changed = true;
            }

            // 全局/函数级优化
            if (passDeadCodeElimination(func)) changed = true;
        }
    }
}

std::vector<IRInstruction*> Optimizer::getOptimizedIR() {
    std::vector<IRInstruction*> finalIR;

    // 1. 先放回全局定义
    for (auto instr : globalDefines) {
        finalIR.push_back(instr);
    }

    // 2. 依次压平每个函数
    for (auto func : functions) {
        std::vector<IRInstruction*> funcIR = flatten(func);
        finalIR.insert(finalIR.end(), funcIR.begin(), funcIR.end());
    }

    return finalIR;
}

// === 构建与拆分逻辑 ===

void Optimizer::splitFunctions() {
    Function* currentFunc = nullptr;

    // 使用索引遍历，以便可以访问下一条指令 (i + 1)
    for (size_t i = 0; i < originalIR.size(); ++i) {
        IRInstruction* instr = originalIR[i];

        // 判定是否为新函数的开始
        bool isFuncStart = false;

        // 逻辑：如果当前是 LABEL，且它是 "main" 或者 下一条指令是 FUNC_ENTRY
        if (instr->op == IROp::LABEL) {
            // 特判 main (以防 main 函数没有 FUNC_ENTRY，虽然通常应该有)
            if (instr->result->name == "main") {
                isFuncStart = true;
            }
                // 核心修复：检查下一条指令是否为 FUNC_ENTRY
            else if (i + 1 < originalIR.size() && originalIR[i+1]->op == IROp::FUNC_ENTRY) {
                isFuncStart = true;
            }
        }

        if (isFuncStart) {
            // 1. 创建新函数
            currentFunc = new Function(instr->result->name);
            functions.push_back(currentFunc);

            // 2. 创建该函数的第一个基本块
            BasicBlock* block = new BasicBlock(0);
            block->instructions.push_back(instr); // 将函数名 Label 加入块
            currentFunc->blocks.push_back(block);
        }
        else if (currentFunc) {
            // 3. 将指令加入当前函数的当前块
            // 注意：这里简单地加到最后一个块，具体的块切分会在 buildCFG 中进行
            currentFunc->blocks.back()->instructions.push_back(instr);
        }
        else {
            // 4. 函数定义之外的指令（如全局变量定义）
            globalDefines.push_back(instr);
        }
    }
}

void Optimizer::buildCFG(Function* func) {
    // 这里为了简化，我们只做基本的块切分，不做复杂的图连接
    // 因为 Local CSE 和 常量折叠 主要依赖块内信息
    // 真正的 CFG 需要解析 JUMP/BEQZ 建立 preds/succs

    std::vector<BasicBlock*> newBlocks;
    BasicBlock* currBlock = nullptr;
    int blockId = 0;

    // 提取当前函数的指令流
    std::vector<IRInstruction*> rawInstrs;
    for (auto b : func->blocks) {
        for (auto i : b->instructions) rawInstrs.push_back(i);
        delete b; // 删除旧的粗糙块
    }
    func->blocks.clear();

    for (auto instr : rawInstrs) {
        // 如果遇到 Label 或 上一条是跳转，开始新块
        bool isLeader = (instr->op == IROp::LABEL) ||
                        (instr->op == IROp::FUNC_ENTRY) ||
                        (currBlock == nullptr);

        if (!isLeader && !currBlock->instructions.empty()) {
            IROp lastOp = currBlock->instructions.back()->op;
            if (lastOp == IROp::JUMP || lastOp == IROp::BEQZ || lastOp == IROp::RET) {
                isLeader = true;
            }
        }

        if (isLeader) {
            currBlock = new BasicBlock(blockId++);
            newBlocks.push_back(currBlock);
        }
        currBlock->instructions.push_back(instr);
    }
    func->blocks = newBlocks;

    // 【新增】构建块之间的连接关系 (preds/succs)
    buildCFGEdges(func);
}

void Optimizer::addEdge(BasicBlock* from, BasicBlock* to) {
    for (auto b : from->succs) if (b == to) return;
    from->succs.push_back(to);
    to->preds.push_back(from);
}

// 核心函数：构建控制流图的边
void Optimizer::buildCFGEdges(Function* func) {
    auto& blocks = func->blocks;
    if (blocks.empty()) return;

    // ==========================================
    // 阶段 1: 建立 Label -> BasicBlock 的映射表
    // ==========================================
    // 这样我们遇到跳转指令 "JUMP L1" 时，能知道 "L1" 对应哪个内存地址的 Block
    std::map<std::string, BasicBlock*> labelToBlock;

    for (auto block : blocks) {
        // 扫描块内的指令，寻找 LABEL
        for (auto instr : block->instructions) {
            if (instr->op == IROp::LABEL) {
                // 记录 Label 名字对应的块指针
                // 注意：SysY 中 Label 名字通常是唯一的
                labelToBlock[instr->result->name] = block;
            }
        }
    }

    // ==========================================
    // 阶段 2: 遍历所有块，分析出口并连接
    // ==========================================
    for (size_t i = 0; i < blocks.size(); ++i) {
        BasicBlock* currBlock = blocks[i];

        // 防御性编程：处理空块 (虽少见，但在优化过程中可能产生)
        if (currBlock->instructions.empty()) {
            // 空块默认 Fall-through 到下一块
            if (i + 1 < blocks.size()) {
                addEdge(currBlock, blocks[i + 1]);
            }
            continue;
        }

        // 获取该块的最后一条指令，它决定了控制流的去向
        IRInstruction* lastInstr = currBlock->instructions.back();

        if (lastInstr->op == IROp::JUMP) {
            // === 情况 A: 无条件跳转 (JUMP Target) ===
            // 逻辑：必定跳转到 Target，不会执行下一块
            if (lastInstr->result && labelToBlock.count(lastInstr->result->name)) {
                addEdge(currBlock, labelToBlock[lastInstr->result->name]);
            } else {
                // 错误处理：跳转目标不存在 (通常不应发生)
                // std::cerr << "[CFG Error] Jump target not found: " << lastInstr->result->name << std::endl;
            }
        }
        else if (lastInstr->op == IROp::BEQZ) {
            // === 情况 B: 条件跳转 (BEQZ Target, Cond) ===
            // 逻辑：
            // 1. 如果条件成立 -> 跳转到 Target
            // 2. 如果条件不成立 -> 执行下一块 (Fall-through)

            // 连接分支目标
            if (lastInstr->result && labelToBlock.count(lastInstr->result->name)) {
                addEdge(currBlock, labelToBlock[lastInstr->result->name]);
            }

            // 连接自然后继 (Fall-through)
            if (i + 1 < blocks.size()) {
                addEdge(currBlock, blocks[i + 1]);
            }
        }
        else if (lastInstr->op == IROp::RET) {
            // === 情况 C: 返回 (RET) ===
            // 逻辑：函数结束，没有后继块
            // 不需要做任何连接
        }
        else {
            // === 情况 D: 普通指令 (ADD, ASSIGN, CALL 等) ===
            // 逻辑：顺序执行到下一块
            // 注意：CALL 虽然是跳转，但在 CFG 中视为普通指令，因为它会返回
            if (i + 1 < blocks.size()) {
                addEdge(currBlock, blocks[i + 1]);
            }
        }
    }
}

std::vector<IRInstruction*> Optimizer::flatten(Function* func) {
    std::vector<IRInstruction*> res;
    for (auto block : func->blocks) {
        for (auto instr : block->instructions) {
            res.push_back(instr);
        }
    }
    return res;
}

// === 核心优化 Passes ===

// 1. 常量折叠：直接计算出立即数结果
bool Optimizer::passConstantFolding(BasicBlock* block) {
    bool changed = false;
    // 块内常量表：Key=变量名, Value=当前常量值
    std::map<std::string, int> constValues;

    for (auto it = block->instructions.begin(); it != block->instructions.end(); ++it) {
        IRInstruction* instr = *it;

        // ==========================================================
        // 阶段 1: 尝试替换源操作数 (Uses: arg1, arg2)
        // ==========================================================

        // 1. 替换 arg1
        // 【保护】内存指令的 arg1 通常是基地址 (如 LOAD t0, t1, 4)，不能替换为立即数
        bool isMemBase = (instr->op == IROp::STORE || instr->op == IROp::LOAD || instr->op == IROp::GET_ADDR);
        if (instr->arg1 && instr->arg1->type != OperandType::IMM && constValues.count(instr->arg1->toString())) {
            if (!isMemBase) {
                std::string key = instr->arg1->toString();
                auto it = constValues.find(key);
                if (it != constValues.end()) {
                    int v = it->second;
                    //delete instr->arg1;
                    instr->arg1 = new Operand(v);
                    changed = true;
                }

            }
        }

        // 2. 替换 arg2
        if (instr->arg2 && instr->arg2->type != OperandType::IMM && constValues.count(instr->arg2->toString())) {
            std::string key = instr->arg2->toString();
            auto it = constValues.find(key);
            if (it != constValues.end()) {
                int v = it->second;
                //delete instr->arg2;
                instr->arg2 = new Operand(v);
                changed = true;
            }

        }

        // ==========================================================
        // 阶段 2: 尝试替换结果操作数 (当 Result 实际上是 Use 时)
        // ==========================================================

        // 某些指令的 result 字段实际上是被使用的值 (Use)，而不是被定义的值 (Def)
        // 例如: RET t0 (使用 t0), PARAM t0 (使用 t0), PRINTINT t0 (使用 t0)
        // 注意: STORE 的 result 是要存的值 (Use), arg1 是地址
        bool isResultUse = (instr->op == IROp::STORE ||
                            instr->op == IROp::RET ||
                            instr->op == IROp::PARAM ||
                            instr->op == IROp::PRINTINT ||
                            instr->op == IROp::PRINTSTR); // PRINTSTR arg1通常是Label，但也可能是变量

        if (isResultUse && instr->result && instr->result->type != OperandType::IMM && constValues.count(instr->result->toString())) {
            std::string key = instr->result->toString();          // 先保存 key
            auto it = constValues.find(key);                      // 用 find 更稳健
            if (it != constValues.end()) {
                int v = it->second;                               // 先取常量值
                //delete instr->result;                             // 再释放
                instr->result = new Operand(v);                   // 再替换
                changed = true;
            }

        }

        // ==========================================================
        // 阶段 3: 处理定义 (Definitions) - 尝试计算并记录常量
        // ==========================================================

        bool isConstantDef = false;

        // 只有当指令是定义变量 (即 result 不是 Use) 时，才处理
        if (!isResultUse && instr->result && (instr->result->type == OperandType::TEMP || instr->result->type == OperandType::VAR)) {

            Operand* res = instr->result;

            // 【核心修正】安全性检查：只有临时变量或局部变量才允许常量折叠
            // 全局变量可能被函数调用修改，块内分析无法感知，因此视为不安全
            bool isSafeToFold = false;

            if (res->type == OperandType::TEMP) {
                // 临时变量总是安全的 (SSA特性或局部生命周期)
                isSafeToFold = true;
            } else if (res->type == OperandType::VAR && res->symbol) {
                // 变量: 必须是局部变量 (scope > 1) 且非静态
                // 假设 scope 1 是全局作用域
                if (res->symbol->scope > 1 && res->symbol->type != SymbolType::StaticInt && res->symbol->type != SymbolType::StaticIntArray) {
                    isSafeToFold = true;
                }
            }

            // 只有安全的变量才尝试计算常量
            if (isSafeToFold) {

                // A. 简单的赋值传播 (ASSIGN x, #imm)
                if (instr->op == IROp::ASSIGN && instr->arg1 && instr->arg1->type == OperandType::IMM) {
                    constValues[res->toString()] = instr->arg1->value;
                    isConstantDef = true;
                }
                    // B. 二元运算折叠 (ADD x, #imm1, #imm2)
                else if (instr->arg1 && instr->arg1->type == OperandType::IMM &&
                         instr->arg2 && instr->arg2->type == OperandType::IMM) {

                    int v1 = instr->arg1->value;
                    int v2 = instr->arg2->value;
                    int resultVal = 0;
                    bool calculable = true;

                    switch (instr->op) {
                        case IROp::ADD: resultVal = v1 + v2; break;
                        case IROp::SUB: resultVal = v1 - v2; break;
                        case IROp::MUL: resultVal = v1 * v2; break;
                        case IROp::DIV:
                            if (v2 != 0) resultVal = v1 / v2;
                            else calculable = false; // 除零保护
                            break;
                        case IROp::MOD:
                            if (v2 != 0) resultVal = v1 % v2;
                            else calculable = false;
                            break;
                            // 逻辑运算
                        case IROp::GT:  resultVal = (v1 > v2); break;
                        case IROp::GE:  resultVal = (v1 >= v2); break;
                        case IROp::LT:  resultVal = (v1 < v2); break;
                        case IROp::LE:  resultVal = (v1 <= v2); break;
                        case IROp::EQ:  resultVal = (v1 == v2); break;
                        case IROp::NEQ: resultVal = (v1 != v2); break;
                            // 移位运算 (用于乘除优化产生的指令)
                        case IROp::SLL: resultVal = v1 << v2; break;
                        case IROp::SRA: resultVal = v1 >> v2; break;
                        default: calculable = false; break;
                    }

                    if (calculable) {
                        // 替换当前指令为 ASSIGN #result
                        instr->op = IROp::ASSIGN;

                        //delete instr->arg1;
                        instr->arg1 = new Operand(resultVal); // arg1 变成结果立即数

                        if (instr->arg2) {
                            //delete instr->arg2;
                            instr->arg2 = nullptr;
                        }

                        constValues[res->toString()] = resultVal;
                        isConstantDef = true;
                        changed = true;
                    }
                }
                    // C. 一元运算折叠 (NEG x, #imm)
                else if (instr->op == IROp::NEG && instr->arg1 && instr->arg1->type == OperandType::IMM) {
                    int val = -instr->arg1->value;

                    instr->op = IROp::ASSIGN;
                    //delete instr->arg1;
                    instr->arg1 = new Operand(val);

                    constValues[res->toString()] = val;
                    isConstantDef = true;
                    changed = true;
                }
                else if (instr->op == IROp::NOT && instr->arg1 && instr->arg1->type == OperandType::IMM) {
                    int val = !instr->arg1->value;

                    instr->op = IROp::ASSIGN;
                    //delete instr->arg1;
                    instr->arg1 = new Operand(val);

                    constValues[res->toString()] = val;
                    isConstantDef = true;
                    changed = true;
                }
            }
        }

        // ==========================================================
        // 阶段 4: 状态失效 (Invalidation)
        // ==========================================================

        // 如果变量被重新定义了 (Result Def)，但没有计算出新的常量值 (isConstantDef == false)
        // 或者该变量是不安全的 (如全局变量被修改)，必须从表中移除！
        // 否则后续指令会错误地使用旧的常量值。
        if (!isResultUse && instr->result && !isConstantDef) {
            if (instr->result->type == OperandType::TEMP || instr->result->type == OperandType::VAR) {
                constValues.erase(instr->result->toString());
            }
        }
    }

    return changed;
}

// 2. 代数化简与强度削减：x*1=x, x*0=0, x*8=x<<3
bool Optimizer::passAlgebraicSimplification(BasicBlock* block) {
    bool changed = false;
    for (auto instr : block->instructions) {
        if (!instr->arg1 || !instr->arg2) continue; // 必须是二元运算

        // 1. 规范化：确保 arg2 是立即数 (针对满足交换律的 ADD/MUL)
        // 如果 arg1 是立即数，arg2 是变量，交换它们，方便后续统一处理
        if (instr->arg1->type == OperandType::IMM && instr->arg2->type != OperandType::IMM) {
            if (instr->op == IROp::ADD || instr->op == IROp::MUL) {
                std::swap(instr->arg1, instr->arg2);
                changed = true;
            }
        }

        // 只有当 arg2 是立即数时，才进行代数化简
        if (instr->arg2->type == OperandType::IMM) {
            int val = instr->arg2->value;
            int power = 0;

            // === 乘法优化 (MUL -> SLL 是安全的) ===
            if (instr->op == IROp::MUL) {
                if (val == 0) {
                    // x * 0 = 0 -> ASSIGN 0
                    instr->op = IROp::ASSIGN;
                    delete instr->arg1; instr->arg1 = new Operand(0);
                    delete instr->arg2; instr->arg2 = nullptr;
                    changed = true;
                } else if (val == 1) {
                    // x * 1 = x -> ASSIGN x
                    instr->op = IROp::ASSIGN;
                    delete instr->arg2; instr->arg2 = nullptr;
                    changed = true;
                } else if (isPowerOfTwo(val, power)) {
                    // x * 2^k = x << k
                    // 乘法可以使用逻辑左移优化，没有负数陷阱
                    instr->op = IROp::SLL;
                    delete instr->arg2; instr->arg2 = new Operand(power);
                    changed = true;
                }
            }
                // === 除法优化 (已修正：移除不安全的 SRA 优化) ===
            else if (instr->op == IROp::DIV) {
                if (val == 1) {
                    // x / 1 = x (这是安全的恒等变换)
                    instr->op = IROp::ASSIGN;
                    delete instr->arg2; instr->arg2 = nullptr;
                    changed = true;
                }
                // 【注意】这里移除了 isPowerOfTwo -> SRA 的优化块
                // 避免了负数除法（向零取整）和算术右移（向下取整）结果不一致的问题。
                // 所有的除法运算将保留为 DIV 指令，交给硬件正确处理。
            }
                // === 加法优化 ===
            else if (instr->op == IROp::ADD) {
                if (val == 0) { // x + 0 = x
                    instr->op = IROp::ASSIGN;
                    delete instr->arg2; instr->arg2 = nullptr;
                    changed = true;
                }
            }
                // === 减法优化 ===
            else if (instr->op == IROp::SUB) {
                if (val == 0) { // x - 0 = x
                    instr->op = IROp::ASSIGN;
                    delete instr->arg2; instr->arg2 = nullptr;
                    changed = true;
                }
            }
        }
    }
    return changed;
}

// 3. 局部公共子表达式删除 (Local CSE)
bool Optimizer::passLocalCSE(BasicBlock* block) {
    bool changed = false;
    // Map: "OP_Arg1_Arg2" -> Result Variable Name
    std::unordered_map<std::string, std::string> exprMap;

    for (auto instr : block->instructions) {
        // 跳过有副作用的指令 (CALL, LOAD, GETINT)
        // 注意：LOAD 虽然无副作用，但内存值可能变，简单CSE通常跳过
        if (instr->op == IROp::CALL || instr->op == IROp::LOAD || instr->op == IROp::GETINT || instr->op == IROp::GET_ADDR) {

            // 【可选修正】如果遇到 CALL，为了绝对安全，应该移除所有涉及全局变量的表达式
            // 这里为了保持代码精简，且主要修复操作数重定义 Bug，暂时保留 continue 逻辑
            continue;
        }

        // ==========================================================
        // 【修改点 1】 变量重定义检查 (Invalidation Logic)
        // ==========================================================
        if (instr->result) {
            std::string definedVar = instr->result->toString();

            // 构造查找模式：在 Key 中查找 "_变量名"
            // Key 的格式是 "OP_Arg1_Arg2"，所以操作数前一定有下划线
            std::string argPattern = "_" + definedVar;

            for (auto it = exprMap.begin(); it != exprMap.end(); ) {
                bool shouldRemove = false;

                // A. 检查结果 (Result) 是否被覆盖 (原逻辑)
                // 如果 Map 中存的旧结果变量就是当前被重定义的变量，删除它
                if (it->second == definedVar) {
                    shouldRemove = true;
                }
                    // B. 【新增逻辑】 检查操作数 (Operand) 是否被覆盖
                    // 如果 Map 的 Key (表达式) 中使用了当前被重定义的变量，删除它
                else {
                    size_t pos = it->first.find(argPattern);

                    // 循环查找，防止匹配到类似 "_t11" 中的 "_t1"
                    while (pos != std::string::npos) {
                        size_t endPos = pos + argPattern.length();

                        // 确认匹配的是完整单词：
                        // 匹配结束位置必须是字符串末尾，或者下一个字符是下划线 '_'
                        if (endPos == it->first.length() || it->first[endPos] == '_') {
                            shouldRemove = true;
                            break;
                        }
                        // 继续向后查找
                        pos = it->first.find(argPattern, endPos);
                    }
                }

                if (shouldRemove) {
                    it = exprMap.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // ==========================================================
        // CSE 替换逻辑 (保持不变，但逻辑更安全了)
        // ==========================================================
        if (instr->result && instr->arg1 && instr->arg2) {
            // 构建 Key
            std::string key = instr->getOpString() + "_" + instr->arg1->toString() + "_" + instr->arg2->toString();

            // 交换律支持 (ADD, MUL)
            if (instr->op == IROp::ADD || instr->op == IROp::MUL) {
                std::string altKey = instr->getOpString() + "_" + instr->arg2->toString() + "_" + instr->arg1->toString();
                if (exprMap.count(altKey)) key = altKey;
            }

            if (exprMap.count(key)) {
                // 发现重复计算，执行替换
                std::string prevRes = exprMap[key];

                instr->op = IROp::ASSIGN;

                // 释放旧参数内存
                if (instr->arg1) delete instr->arg1;
                instr->arg1 = new Operand(prevRes, OperandType::TEMP); // 复用之前的计算结果

                if (instr->arg2) { delete instr->arg2; instr->arg2 = nullptr; }

                changed = true;
            } else {
                // 记录新表达式
                // 只有非赋值指令才值得记录 (ASSIGN t1, t2 没必要记录为 ASSIGN_t2_null)
                if (instr->op != IROp::ASSIGN) {
                    exprMap[key] = instr->result->toString();
                }
            }
        }
    }
    return changed;
}

// 4. 复写传播：消除 t1 = t2 后的 t1 使用
// Optimizer.cpp -> passCopyPropagation

bool Optimizer::passCopyPropagation(BasicBlock* block) {
    bool changed = false;
    // Map: 目标变量名 -> 源操作数 (存储副本以保证内存安全)
    std::map<std::string, Operand> copies;

    for (auto instr : block->instructions) {

        // ==========================================================
        // 【修正 1】 处理 CALL 的副作用 (Side Effects)
        // 函数调用可能会修改所有全局变量，因此所有依赖全局变量的副本必须失效
        // ==========================================================
        if (instr->op == IROp::CALL) {
            auto it = copies.begin();
            while (it != copies.end()) {
                // 如果源操作数是全局变量 (Scope == 1 且非 Static)
                Operand& src = it->second;
                bool isGlobal = (src.type == OperandType::VAR && src.symbol && src.symbol->scope == 1);

                if (isGlobal) {
                    it = copies.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // ==========================================================
        // 1 & 2. 替换 Use (arg1, arg2)
        // ==========================================================
        // Helper lambda to replace operand
        auto tryReplace = [&](Operand*& op) {
            if (op && copies.count(op->toString())) {
                Operand& replacement = copies[op->toString()];

                // 【修正 3】 后端保护：如果替换成 IMM，需确认后端是否支持
                // 为了安全，建议只在 CopyProp 中传播变量。常量传播由 passConstantFolding 负责。
                // 如果你确定要传播立即数，请确保 MIPSGenerator 能处理 `DIV t1, t2, 5` 这种情况
                if (replacement.type == OperandType::IMM) {
                    // 策略：不传播立即数，防止破坏指令格式 (如 DIV, MIPS的 div 不支持立即数)
                    return;
                }

                // 执行替换 (深拷贝)
                delete op;
                op = new Operand(replacement.name, replacement.type);
                op->value = replacement.value;
                op->symbol = replacement.symbol;
                changed = true;
            }
        };

        tryReplace(instr->arg1);
        tryReplace(instr->arg2);

        // ==========================================================
        // 3. 替换 Result (当它作为 Use 时)
        // ==========================================================
        bool isResultUse = (instr->op == IROp::STORE ||
                            instr->op == IROp::RET ||
                            instr->op == IROp::PARAM ||
                            instr->op == IROp::PRINTINT ||
                            instr->op == IROp::PRINTSTR);

        if (isResultUse) {
            tryReplace(instr->result);
        }

        // ==========================================================
        // 4. 维护 Copies 集合 (Def & Kill)
        // ==========================================================

        // 4a. Gen: 记录副本 (ASSIGN)
        if (instr->op == IROp::ASSIGN && instr->result && instr->arg1) {
            // 记录副本：深拷贝 Operand 内容，防止悬空指针
            // 只记录 变量->变量 的映射 (忽略立即数，见修正3)
            if (instr->arg1->type == OperandType::TEMP || instr->arg1->type == OperandType::VAR) {
                copies[instr->result->toString()] = *instr->arg1;
            }
        }
            // 4b. Kill: 处理定义造成的失效
        else if (instr->result && !isResultUse) {
            std::string defVar = instr->result->toString();

            // 1. 自身被重定义：移除 t1 -> ...
            if (copies.count(defVar)) {
                copies.erase(defVar);
            }

            // 2. 作为源被修改：移除 ... -> t1
            auto it = copies.begin();
            while (it != copies.end()) {
                // 检查 Map 的 Value (Source) 是否等于当前定义的变量
                if (it->second.toString() == defVar) {
                    it = copies.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
    return changed;
}

// 5. 死代码删除 (简单版：基于活跃度分析比较复杂，这里做简单的引用计数)
bool Optimizer::passDeadCodeElimination(Function* func) {
    bool changed = false;
    std::set<std::string> usedVars;

    // 1. 扫描所有使用
    for (auto block : func->blocks) {
        for (auto instr : block->instructions) {
            if (instr->arg1) usedVars.insert(instr->arg1->toString());
            if (instr->arg2) usedVars.insert(instr->arg2->toString());

            // 也是 "使用" 的情况：作为函数参数，返回值，打印值
            if (instr->op == IROp::PARAM && instr->arg1) usedVars.insert(instr->arg1->toString());
            if (instr->op == IROp::RET && instr->result) usedVars.insert(instr->result->toString());
            if (instr->op == IROp::BEQZ && instr->result) usedVars.insert(instr->result->toString()); // 条件判断
            if (instr->op == IROp::STORE && instr->result) usedVars.insert(instr->result->toString());        }
    }

    // 2. 删除无用定义
    for (auto block : func->blocks) {
        auto it = block->instructions.begin();
        while (it != block->instructions.end()) {
            IRInstruction* instr = *it;
            bool remove = false;

            // 如果是有结果的指令，且结果从未被使用，且无副作用
            if (instr->result && (instr->result->type == OperandType::TEMP) && !hasSideEffect(instr)) {
                if (usedVars.find(instr->result->toString()) == usedVars.end()) {
                    remove = true;
                }
            }
            // 特殊：ASSIGN 的目标如果不是 Temp (即是 Var)，不能随便删，可能跨块使用
            // 这里为了安全，只删临时变量的死代码

            if (remove) {
                // delete instr; // 如果不需要保留指针的话
                it = block->instructions.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    return changed;
}

// === 工具函数 ===

bool Optimizer::isPowerOfTwo(int n, int& power) {
    if (n <= 0) return false;
    if ((n & (n - 1)) == 0) {
        power = 0;
        while (n > 1) {
            n >>= 1;
            power++;
        }
        return true;
    }
    return false;
}

bool Optimizer::hasSideEffect(IRInstruction* instr) {
    switch (instr->op) {
        case IROp::CALL:
        case IROp::STORE:
        case IROp::PRINTINT:
        case IROp::PRINTSTR:
        case IROp::GETINT:
        case IROp::RET:
        case IROp::JUMP:
        case IROp::BEQZ:
        case IROp::FUNC_ENTRY:
        case IROp::FUNC_EXIT:
        case IROp::LABEL:
            return true;
        default:
            return false;
    }
}