#ifndef COMPILER_IR_H
#define COMPILER_IR_H

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include "SymbolTable.h"

// 中间代码操作符
enum class IROp {
    // 算术
    ADD, SUB, MUL, DIV, MOD, NEG,
    // 逻辑/比较
    NOT, GT, GE, LT, LE, EQ, NEQ,
    // 内存与赋值
    ASSIGN,     // x = y
    LOAD,       // x = *y (用于数组读取: x = arr[i])
    STORE,      // *x = y (用于数组赋值: arr[i] = y)
    GET_ADDR,   // x = &arr + offset (计算数组元素地址)
    // 跳转
    LABEL, JUMP, BEQZ,
    // 函数
    PARAM, CALL, RET,
    FUNC_ENTRY, FUNC_EXIT,
    // IO
    GETINT, PRINTINT, PRINTSTR
};

// 操作数类型
enum class OperandType {
    VAR,    // 源代码中的变量 (指向 SymbolEntry)
    TEMP,   // 临时变量 (t0, t1...)
    IMM,    // 立即数
    LABEL   // 标号
};

// 操作数
struct Operand {
    OperandType type;
    std::string name;       // 临时变量名、标号名
    int value;              // 立即数值
    SymbolEntry* symbol;    // 指向符号表项

    Operand(int v) : type(OperandType::IMM), value(v), symbol(nullptr) {}
    Operand(std::string n, OperandType t) : type(t), name(n), value(0), symbol(nullptr) {}
    Operand(SymbolEntry* s) : type(OperandType::VAR), name(s->name), value(0), symbol(s) {}

    std::string toString() const {
        if (type == OperandType::IMM) return "#" + std::to_string(value);
        if (type == OperandType::VAR) return symbol->name; // + "_" + std::to_string(symbol->line); // 可加行号区分重名
        return name;
    }
};

// 四元式指令
struct IRInstruction {
    IROp op;
    Operand* result;
    Operand* arg1;
    Operand* arg2;

    IRInstruction(IROp o, Operand* r = nullptr, Operand* a1 = nullptr, Operand* a2 = nullptr)
            : op(o), result(r), arg1(a1), arg2(a2) {}

    std::string getOpString() const {
        switch (op) {
            case IROp::ADD: return "ADD"; case IROp::SUB: return "SUB";
            case IROp::MUL: return "MUL"; case IROp::DIV: return "DIV"; case IROp::MOD: return "MOD";
            case IROp::NEG: return "NEG"; case IROp::NOT: return "NOT";
            case IROp::GT: return "GT"; case IROp::GE: return "GE";
            case IROp::LT: return "LT"; case IROp::LE: return "LE";
            case IROp::EQ: return "EQ"; case IROp::NEQ: return "NEQ";
            case IROp::ASSIGN: return "ASSIGN";
            case IROp::LOAD: return "LOAD";
            case IROp::STORE: return "STORE";
            case IROp::GET_ADDR: return "GET_ADDR";
            case IROp::LABEL: return "LABEL";
            case IROp::JUMP: return "JUMP";
            case IROp::BEQZ: return "BEQZ";
            case IROp::PARAM: return "PARAM";
            case IROp::CALL: return "CALL";
            case IROp::RET: return "RET";
            case IROp::GETINT: return "GETINT";
            case IROp::PRINTINT: return "PRINTINT";
            case IROp::PRINTSTR: return "PRINTSTR";
            case IROp::FUNC_ENTRY: return "FUNC_ENTRY";
            case IROp::FUNC_EXIT: return "FUNC_EXIT";
            default: return "UNKNOWN";
        }
    }

    std::string toString() const {
        if (op == IROp::LABEL) return result->toString() + ":";
        std::stringstream ss;
        ss << getOpString() << " "
           << (result ? result->toString() : "-") << ", "
           << (arg1 ? arg1->toString() : "-") << ", "
           << (arg2 ? arg2->toString() : "-");
        return ss.str();
    }
};

#endif // COMPILER_IR_H