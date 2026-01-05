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

//为语法分析准备的新结构体
// 语法错误 - 在语法分析阶段产生
struct SyntaxError {
    int line;
    std::string code;   // 错误类别（i, j, k）
};



#endif // COMPILER_TOKEN_H
