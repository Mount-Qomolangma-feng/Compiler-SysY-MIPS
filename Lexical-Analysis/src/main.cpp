#include "Lexer.h"
#include <fstream>
#include <sstream>
#include <iostream>
//#include <windows.h>
//SetConsoleOutputCP(65001);

int main() {
    std::ifstream fin("testfile.txt");
    if (!fin.is_open()) {
        std::cerr << "无法打开 testfile.txt\n";
        return 1;
    }

    std::ostringstream oss;
    oss << fin.rdbuf();
    fin.close();

    Lexer lexer(oss.str());
    lexer.analyze();
    lexer.writeOutput("lexer.txt", "error.txt");

    std::cout << (lexer.hasError() ? "发现词法错误，已输出至 error.txt\n"
                                   : "词法分析完成，结果写入 lexer.txt\n");
    return 0;
}
