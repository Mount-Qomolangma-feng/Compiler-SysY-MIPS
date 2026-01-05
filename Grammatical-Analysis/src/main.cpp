#include "Lexer.h"
#include "Parser.h"
#include <fstream>
#include <sstream>
#include <iostream>

int main() {
    // 1. 文件读取阶段
    std::ifstream fin("testfile.txt");
    if (!fin.is_open()) {
        std::cerr << "无法打开 testfile.txt\n";
        return 1;
    }

    std::ostringstream oss;
    oss << fin.rdbuf();
    fin.close();

    printf("文件读取已经完成\n");

    // 2. 词法分析阶段
    Lexer lexer(oss.str());
    lexer.analyze();

    printf("词法分析已经完成\n");

    // 新增：输出词法分析结果到token.txt
    //lexer.writeTokens("token.txt");


    // 3. 语法分析阶段
    Parser parser(lexer.getTokens(), lexer.getLexErrors());
    printf("3\n");
    parser.parse();

    printf("语法分析已经完成\n");

    // 4. 统一结果输出阶段
    parser.writeOutput("parser.txt", "error.txt");

    printf("结果输出已经完成\n");

    // 5. 控制台反馈
    if (parser.hasError()) {
        std::cout << "发现错误，已输出至 error.txt\n";
    } else {
        std::cout << "语法分析完成，结果写入 parser.txt\n";
    }

    return 0;
}