#ifndef COMPILER_OPTIMIZER_H
#define COMPILER_OPTIMIZER_H

#include "OptimizerStructs.h"
#include "IR.h"
#include <vector>
#include <map>

class Optimizer {
public:
    Optimizer(std::vector<IRInstruction*>& ir);

    // 执行所有优化 pass
    void execute();

    // 获取优化后的 IR 指令列表 (用于替换 IRGenerator 中的 instructions)
    std::vector<IRInstruction*> getOptimizedIR();

private:
    std::vector<IRInstruction*> originalIR;
    std::vector<Function*> functions;
    std::vector<IRInstruction*> globalDefines; // 全局变量定义保留在函数外

    // === 辅助构建函数 ===
    void splitFunctions(); // 将线性IR切分为函数
    void buildCFG(Function* func); // 构建控制流图

    void buildCFGEdges(Function* func); // 新增声明
    void addEdge(BasicBlock* from, BasicBlock* to); // 新增声明

    std::vector<IRInstruction*> flatten(Function* func); // 将CFG压平回线性IR

    // === 优化 Passes ===

    // 1. 常量传播与折叠 (Constant Propagation & Folding)
    // 对应评分：减少计算指令，尤其是预计算
    bool passConstantFolding(BasicBlock* block);

    // 2. 代数化简与强度削减 (Algebraic Simplification & Strength Reduction)
    // 对应评分：DIV*15 -> SRA*1, MULT*5 -> SLL*1
    bool passAlgebraicSimplification(BasicBlock* block);

    // 3. 局部公共子表达式删除 (Local Common Subexpression Elimination)
    // 对应评分：减少重复计算
    bool passLocalCSE(BasicBlock* block);

    // 4. 复写传播 (Copy Propagation)
    // 辅助 CSE，消除 t1 = t2 这种冗余赋值
    bool passCopyPropagation(BasicBlock* block);

    // 5. 死代码删除 (Dead Code Elimination)
    // 对应评分：减少指令总数
    bool passDeadCodeElimination(Function* func);

    // === 工具函数 ===
    bool isConstant(Operand* op);
    int getConstantValue(Operand* op);
    bool isPowerOfTwo(int n, int& power); // 检查是否是2的幂次

    // 检查是否有副作用 (IO, 内存写入, 函数调用)
    bool hasSideEffect(IRInstruction* instr);
};

#endif // COMPILER_OPTIMIZER_H