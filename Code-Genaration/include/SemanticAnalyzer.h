#ifndef COMPILER_SEMANTICANALYZER_H
#define COMPILER_SEMANTICANALYZER_H

#include "TreeNode.h"
#include "SymbolTable.h"
#include "Token.h"
#include <vector>
#include <string>
#include <memory>
#include <optional>

// 语义错误
struct SemanticError {
    int line;
    std::string code;   // 错误类别码 (b, c, d, e, f, g, h, l, m)
};

// 表达式求值结果
struct EvalResult {
    int value;//表达式的值
    bool isConstant;//是否为常量表达式
    SymbolType type;//表达式类型

    //初始化函数
    EvalResult(int v = 0, bool ic = false, SymbolType t = SymbolType::Int)
            : value(v), isConstant(ic), type(t) {}
};

class SemanticAnalyzer {
private:
    SymbolTable symbolTable;//符号表，管理常量，变量，函数的作用域
    std::vector<SemanticError> semanticErrors;//存储所有语义错误

    // 当前函数信息（函数分析状态）
    SymbolType currentFunctionType;//当前正在分析的函数返回类型
    std::string currentFunctionName;//当前函数名
    bool hasReturnStatement;//标记函数中是否有return语句
    int functionStartLine;//函数起始行号

    // 循环信息
    int loopDepth; // 当前循环嵌套深度，用于检查break/continue语句（错误m）

    int currentStackOffset;

    // 参数类型信息结构体（用于函数参数类型检查）
    struct ParamType {
        SymbolType baseType;    // 基础类型
        bool isArray;           // 是否是数组
        bool isConst;           // 是否是常量
        bool isArrayElement;    // 是否是数组元素访问（如 arr[i]）

        ParamType(SymbolType t = SymbolType::Int, bool arr = false, bool c = false, bool elem = false)
                : baseType(t), isArray(arr), isConst(c), isArrayElement(elem) {}
    };

    // 参数类型分析函数
    ParamType analyzeParamType(const std::shared_ptr<TreeNode>& paramNode);

    // 参数类型匹配检查函数
    bool isParamTypeMatch(const ParamInfo& expected, const ParamType& actual);

    // 从LVal节点提取标识符的辅助函数
    std::string extractIdentFromLVal(const std::shared_ptr<TreeNode>& lvalNode);

    // 检查是否是数组元素访问
    bool isArrayElementAccess(const std::shared_ptr<TreeNode>& paramNode);

    // 系统函数检查相关函数
    bool isSystemFunction(const std::string& funcName);
    bool checkSystemFunctionCall(const std::string& funcName,
                                 const std::vector<std::shared_ptr<TreeNode>>& actualParams,
                                 int line);

    // 调试相关
    bool debugEnabled;
    int visitDepth;
    void debugPrint(const std::string& functionName, const std::shared_ptr<TreeNode>& node) const;
    void debugPrint(const std::string& functionName, const std::string& additionalInfo = "") const;

    int getBlockEndLine(const std::shared_ptr<TreeNode>& blockNode);

    // 辅助函数
    //将SymbolType里那一堆翻译成程序语言
    std::string getTypeString(SymbolType type) const;
    //扔进去一个节点，判断是什么类型
    SymbolType getVarDefType(const std::shared_ptr<TreeNode>& node, bool isConst, bool isStatic) const;

    // 表达式求值和类型检查
    /*
     evaluate系列函数 - 常量表达式求值
    作用概述
    常量表达式编译时求值系统，在编译期间计算表达式的值，主要用于：
    数组大小计算
    常量初始化值验证
    全局变量初始化
    编译时常量折叠优化
     */

    // [新增] 递归收集初始化节点中的所有表达式节点
    void collectInitExprNodes(const std::shared_ptr<TreeNode>& node, std::vector<std::shared_ptr<TreeNode>>& outExprNodes);

    // [新增] 填充数组初始化值
    void fillArrayInitValues(SymbolEntry& entry, const std::shared_ptr<TreeNode>& initValNode, bool requireConst);

    //表达式分析的入口点，直接委托给 evaluateAddExp
    //对应文法：Exp → AddExp
    EvalResult evaluateExpression(const std::shared_ptr<TreeNode>& node);
    //作用：专门用于常量表达式的求值，会进行额外的常量性检查
    //对应文法：ConstExp → AddExp
    EvalResult evaluateConstExp(const std::shared_ptr<TreeNode>& node);
    //处理加减法运算，考虑运算符优先级
    //对应文法：AddExp → MulExp | AddExp ('+' | '-') MulExp
    EvalResult evaluateAddExp(const std::shared_ptr<TreeNode>& node);
    //作用：处理乘除模运算，优先级高于加减法
    //对应文法：MulExp → UnaryExp | MulExp ('*' | '/' | '%') UnaryExp
    EvalResult evaluateMulExp(const std::shared_ptr<TreeNode>& node);
    //作用：处理一元运算符和函数调用
    //对应文法：UnaryExp → PrimaryExp | Ident '(' [FuncRParams] ')' | UnaryOp UnaryExp
    EvalResult evaluateUnaryExp(const std::shared_ptr<TreeNode>& node);
    //作用：处理基本表达式元素
    //对应文法：PrimaryExp → '(' Exp ')' | LVal | Number
    EvalResult evaluatePrimaryExp(const std::shared_ptr<TreeNode>& node);
    //作用：分析变量访问或数组元素访问
    //对应文法：LVal → Ident ['[' Exp ']']
    EvalResult evaluateLVal(const std::shared_ptr<TreeNode>& node);
    //作用：处理数字字面量
    //对应文法：Number → IntConst
    EvalResult evaluateNumber(const std::shared_ptr<TreeNode>& node);

    //添加两个辅助函数


    // 类型检查
    //这一系列函数是语义分析器的核心检查器，专门负责检测各种语义错误。
    //检查函数调用的合法性
    bool checkFunctionCall(const std::string& funcName,
                           const std::vector<std::shared_ptr<TreeNode>>& actualParams,
                           int line);
    //检查return语句的合法性
    void checkReturnStatement(const std::shared_ptr<TreeNode>& expNode, int line);
    //检查赋值语句左边的合法性
    bool checkLValAssignment(const std::shared_ptr<TreeNode>& lvalNode, int line);
    //检查printf语句的格式
    void checkPrintfStatement(const std::shared_ptr<TreeNode>& node);

    // 数组大小计算
    int getArraySizeFromConstDef(const std::shared_ptr<TreeNode>& node);

    // 错误处理
    void addError(int line, const std::string& code);
    bool hasErrorOnLine(int line) const;

    // 在 SemanticAnalyzer.h 的 private: 部分添加

    // ... (在其他私有函数，例如 addError 附近) ...

    /**
     * @brief 控制流分析 (CFA) 辅助函数。
     * * 检查给定的AST节点（语句或块）是否存在一条执行路径
     * 可以“穿透”该节点（即，不经过 return 语句就执行完毕）。
     *
     * @param node 要分析的语句 (STMT) 或代码块 (BLOCK) 节点。
     * @return true 如果存在一条路径可以穿透该节点，false 如果所有路径都以 return 终止。
     */
    bool canFallThrough(const std::shared_ptr<TreeNode>& node);

public:
    SemanticAnalyzer();//创建并初始化语义分析器的初始状态

    // 语义分析主函数
    void analyze(const std::shared_ptr<TreeNode>& root);//启动整个语义分析过程，是语义分析的入口点

    // 错误检查
    bool hasError() const;
    const std::vector<SemanticError>& getSemanticErrors() const;

    // 输出符号表
    void writeSymbolTable(const std::string& filename) const;

    // 遍历语法树的函数
    //对应文法：CompUnit → {Decl} {FuncDef} MainFuncDef
    void visitCompUnit(const std::shared_ptr<TreeNode>& node);
    //对应文法：Decl → ConstDecl | VarDecl
    void visitDecl(const std::shared_ptr<TreeNode>& node);
    //对应文法：ConstDecl → 'const' BType ConstDef { ',' ConstDef } ';'
    void visitConstDecl(const std::shared_ptr<TreeNode>& node);
    //对应文法：VarDecl → ['static'] BType VarDef { ',' VarDef } ';'
    void visitVarDecl(const std::shared_ptr<TreeNode>& node);
    //对应文法：ConstDef → Ident ['[' ConstExp ']'] '=' ConstInitVal
    void visitConstDef(const std::shared_ptr<TreeNode>& node, bool isGlobal);
    void visitVarDef(const std::shared_ptr<TreeNode>& node, bool isGlobal, bool isStatic);
    //对应文法：FuncDef → FuncType Ident '(' [FuncFParams] ')' Block
    void visitFuncDef(const std::shared_ptr<TreeNode>& node);
    void visitMainFuncDef(const std::shared_ptr<TreeNode>& node);
    //对应文法：FuncFParams → FuncFParam { ',' FuncFParam }
    void visitFuncFParams(const std::shared_ptr<TreeNode>& node);
    void visitFuncFParam(const std::shared_ptr<TreeNode>& node);
    //对应文法：Block → '{' { BlockItem } '}'
    void visitBlock(const std::shared_ptr<TreeNode>& node, bool isFunctionBody = false);
    //对应文法：多种语句类型
    void visitStmt(const std::shared_ptr<TreeNode>& node);
    // 在 SemanticAnalyzer.h 的 public 部分添加
    void visitForStmt(const std::shared_ptr<TreeNode>& node);
    //对应文法：LVal → Ident ['[' Exp ']']
    void visitLVal(const std::shared_ptr<TreeNode>& node, bool isAssignment = false);
    //这些函数调用前面分析的 evaluate 系列函数：
    void visitExp(const std::shared_ptr<TreeNode>& node);
    void visitAddExp(const std::shared_ptr<TreeNode>& node);
    void visitMulExp(const std::shared_ptr<TreeNode>& node);
    void visitUnaryExp(const std::shared_ptr<TreeNode>& node);
    void visitCond(const std::shared_ptr<TreeNode>& node);

    // 获取当前作用域信息
    int getCurrentScopeId() const { return symbolTable.getCurrentScopeId(); }
    bool isGlobalScope() const { return symbolTable.getCurrentScopeId() == 1; }

    // 启用/禁用调试
    void enableDebug(bool enable = true) { debugEnabled = enable; }

    // 输出格式化的符号表到文件
    void writeFormattedSymbolTable(const std::string& filename) const {
        symbolTable.writeFormattedSymbolTable(filename);
    }

    // 【新增】获取符号表的引用（供IRGenerator使用）
    SymbolTable& getSymbolTable() {
        return symbolTable;
    }
};

#endif // COMPILER_SEMANTICANALYZER_H