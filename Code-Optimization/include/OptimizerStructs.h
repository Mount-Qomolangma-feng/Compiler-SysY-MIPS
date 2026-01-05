#ifndef COMPILER_OPTIMIZERSTRUCTS_H
#define COMPILER_OPTIMIZERSTRUCTS_H

#include "IR.h"
#include <vector>
#include <set>
#include <map>
#include <list>

// 基本块：包含顺序执行的指令列表，中间没有跳转
struct BasicBlock {
    int id;
    std::string label; // 块入口标签（如果有）
    std::list<IRInstruction*> instructions; // 使用list方便插入/删除

    std::vector<BasicBlock*> preds; // 前驱块
    std::vector<BasicBlock*> succs; // 后继块

    BasicBlock(int i) : id(i) {}
};

// 函数：包含一系列基本块
struct Function {
    std::string name;
    std::vector<BasicBlock*> blocks; // 所有的基本块

    Function(std::string n) : name(n) {}

    ~Function() {
        for (auto b : blocks) delete b;
    }
};

#endif // COMPILER_OPTIMIZERSTRUCTS_H