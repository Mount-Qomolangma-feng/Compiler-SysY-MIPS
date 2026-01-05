#ifndef COMPILER_PARSER_H
#define COMPILER_PARSER_H

#include <vector>
#include <memory>
#include <iostream>
#include <fstream>
#include "Token.h"
#include "TreeNode.h"

class Parser {
public:
    // 构造函数，接收词法分析的结果
    Parser(const std::vector<Token>& tokens, const std::vector<LexError>& lexErrors);

    // 开始语法分析的主函数，构建完整的语法树
    void parse();

    // 检查是否存在错误（词法错误或语法错误）
    bool hasError() const;

    // 输出分析结果到文件
    void writeOutput(const std::string& successFile, const std::string& errorFile);

    // 获取语法树根节点（用于后续实验）
    std::shared_ptr<TreeNode> getRoot() const { return root; }
    const std::vector<SyntaxError>& getSyntaxErrors() const { return syntaxErrors; }

private:
    std::vector<Token> tokens;      // 词法分析生成的token序列
    std::vector<LexError> lexErrors;        // 词法分析阶段的错误列表
    std::vector<SyntaxError> syntaxErrors;      // 语法分析阶段的错误列表
    std::shared_ptr<TreeNode> root;     // 语法树的根节点

    size_t currentTokenIndex;       // 当前正在处理的token索引位置
    bool outputEnabled;     // 是否启用输出（用于调试和结果输出）
    std::ostream* outputStream;     // 输出流指针

    // 在Parser类中添加一个成员变量来记录前一个token的行号
// 在Parser.h的private部分添加：
    int previousTokenLine;

    int currentFunctionCallLine; // 记录当前函数调用的行号

    // 辅助函数
    const Token& peek() const;      // 查看当前token（不消费）
    const Token& advance();      // 消费当前token并移动到下一个
    bool match(const std::string& expectedCode);        // 尝试匹配指定类型的token
    bool check(const std::string& expectedCode) const;      // 检查当前token类型
    void error(int line, const std::string& code,const std::vector<std::string>& syncTokens);      // 记录语法错误
    void error2(int line, const std::string& code);      // 记录语法错误,但是不调用syncto

    bool isStartOfExp() const;


    // 输出控制
    void enableOutput(std::ostream& stream);        // 启用输出
    void disableOutput();       // 禁用输出
    void outputToken(const Token& token);       // 输出token信息
    void outputNonTerminal(const std::string& name);        // 输出非终结符信息

    // 递归下降解析函数
    std::shared_ptr<TreeNode> parseCompUnit();      // CompUnit → {Decl} {FuncDef} MainFuncDef
    std::shared_ptr<TreeNode> parseDecl();      // Decl → ConstDecl | VarDecl
    std::shared_ptr<TreeNode> parseConstDecl();     // ConstDecl → 'const' BType ConstDef { ',' ConstDef } ';'
    std::shared_ptr<TreeNode> parseVarDecl();       // VarDecl → ['static'] BType VarDef { ',' VarDef } ';'
    std::shared_ptr<TreeNode> parseBType();     // BType → 'int'
    std::shared_ptr<TreeNode> parseConstDef();      // ConstDef → Ident ['[' ConstExp ']'] '=' ConstInitVal
    std::shared_ptr<TreeNode> parseConstInitVal();      // ConstInitVal → ConstExp | '{' [ConstExp {',' ConstExp}] '}'
    std::shared_ptr<TreeNode> parseVarDef();        // VarDef → Ident ['[' ConstExp ']'] ['=' InitVal]
    std::shared_ptr<TreeNode> parseInitVal();       // InitVal → Exp | '{' [Exp {',' Exp}] '}'
    std::shared_ptr<TreeNode> parseFuncDef();       // FuncDef → FuncType Ident '(' [FuncFParams] ')' Block
    std::shared_ptr<TreeNode> parseMainFuncDef();       // MainFuncDef → 'int' 'main' '(' ')' Block
    std::shared_ptr<TreeNode> parseFuncType();      // FuncType → 'void' | 'int'
    std::shared_ptr<TreeNode> parseFuncFParams();       // FuncFParams → FuncFParam {',' FuncFParam}
    std::shared_ptr<TreeNode> parseFuncFParam();        // FuncFParam → BType Ident ['[' ']']
    std::shared_ptr<TreeNode> parseBlock();     // Block → '{' {BlockItem} '}'
    std::shared_ptr<TreeNode> parseBlockItem();     // BlockItem → Decl | Stmt
    std::shared_ptr<TreeNode> parseStmt();      // Stmt → 各种语句类型（赋值、if、for、return等）
    std::shared_ptr<TreeNode> parseForStmt();       // ForStmt → LVal '=' Exp {',' LVal '=' Exp}
    std::shared_ptr<TreeNode> parseExp();       // Exp → AddExp
    std::shared_ptr<TreeNode> parseCond();      // Cond → LOrExp
    std::shared_ptr<TreeNode> parseLVal();      // LVal → Ident ['[' Exp ']']
    std::shared_ptr<TreeNode> parsePrimaryExp();        // PrimaryExp → '(' Exp ')' | LVal | Number
    std::shared_ptr<TreeNode> parseNumber();        // Number → IntConst
    std::shared_ptr<TreeNode> parseUnaryExp();      // UnaryExp → PrimaryExp | Ident '(' [FuncRParams] ')' | UnaryOp UnaryExp
    //下面的都是运算符表达式
    std::shared_ptr<TreeNode> parseUnaryOp();
    std::shared_ptr<TreeNode> parseFuncRParams();
    std::shared_ptr<TreeNode> parseMulExp();
    std::shared_ptr<TreeNode> parseAddExp();
    std::shared_ptr<TreeNode> parseRelExp();
    std::shared_ptr<TreeNode> parseEqExp();
    std::shared_ptr<TreeNode> parseLAndExp();
    std::shared_ptr<TreeNode> parseLOrExp();
    std::shared_ptr<TreeNode> parseConstExp();

    // 错误恢复：跳过token直到遇到同步token集合中的某个token
    void syncTo(const std::vector<std::string>& syncTokens);
};

#endif // COMPILER_PARSER_H