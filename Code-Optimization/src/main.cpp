#include "Lexer.h"
#include "Parser.h"
#include "SemanticAnalyzer.h"
#include "IRGenerator.h"
#include "MIPSGenerator.h"
#include "Optimizer.h"  // 【新增 1】引入优化器头文件
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

//#include <windows.h>

// 定义是否开启优化的开关，方便作业要求的“优化前/后”切换
const bool ENABLE_OPTIMIZATION = true;

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

    // 6. 中间代码生成阶段 - 新增
    std::cout << "开始中间代码生成...\n";

    // 创建IRGenerator实例，传入符号表
    IRGenerator irGenerator(semanticAnalyzer.getSymbolTable());

    // 生成中间代码
    irGenerator.generate(parser.getRoot());

    // 输出中间代码到文件
    irGenerator.printIR("ir.txt");  // 输出到ir.txt文件

    // 【新增】输出 MIPS 生成用的符号表/栈帧布局信息
    // 这将生成一个名为 mips_stack_layout.txt 的文件，供你调试查看
    irGenerator.dumpMipsCodeGenTable("mips_stack_layout.txt");

    std::cout << "中间代码生成完成，已输出至 ir.txt\n";
    std::cout << "MIPS 栈帧布局信息已输出至 mips_stack_layout.txt\n";

    // ==========================================
    // 【新增】 IR 优化阶段
    // ==========================================
    if (ENABLE_OPTIMIZATION) {
        std::cout << ">>> 正在执行 IR 优化...\n";

        // 1. 获取原始指令
        std::vector<IRInstruction*> rawIR = irGenerator.getInstructions();

        // 2. 创建并运行优化器
        Optimizer optimizer(rawIR);
        optimizer.execute();

        // 3. 获取优化后的指令
        std::vector<IRInstruction*> optimizedIR = optimizer.getOptimizedIR();

        // 4. 【关键】将优化后的指令写回 IRGenerator
        // 这样后续的 MIPSGenerator 就会基于优化后的代码生成
        irGenerator.setInstructions(optimizedIR);

        // 5. 输出【优化后】的中间代码
        irGenerator.printIR("testfilei_opt_after.txt");

        std::cout << ">>> IR 优化完成，指令数从 " << rawIR.size()
                  << " 减少到 " << optimizedIR.size() << "\n";
    }

    // ==========================================
    // 7. MIPS 代码生成阶段 (【新增调用部分】)
    // ==========================================
    std::cout << "开始生成 MIPS 汇编代码...\n";

    // 创建 MIPSGenerator 实例
    // 参数1: irGenerator (提供指令列表)
    // 参数2: semanticAnalyzer.getSymbolTable() (提供全局变量/静态变量信息)
    MIPSGenerator mipsGenerator(irGenerator, semanticAnalyzer.getSymbolTable());

    // 执行生成，指定输出文件名为 "mips.txt"
    mipsGenerator.generate("mips.txt");

    std::cout << "MIPS 汇编生成完成，已输出至 mips.txt\n";



    return 0;
}