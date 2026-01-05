#ifndef COMPILER_TOKEN_H
#define COMPILER_TOKEN_H

#include <string>

struct Token {
    std::string code;   // 类别码，如 IDENFR、INTCON 等
    std::string lexeme; // 单词原始字符串
    int line;           // 出现的行号
};

struct LexError {
    int line;
    std::string code;   // 错误类别（a）
};

#endif // COMPILER_TOKEN_H
