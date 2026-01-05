#ifndef COMPILER_LEXER_H
#define COMPILER_LEXER_H

#include <string>
#include <vector>
#include <unordered_map>
#include "Token.h"

class Lexer {
public:
    explicit Lexer(const std::string &input);
    void analyze();
    bool hasError() const;
    void writeOutput(const std::string &successFile, const std::string &errorFile) const;

    // 新增：输出词法分析结果到文件
    void writeTokens(const std::string& filename) const;

    //为语法分析提供的接口
    //已经内联实现，所以无需再额外编写代码
    const std::vector<Token>& getTokens() const { return tokens; }
    const std::vector<LexError>& getLexErrors() const { return errors; }

private:
    std::string content;
    size_t pos;
    int line;
    std::vector<Token> tokens;
    std::vector<LexError> errors;

    // 内部函数
    char peek(size_t offset = 0) const;
    void skipWhitespace();
    void handleComment();
    void handleString();
    void handleIdentifier();
    void handleNumber();
    void handleOperator();
    void handleSingleChar();
    void pushToken(const std::string &code, const std::string &lex);
    void pushError(int errLine, const std::string &errCode);

    // 词法表
    std::unordered_map<std::string, std::string> keywords;
    std::unordered_map<char, std::string> singleSym;
    std::unordered_map<std::string, std::string> multiSym;
};

#endif // COMPILER_LEXER_H
