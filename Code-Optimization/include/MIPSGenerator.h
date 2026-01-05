#ifndef COMPILER_MIPSGENERATOR_H
#define COMPILER_MIPSGENERATOR_H

#include "IRGenerator.h"
#include "MIPS.h"
#include <fstream>
#include <list>
#include <map>
#include <set>

class MIPSGenerator {
public:
    MIPSGenerator(IRGenerator& irGen, SymbolTable& symTab);

    // 主生成函数
    void generate(const std::string& filename);

private:
    IRGenerator& irGenerator;
    SymbolTable& symbolTable;
    std::ofstream fout;

    // 当前正在处理的函数上下文
    /*CodeGenFunctionInfo* currentFuncInfo = nullptr;
    std::string currentFuncName;
    std::string lastFuncName;*/

    // === 【新增】窥孔优化相关 ===
    std::list<std::string> asmBuffer; // 指令缓冲区
    void peepholeOptimize();          // 优化核心逻辑
    void flushAsmBuffer();            // 将缓冲区写入文件

    // === 新增：函数上下文栈管理 ===
    std::stack<std::string> funcNameStack;          // 函数名栈
    std::stack<CodeGenFunctionInfo*> funcInfoStack; // 函数信息栈

    // 获取当前函数名（栈顶）
    std::string getCurrentFuncName() const {
        return funcNameStack.empty() ? "" : funcNameStack.top();
    }

    // 获取当前函数信息（栈顶）
    CodeGenFunctionInfo* getCurrentFuncInfo() const {
        return funcInfoStack.empty() ? nullptr : funcInfoStack.top();
    }

    // 函数入栈
    void pushFuncContext(const std::string& funcName, CodeGenFunctionInfo* funcInfo) {
        funcNameStack.push(funcName);
        funcInfoStack.push(funcInfo);
    }

    // 函数出栈
    void popFuncContext() {
        if (!funcNameStack.empty()) funcNameStack.pop();
        if (!funcInfoStack.empty()) funcInfoStack.pop();
    }

    std::string currentLabel;

    // === 新增：参数传递状态维护 ===
    int paramCounter = 0;      // 当前是第几个参数
    int paramsStackSize = 0;   // 参数压栈造成的栈空间增长量

    // === 寄存器管理器 (实现 FIFO 策略) ===
    class RegisterManager {
    public:
        RegisterManager(MIPSGenerator& gen) : generator(gen) {
            // 初始化可用寄存器池 ($t0 - $t7)
            // $t8, $t9 保留给立即数和地址计算，不参与分配
            for (int i = static_cast<int>(MipsReg::T0); i <= static_cast<int>(MipsReg::T7); ++i) {
                freeRegs.push_back(static_cast<MipsReg>(i));
            }
        }

        // 获取包含该操作数数值的寄存器
        // 如果已在寄存器中，直接返回
        // 如果不在，分配一个新寄存器并加载
        // 如果寄存器满了，使用 FIFO 溢出旧变量
        MipsReg getReg(Operand* op, bool isLoad = true);

        // 为结果分配一个寄存器（不需要加载旧值，因为会被覆盖）
        MipsReg allocateReg(Operand* result);

        // 将指定寄存器写回内存（Spill）
        void spillReg(MipsReg reg);

        // 强制写回所有寄存器 (用于函数调用前、跳转前、基本块结束)
        void spillAll();

        // 【新增】将参数从参数寄存器(srcReg)移动到分配的通用寄存器，并建立映射
        void mapParamToReg(const std::string& varName, MipsReg srcReg);

        // 【新增】清空映射但不生成指令 (用于 Label 处重置状态)
        void clearMap();

        // 标记寄存器空闲
        void freeReg(MipsReg reg);

        // 【新增】获取存放变量地址的寄存器
        // 对于数组，这将计算基地址并缓存；对于指针变量，直接返回其值
        MipsReg getAddrReg(Operand* op);

    private:
        MIPSGenerator& generator;

        // 寄存器池
        std::list<MipsReg> freeRegs; // 空闲寄存器队列
        std::list<MipsReg> busyRegs; // 已用寄存器队列 (用于 FIFO 溢出)

        // 映射关系
        std::map<std::string, MipsReg> varToReg; // 变量名 -> 寄存器
        std::map<MipsReg, std::string> regToVar; // 寄存器 -> 变量名

        // 【新增】缓存变量名对应的符号表项指针
        std::map<std::string, SymbolEntry*> varToSymbol;

        // 【新增】脏位标记：记录寄存器是否被修改过
        std::set<MipsReg> dirtyRegs;

        // 辅助：从栈或全局区加载变量到寄存器
        void loadValue(MipsReg reg, Operand* op);

        // 【新增】将指定寄存器移动到 busyRegs 队列的末尾（标记为最近使用）
        void makeMostRecentlyUsed(MipsReg reg);
    };

    RegisterManager regAlloc;

    // === 生成辅助函数 ===
    void emit(const std::string& asmCmd);
    void emitLabel(const std::string& label);

    // 生成数据段 (.data)
    void generateDataSegment();

    // 生成代码段 (.text)
    void generateTextSegment();

    // 生成 IO 辅助函数 (getint, printint)
    void generateSyscallHelpers();

    // 指令处理
    void visitFuncEntry(IRInstruction* instr);
    void visitFuncExit(IRInstruction* instr);
    void visitBinaryOp(IRInstruction* instr, const std::string& mipsOp);
    void visitUnaryOp(IRInstruction* instr); // NEG, NOT
    void visitLoadStore(IRInstruction* instr);
    void visitBranch(IRInstruction* instr); // BEQZ, JUMP
    void visitParam(IRInstruction* instr); // 新增声明
    void visitCall(IRInstruction* instr);
    void visitRet(IRInstruction* instr);
    void visitIO(IRInstruction* instr);

    // 内存地址计算辅助
    // 自动判断是全局变量还是局部变量，生成对应的地址指令
    // 如果是局部变量，返回 offset($fp) 格式字符串
    // 如果是全局变量，利用 $t9 计算地址，返回 0($t9)
    // 修改后
    std::string getAddress(Operand* op, MipsReg tempReg = MipsReg::T9);

    // 【新增】标签转换函数：为用户符号添加前缀，防止与汇编关键字(如 add, div)冲突
    // "main" -> "main"
    // "add"  -> "_add"
    std::string getMipsLabel(const std::string& name);
};

#endif // COMPILER_MIPSGENERATOR_H