#include "Lexer.h"
#include "Parser.h"
#include "SemanticAnalyzer.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

//#include <windows.h>

int main() {
    // 只设置控制台编码，不使用locale
    //SetConsoleOutputCP(CP_UTF8);
    //SetConsoleCP(CP_UTF8);

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

    // 3. 语法分析阶段
    Parser parser(lexer.getTokens(), lexer.getLexErrors());
    parser.parse();

    // [新] 将语法树输出到 "tree.txt"
    parser.printAst("tree.txt");

    printf("语法分析已经完成\n");

    // 4. 语义分析阶段
    SemanticAnalyzer semanticAnalyzer;
    semanticAnalyzer.enableDebug(false);
    semanticAnalyzer.analyze(parser.getRoot());

    // 输出格式化的符号表
    semanticAnalyzer.writeFormattedSymbolTable("table.txt");

    printf("语义分析已经完成\n");

    // 5. 统一结果输出阶段
    if (parser.hasError() || semanticAnalyzer.hasError()) {
        // 输出错误到error.txt
        std::ofstream fout("error.txt");

        // 合并所有错误
        std::vector<std::pair<int, std::string>> allErrors;

        // 添加词法错误
        for (const auto& error : lexer.getLexErrors()) {
            allErrors.push_back({error.line, error.code});
        }

        // 添加语法错误
        for (const auto& error : parser.getSyntaxErrors()) {
            allErrors.push_back({error.line, error.code});
        }

        // 添加语义错误
        for (const auto& error : semanticAnalyzer.getSemanticErrors()) {
            allErrors.push_back({error.line, error.code});
        }

        // 按行号排序并去重（一行最多一个错误）
        std::sort(allErrors.begin(), allErrors.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        // 去重：同一行只保留第一个错误
        std::vector<std::pair<int, std::string>> uniqueErrors;
        int lastLine = -1;
        for (const auto& error : allErrors) {
            if (error.first != lastLine) {
                uniqueErrors.push_back(error);
                lastLine = error.first;
            }
        }

        for (const auto& error : uniqueErrors) {
            fout << error.first << " " << error.second << "\n";
        }

        fout.close();
        std::cout << "发现错误，已输出至 error.txt\n";
    } else {
        // 输出符号表到symbol.txt
        semanticAnalyzer.writeSymbolTable("symbol.txt");
        std::cout << "语义分析完成，符号表写入 symbol.txt\n";
    }

    return 0;
}