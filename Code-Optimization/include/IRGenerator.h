#ifndef COMPILER_IRGENERATOR_H
#define COMPILER_IRGENERATOR_H

#include "IR.h"
#include "TreeNode.h"
#include "SymbolTable.h"
#include <vector>
#include <stack>
#include <map>
#include <string>
#include <set> // 记得在文件头部包含头文件

// === 【新增】服务于代码生成的符号表结构 ===

// 1. 代码生成专用的符号表项
struct CodeGenSymbolEntry {
    std::string name;   // 变量名或临时变量名 (如 "a", "t0")
    int offset;         // 栈偏移量 (相对于 FP)
    int size;           // 字节大小 (通常为4)
    bool isTemp;        // 是否为临时变量
    bool isParam;       // 是否为参数

    CodeGenSymbolEntry(std::string n = "", int o = 0, bool temp = false, bool param = false)
            : name(n), offset(o), size(4), isTemp(temp), isParam(param) {}
};

// 2. 函数的栈帧布局信息
struct CodeGenFunctionInfo {
    std::string funcName;
    int frameSize;      // 当前栈帧总大小 (FP/RA + Params + Locals + Temps)

    // 存储该函数内所有变量(含临时变量)的布局信息
    // Key: 变量名/临时变量名, Value: 符号信息
    std::map<std::string, CodeGenSymbolEntry> symbolMap;

    // 【新增】保存参数定义的顺序，以便对应 $a0-$a3
    std::vector<std::string> paramList;

    CodeGenFunctionInfo() : frameSize(0) {}
};

// ==========================================

class IRGenerator {
public:
    IRGenerator(SymbolTable& symTable);
    void generate(std::shared_ptr<TreeNode> root);
    void printIR(const std::string& filename);

    const std::vector<IRInstruction*>& getInstructions() const { return instructions; }
    const std::map<std::string, std::string>& getStringConstants() const { return stringConstants; }

    // 【新增】获取构建好的 MIPS 代码生成符号表
    const std::map<std::string, CodeGenFunctionInfo>& getCodeGenTable() const { return mipsCodeGenTable; }

    // 【新增】输出 MIPS 代码生成专用的符号表信息到文件
    void dumpMipsCodeGenTable(const std::string& filename);

    // 在 IRGenerator.h 的 public: 下方添加
    void setInstructions(const std::vector<IRInstruction*>& newInstrs) {
        this->instructions = newInstrs;
    }

private:
    SymbolTable& symbolTable;
    std::vector<IRInstruction*> instructions;

    // === 【修复】添加缺失的成员变量声明 ===
    std::map<std::string, std::string> stringConstants; // Label -> String Content
    // ========================================

    int tempCounter = 0;
    int labelCounter = 0;
    int stringCounter = 0;

    // 作用域遍历同步
    int iterScopeId = 0;
    std::vector<Scope*> scopeStack;

    // === 【新增】核心数据结构 ===
    // 全局映射：函数名 -> 该函数的布局信息
    std::map<std::string, CodeGenFunctionInfo> mipsCodeGenTable;

    // 指向当前正在处理的函数的布局信息 (方便快速访问)
    CodeGenFunctionInfo* currentCodeGenInfo = nullptr;
    // ==========================

    // [新增] 记录当前指令位置已经“激活”（已完成声明）的符号
    // 只有在这个集合里的符号，getVar 才有资格返回，从而实现“先声明后使用”
    std::set<SymbolEntry*> activeSymbols;

    // 辅助函数
    Operand* newTemp();
    Operand* newLabel();
    Operand* newImm(int value);
    Operand* getVar(const std::string& name); // 从当前scopeStack查找
    std::string addStringConstant(const std::string& content);
    void emit(IROp op, Operand* result = nullptr, Operand* arg1 = nullptr, Operand* arg2 = nullptr);
    void emitLabel(Operand* label);

    void enterScope();
    void exitScope();

    // [新增] 在当前 IRGenerator 维护的作用域栈中查找符号
    SymbolEntry* lookupSymbol(const std::string& name);

    // 访问函数
    void visitCompUnit(const std::shared_ptr<TreeNode>& node);
    void visitFuncDef(const std::shared_ptr<TreeNode>& node);
    void visitMainFuncDef(const std::shared_ptr<TreeNode>& node);

    // isFunctionBody: 关键参数，用于同步 SemanticAnalyzer 的作用域逻辑
    void visitBlock(const std::shared_ptr<TreeNode>& node, bool isFunctionBody = false);

    // === 新增：声明与定义处理函数 ===
    void visitVarDecl(const std::shared_ptr<TreeNode>& node);
    void visitConstDecl(const std::shared_ptr<TreeNode>& node);
    void visitVarDef(const std::shared_ptr<TreeNode>& node);
    void visitConstDef(const std::shared_ptr<TreeNode>& node);

    void visitStmt(const std::shared_ptr<TreeNode>& node);

    // 处理 ForStmt (特殊的平铺结构)
    void visitForStmtNode(const std::shared_ptr<TreeNode>& node);

    Operand* visitExp(const std::shared_ptr<TreeNode>& node);
    Operand* visitAddExp(const std::shared_ptr<TreeNode>& node);
    Operand* visitMulExp(const std::shared_ptr<TreeNode>& node);
    Operand* visitUnaryExp(const std::shared_ptr<TreeNode>& node);
    Operand* visitPrimaryExp(const std::shared_ptr<TreeNode>& node);

    // isAddress: 如果为 true，返回地址的临时变量（用于数组赋值左侧）；
    //            如果为 false，返回值的变量/临时变量（用于表达式右侧）
    Operand* visitLVal(const std::shared_ptr<TreeNode>& node, bool isAddress = false);

    // 短路求值
    void visitCond(const std::shared_ptr<TreeNode>& node, Operand* trueLabel, Operand* falseLabel);
    void visitLOrExp(const std::shared_ptr<TreeNode>& node, Operand* trueLabel, Operand* falseLabel);
    void visitLAndExp(const std::shared_ptr<TreeNode>& node, Operand* trueLabel, Operand* falseLabel);
    Operand* visitRelExp(const std::shared_ptr<TreeNode>& node);
    Operand* visitEqExp(const std::shared_ptr<TreeNode>& node);
    // Break/Continue 栈
    std::stack<Operand*> breakStack;
    std::stack<Operand*> continueStack;

    std::string extractIdent(const std::shared_ptr<TreeNode>& node);
};

#endif // COMPILER_IRGENERATOR_H