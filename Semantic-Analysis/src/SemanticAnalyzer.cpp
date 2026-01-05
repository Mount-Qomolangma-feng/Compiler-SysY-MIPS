#include "SemanticAnalyzer.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <sstream>

//创建并初始化语义分析器的初始状态
SemanticAnalyzer::SemanticAnalyzer()
        : currentFunctionType(SymbolType::VoidFunc),
          currentFunctionName(""),
          hasReturnStatement(false),
          functionStartLine(0),
          loopDepth(0),
          debugEnabled(true),    // 默认启用调试
          visitDepth(0) {}       // 初始深度为0

//getTypeString 函数的作用是将内部的 SymbolType 枚举值转换为人类可读的字符串表示。这是一个辅助函数，主要用于符号表输出、错误报告和调试信息的生成。
std::string SemanticAnalyzer::getTypeString(SymbolType type) const {
    switch (type) {
        case SymbolType::ConstInt: return "ConstInt";
        case SymbolType::Int: return "Int";
        case SymbolType::VoidFunc: return "VoidFunc";
        case SymbolType::IntFunc: return "IntFunc";
        case SymbolType::ConstIntArray: return "ConstIntArray";
        case SymbolType::IntArray: return "IntArray";
        case SymbolType::StaticInt: return "StaticInt";
        case SymbolType::StaticIntArray: return "StaticIntArray";
        default: return "Unknown";
    }
}

SymbolType SemanticAnalyzer::getVarDefType(const std::shared_ptr<TreeNode>& node, bool isConst, bool isStatic) const {
    // 检查是否是数组
    bool isArray = false;
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::TERMINAL && child->value == "[") {
            isArray = true;
            break;
        }
    }

    if (isConst) {
        return isArray ? SymbolType::ConstIntArray : SymbolType::ConstInt;
    } else if (isStatic) {
        return isArray ? SymbolType::StaticIntArray : SymbolType::StaticInt;
    } else {
        return isArray ? SymbolType::IntArray : SymbolType::Int;
    }
}

/**
 * 从LVal节点提取标识符
 */
std::string SemanticAnalyzer::extractIdentFromLVal(const std::shared_ptr<TreeNode>& lvalNode) {
    if (!lvalNode || lvalNode->children.empty()) {
        return "";
    }

    // LVal → Ident ['[' Exp ']']
    // 第一个子节点应该是标识符
    for (const auto& child : lvalNode->children) {
        if (child->nodeType == NodeType::TERMINAL &&
            child->value != "[" && child->value != "]") {
            return child->value;
        }
    }
    return "";
}

/**
 * 检查是否是数组元素访问
 */
bool SemanticAnalyzer::isArrayElementAccess(const std::shared_ptr<TreeNode>& paramNode) {
    if (!paramNode) return false;

    // 如果是LVal节点，检查是否有数组下标
    if (paramNode->nodeType == NodeType::LVAL) {
        for (const auto& child : paramNode->children) {
            if (child->value == "[") {
                return true; // 有下标，说明是数组元素访问
            }
        }
    }

    // 如果是PrimaryExp或UnaryExp，递归检查
    if (paramNode->nodeType == NodeType::PRIMARY_EXP ||
        paramNode->nodeType == NodeType::UNARY_EXP) {
        for (const auto& child : paramNode->children) {
            if (isArrayElementAccess(child)) {
                return true;
            }
        }
    }

    return false;
}

/**
 * 分析参数表达式的类型
 */
SemanticAnalyzer::ParamType SemanticAnalyzer::analyzeParamType(const std::shared_ptr<TreeNode>& paramNode) {
    if (!paramNode) {
        return ParamType(SymbolType::Int, false, false, false);
    }

    ParamType result;

    switch (paramNode->nodeType) {
        case NodeType::LVAL: {
            // 左值表达式 - 可能是变量或数组元素
            std::string ident = extractIdentFromLVal(paramNode);
            SymbolEntry* symbol = symbolTable.findSymbol(ident);

            if (symbol) {
                result.baseType = symbol->type;
                result.isArray = symbol->isArray();
                result.isConst = symbol->isConstant();
                result.isArrayElement = isArrayElementAccess(paramNode);

                // 如果是数组元素访问，实际传递的是int类型，不是数组
                if (result.isArrayElement) {
                    result.isArray = false;
                }
            } else {
                // 未找到符号，默认为int变量
                result.baseType = SymbolType::Int;
                result.isArray = false;
                result.isConst = false;
                result.isArrayElement = false;
            }
            break;
        }

        case NodeType::PRIMARY_EXP: {
            // 基本表达式 - 可能是括号表达式、左值或数字
            if (!paramNode->children.empty()) {
                auto firstChild = paramNode->children[0];
                if (firstChild->nodeType == NodeType::LVAL) {
                    // 括号内的左值
                    return analyzeParamType(firstChild);
                } else if (firstChild->nodeType == NodeType::EXP) {
                    // 括号内的表达式
                    return analyzeParamType(firstChild);
                } else if (firstChild->nodeType == NodeType::NUMBER) {
                    // 数字字面量
                    result.baseType = SymbolType::Int;
                    result.isArray = false;
                    result.isConst = true; // 字面量是常量
                    result.isArrayElement = false;
                }
            }
            break;
        }

        case NodeType::UNARY_EXP: {
            // 一元表达式 - 可能是函数调用或其他
            if (paramNode->children.size() >= 2 &&
                paramNode->children[1]->value == "(") {
                // 函数调用返回值视为普通int
                result.baseType = SymbolType::Int;
                result.isArray = false;
                result.isConst = false; // 函数返回值不是常量
                result.isArrayElement = false;
            } else {
                // 其他一元表达式，递归分析
                if (!paramNode->children.empty()) {
                    auto lastChild = paramNode->children.back();
                    return analyzeParamType(lastChild);
                }
            }
            break;
        }

        case NodeType::EXP: {
            // 表达式 - 递归分析第一个子节点
            if (!paramNode->children.empty()) {
                return analyzeParamType(paramNode->children[0]);
            }
            break;
        }

        case NodeType::ADD_EXP:
        case NodeType::MUL_EXP: {
            // 算术表达式 - 递归分析子节点
            if (!paramNode->children.empty()) {
                // 对于二元运算，分析第一个操作数
                return analyzeParamType(paramNode->children[0]);
            }
            break;
        }

        case NodeType::NUMBER: {
            // 数字字面量
            result.baseType = SymbolType::Int;
            result.isArray = false;
            result.isConst = true;
            result.isArrayElement = false;
            break;
        }

        default: {
            // 默认情况，视为普通int
            result.baseType = SymbolType::Int;
            result.isArray = false;
            result.isConst = false;
            result.isArrayElement = false;
            break;
        }
    }

    return result;
}

/**
 * 检查参数类型是否匹配
 */
bool SemanticAnalyzer::isParamTypeMatch(const ParamInfo& expected, const ParamType& actual) {
    // 数组类型匹配规则
    if (expected.isArray) {
        // 期望数组：实际参数必须是数组名（不是数组元素）
        // 允许传递数组名，但不允许传递数组元素或其他类型
        return actual.isArray && !actual.isArrayElement;
    } else {
        // 期望普通变量：实际参数可以是：
        // - 普通变量
        // - 数组元素（如arr[i]）
        // - 常量值
        // - 表达式结果
        // - 函数返回值

        // 不允许传递数组名给普通变量参数
        if (actual.isArray && !actual.isArrayElement) {
            return false; // 传递数组名给普通变量参数
        }

        // 其他情况都允许
        return true;
    }
}

/**
 * 检查是否是系统保留函数
 */
bool SemanticAnalyzer::isSystemFunction(const std::string& funcName) {
    return funcName == "getint" || funcName == "printf";
}

/**
 * 检查系统函数调用的合法性
 */
bool SemanticAnalyzer::checkSystemFunctionCall(const std::string& funcName,
                                               const std::vector<std::shared_ptr<TreeNode>>& actualParams,
                                               int line) {
    if (funcName == "getint") {
        // getint() 应该没有参数
        if (!actualParams.empty()) {
            addError(line, "d"); // 参数个数不匹配
            return false;
        }
        return true;
    }
    else if (funcName == "printf") {
        // printf 应该至少有一个参数（格式字符串）
        if (actualParams.empty()) {
            addError(line, "d"); // 参数个数不匹配
            return false;
        }

        // 第一个参数必须是字符串常量
        // 这里可以添加更详细的检查，但根据SysY规范，printf的参数处理是特殊的
        // 我们主要确保不报"未定义函数"错误，具体格式检查在checkPrintfStatement中完成

        return true;
    }

    // 未知的系统函数（理论上不会发生，因为isSystemFunction已经过滤）
    return false;
}

// 在 SemanticAnalyzer.cpp 中添加调试函数实现
void SemanticAnalyzer::debugPrint(const std::string& functionName, const std::shared_ptr<TreeNode>& node) const {
    if (!debugEnabled) return;

    std::string indent(visitDepth * 2, ' ');
    std::cout << indent << "[" << functionName << "] ";
    std::cout << "scope=" << getCurrentScopeId();
    std::cout << ", line=" << (node ? node->line : 0);
    std::cout << ", children=" << (node ? node->children.size() : 0);

    // 如果是终结符，显示值
    if (node && node->nodeType == NodeType::TERMINAL && !node->value.empty()) {
        std::cout << ", value='" << node->value << "'";
    }

    std::cout << std::endl;
}

void SemanticAnalyzer::debugPrint(const std::string& functionName, const std::string& additionalInfo) const {
    if (!debugEnabled) return;

    std::string indent(visitDepth * 2, ' ');
    std::cout << indent << "[" << functionName << "] ";
    std::cout << "scope=" << getCurrentScopeId();
    if (!additionalInfo.empty()) {
        std::cout << ", " << additionalInfo;
    }
    std::cout << std::endl;
}

// 新增辅助函数：获取Block节点的结束行号
int SemanticAnalyzer::getBlockEndLine(const std::shared_ptr<TreeNode>& blockNode) {
    // Block → '{' { BlockItem } '}'
    // 结束行号应该是右花括号 '}' 所在的行号

    // 方法1: 如果有子节点，最后一个子节点可能是右花括号
    if (!blockNode->children.empty()) {
        auto lastChild = blockNode->children.back();
        if (lastChild->nodeType == NodeType::TERMINAL && lastChild->value == "}") {
            //printf("%d\n",lastChild->line);
            return lastChild->line;
        }
    }

    // 方法2: 如果找不到明确的右花括号，返回Block节点本身的行号
    // 在语法树构建时，Block节点的行号通常是右花括号所在行
    return blockNode->line;
}

EvalResult SemanticAnalyzer::evaluateExpression(const std::shared_ptr<TreeNode>& node) {
    if (!node) return EvalResult(0, false);

    switch (node->nodeType) {
        case NodeType::EXP:
            return evaluateExpression(node->children[0]); // Exp → AddExp
        case NodeType::ADD_EXP:
            return evaluateAddExp(node);
        case NodeType::MUL_EXP:
            return evaluateMulExp(node);
        case NodeType::UNARY_EXP:
            return evaluateUnaryExp(node);
        case NodeType::PRIMARY_EXP:
            return evaluatePrimaryExp(node);
        case NodeType::LVAL:
            return evaluateLVal(node);
        case NodeType::NUMBER:
            return evaluateNumber(node);
        case NodeType::CONST_EXP:
            return evaluateConstExp(node);
        default:
            return EvalResult(0, false);
    }
}


EvalResult SemanticAnalyzer::evaluateConstExp(const std::shared_ptr<TreeNode>& node) {
    // ConstExp → AddExp

    // 添加调试信息 - 函数开始
    /*std::cout << "[DEBUG evaluateConstExp] 开始, 节点类型: "
              << (node ? std::to_string(static_cast<int>(node->nodeType)) : "null")
              << ", 子节点数: " << (node ? node->children.size() : 0) << std::endl;*/

    if (!node->children.empty()) {
        //return evaluateAddExp(node->children[0]);
        //std::cout << "[DEBUG evaluateConstExp] 有子节点，调用 evaluateAddExp" << std::endl;
        EvalResult result = evaluateAddExp(node->children[0]);
        /*std::cout << "[DEBUG evaluateConstExp] evaluateAddExp 返回, isConstant: "
                  << result.isConstant << ", value: " << result.value << std::endl;*/
        return result;
    }
    //std::cout << "[DEBUG evaluateConstExp] 无子节点，返回默认值" << std::endl;

    return EvalResult(0, true);//第三个值使用默认值，没有语法错误
}

EvalResult SemanticAnalyzer::evaluateAddExp(const std::shared_ptr<TreeNode>& node) {
    // AddExp → MulExp | AddExp ('+' | '-') MulExp

    // 添加调试信息 - 函数开始
    /*std::cout << "[DEBUG evaluateAddExp] 开始, 节点类型: "
              << (node ? std::to_string(static_cast<int>(node->nodeType)) : "null")
              << ", 子节点数: " << (node ? node->children.size() : 0) << std::endl;*/

    if (node->children.size() == 1) {
        //std::cout << "[DEBUG evaluateAddExp] 单子节点情况，调用 evaluateMulExp" << std::endl;
        EvalResult result = evaluateMulExp(node->children[0]);
        /*std::cout << "[DEBUG evaluateAddExp] evaluateMulExp 返回, isConstant: "
                  << result.isConstant << ", value: " << result.value << std::endl;*/
        return result;
    }

    //std::cout << "[DEBUG evaluateAddExp] 多子节点情况，开始处理加减运算" << std::endl;
    //std::cout << "[DEBUG evaluateAddExp] 递归调用 evaluateAddExp (左操作数)" << std::endl;

    // 处理加减运算
    EvalResult left = evaluateAddExp(node->children[0]);

    /*std::cout << "[DEBUG evaluateAddExp] 左操作数结果: isConstant=" << left.isConstant
              << ", value=" << left.value << std::endl;
    std::cout << "[DEBUG evaluateAddExp] 调用 evaluateMulExp (右操作数)" << std::endl;*/


    EvalResult right = evaluateMulExp(node->children[2]);

    /*std::cout << "[DEBUG evaluateAddExp] 右操作数结果: isConstant=" << right.isConstant
              << ", value=" << right.value << std::endl;*/

    if (!left.isConstant || !right.isConstant) {

        //std::cout << "[DEBUG evaluateAddExp] 操作数不是常量，返回默认值" << std::endl;

        return EvalResult(0, false);
    }

    std::string op = node->children[1]->value;

    //std::cout << "[DEBUG evaluateAddExp] 运算符: '" << op << "'" << std::endl;

    if (op == "+") {
        int result = left.value + right.value;
        /*std::cout << "[DEBUG evaluateAddExp] 加法运算: " << left.value << " + "
                  << right.value << " = " << result << std::endl;*/
        return EvalResult(result, true);
    } else if (op == "-") {
        int result = left.value - right.value;
        /*std::cout << "[DEBUG evaluateAddExp] 减法运算: " << left.value << " - "
                  << right.value << " = " << result << std::endl;*/
        return EvalResult(result, true);
    }

    //std::cout << "[DEBUG evaluateAddExp] 未知运算符，返回默认值" << std::endl;

    return EvalResult(0, false);
}

EvalResult SemanticAnalyzer::evaluateMulExp(const std::shared_ptr<TreeNode>& node) {
    // 添加调试信息 - 函数开始
    /*std::cout << "[DEBUG evaluateMulExp] 开始, 节点类型: "
              << (node ? std::to_string(static_cast<int>(node->nodeType)) : "null")
              << ", 子节点数: " << (node ? node->children.size() : 0) << std::endl;*/

    // MulExp → UnaryExp | MulExp ('*' | '/' | '%') UnaryExp
    if (node->children.size() == 1) {
        //std::cout << "[DEBUG evaluateMulExp] 单子节点情况，调用 evaluateUnaryExp" << std::endl;
        EvalResult result = evaluateUnaryExp(node->children[0]);
        /*std::cout << "[DEBUG evaluateMulExp] evaluateUnaryExp 返回, isConstant: "
                  << result.isConstant << ", value: " << result.value << std::endl;*/
        return result;
    }

    /*std::cout << "[DEBUG evaluateMulExp] 多子节点情况，开始处理乘除模运算" << std::endl;
    std::cout << "[DEBUG evaluateMulExp] 递归调用 evaluateMulExp (左操作数)" << std::endl;*/

    EvalResult left = evaluateMulExp(node->children[0]);
    /*std::cout << "[DEBUG evaluateMulExp] 左操作数结果: isConstant=" << left.isConstant
              << ", value=" << left.value << std::endl;

    std::cout << "[DEBUG evaluateMulExp] 调用 evaluateUnaryExp (右操作数)" << std::endl;*/
    EvalResult right = evaluateUnaryExp(node->children[2]);
    /*std::cout << "[DEBUG evaluateMulExp] 右操作数结果: isConstant=" << right.isConstant
              << ", value=" << right.value << std::endl;*/

    if (!left.isConstant || !right.isConstant) {
        //std::cout << "[DEBUG evaluateMulExp] 操作数不是常量，返回默认值" << std::endl;
        return EvalResult(0, false);
    }

    std::string op = node->children[1]->value;
    //std::cout << "[DEBUG evaluateMulExp] 运算符: '" << op << "'" << std::endl;

    if (op == "*") {
        int result = left.value * right.value;
        /*std::cout << "[DEBUG evaluateMulExp] 乘法运算: " << left.value << " * "
                  << right.value << " = " << result << std::endl;*/
        return EvalResult(result, true);
    } else if (op == "/") {
        if (right.value == 0) {
            //std::cout << "[DEBUG evaluateMulExp] 除零错误" << std::endl;
            return EvalResult(0, false);
        }
        int result = left.value / right.value;
        /*std::cout << "[DEBUG evaluateMulExp] 除法运算: " << left.value << " / "
                  << right.value << " = " << result << std::endl;*/
        return EvalResult(result, true);
    } else if (op == "%") {
        if (right.value == 0) {
            //std::cout << "[DEBUG evaluateMulExp] 模零错误" << std::endl;
            return EvalResult(0, false);
        }
        int result = left.value % right.value;
        /*std::cout << "[DEBUG evaluateMulExp] 取模运算: " << left.value << " % "
                  << right.value << " = " << result << std::endl;*/
        return EvalResult(result, true);
    }

    //std::cout << "[DEBUG evaluateMulExp] 未知运算符，返回默认值" << std::endl;
    return EvalResult(0, false);
}

EvalResult SemanticAnalyzer::evaluateUnaryExp(const std::shared_ptr<TreeNode>& node) {
    // UnaryExp → PrimaryExp | Ident '(' [FuncRParams] ')' | UnaryOp UnaryExp
    //情况1：处理基本表达式（PrimaryExp）
    //如果只有一个子节点且是基本表达式，直接递归求值
    if (node->children.size() == 1 && node->children[0]->nodeType == NodeType::PRIMARY_EXP) {
        return evaluatePrimaryExp(node->children[0]);
    }

    // 处理一元运算符
    if (node->children.size() == 2 && node->children[0]->nodeType == NodeType::UNARY_OP) {
        EvalResult operand = evaluateUnaryExp(node->children[1]);
        if (!operand.isConstant) {
            return EvalResult(0, false);
        }

        std::string op = node->children[0]->value;
        if (op == "+") {
            return EvalResult(operand.value, true);
        } else if (op == "-") {
            return EvalResult(-operand.value, true);
        } else if (op == "!") {
            return EvalResult(operand.value == 0 ? 1 : 0, true);
        }
    }

    // 函数调用不是常量表达式
    // 情况3: 函数调用 - 显式识别但不重复检查
    if (node->children.size() >= 2 &&
        node->children[0]->nodeType == NodeType::TERMINAL &&
        node->children[1]->value == "(") {

        // 记录调试信息或用于后续分析
        // 但不重复checkFunctionCall的工作

        return EvalResult(0, false);
    }


    return EvalResult(0, false);
}

EvalResult SemanticAnalyzer::evaluatePrimaryExp(const std::shared_ptr<TreeNode>& node) {
    // PrimaryExp → '(' Exp ')' | LVal | Number
    //情况1：括号表达式 (Exp)
    //情况2：左值表达式 LVal
    //情况3：数字字面量 Number
    if (node->children.empty()) return EvalResult(0, false);//边界检查：如果没有子节点，直接返回默认的非常量结果

    auto firstChild = node->children[0];
    //关键设计：基于文法规则，PrimaryExp总是只有一个主要子节点
    if (firstChild->nodeType == NodeType::EXP) {
        return evaluateExpression(firstChild);
    } else if (firstChild->nodeType == NodeType::LVAL) {
        return evaluateLVal(firstChild);
    } else if (firstChild->nodeType == NodeType::NUMBER) {
        return evaluateNumber(firstChild);
    }

    return EvalResult(0, false);
}

EvalResult SemanticAnalyzer::evaluateLVal(const std::shared_ptr<TreeNode>& node) {
    // LVal → Ident ['[' Exp ']']
    //左值是普通常量或数组中的某个元素
    // 提取标识符名称
    std::string ident;
    bool foundIdent = false;

    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::TERMINAL) {
            // 更精确的标识符识别：排除所有非标识符的终结符
            if (child->value != "[" && child->value != "]" &&
                child->value != "=" && !child->value.empty()) {
                // 检查是否可能是关键字（简化检查）
                if (child->value != "int" && child->value != "void" &&
                    child->value != "const" && child->value != "static" &&
                    child->value != "if" && child->value != "for" &&
                    child->value != "while" && child->value != "return" &&
                    child->value != "break" && child->value != "continue" &&
                    child->value != "printf") {
                    ident = child->value;
                    foundIdent = true;
                    break;
                }
            }
        }
    }

    if (!foundIdent || ident.empty()) {
        return EvalResult(0, false, SymbolType::Int);
    }

    // 在符号表中查找标识符
    SymbolEntry* symbol = symbolTable.findSymbol(ident);
    if (!symbol) {
        // 符号未定义，在常量求值阶段不报告错误（在visitLVal中处理）
        return EvalResult(0, false, SymbolType::Int);
    }

    // 检查是否是常量（只有常量才能在常量表达式中使用）
    if (!symbol->isConstant()) {
        return EvalResult(0, false, symbol->type);
    }

    // 检查是否是数组元素访问
    std::vector<std::shared_ptr<TreeNode>> expChildren;
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::EXP) {
            expChildren.push_back(child);
        }
    }

    // 处理数组元素访问
    if (!expChildren.empty()) {
        // 计算下标表达式
        EvalResult indexResult = evaluateExpression(expChildren[0]);
        if (!indexResult.isConstant) {
            return EvalResult(0, false, symbol->type);
        }

        int index = indexResult.value;

        // 检查数组边界
        if (symbol->isArray()) {
            if (symbol->arraySize <= 0) {
                // 数组大小未知
                return EvalResult(0, true, symbol->type);
            }

            if (index < 0 || index >= symbol->arraySize) {
                // 数组越界，但在常量求值阶段不报错，返回默认值
                return EvalResult(0, true, symbol->type);
            }

            // 注意：当前符号表结构不支持存储数组元素的具体值
            // 这里返回数组基值作为占位符（实际实现需要扩展符号表）
            return EvalResult(symbol->value, true, symbol->type);
        } else {
            // 对非数组变量使用下标访问，语法错误
            // 在常量求值阶段返回基值，错误在语义分析中处理
            return EvalResult(symbol->value, true, symbol->type);
        }
    }

    // 简单变量访问，返回常量值
    return EvalResult(symbol->value, true, symbol->type);
}

EvalResult SemanticAnalyzer::evaluateNumber(const std::shared_ptr<TreeNode>& node) {
    // Number → IntConst
    if (!node->children.empty()) {
        auto terminalNode = node->children[0];
        try {
            int value = std::stoi(terminalNode->value);
            return EvalResult(value, true);
        } catch (const std::exception& e) {
            return EvalResult(0, false);
        }
    }
    return EvalResult(0, false);
}

/*
    checkFunctionCall 函数是函数调用语义检查器，专门验证函数调用的合法性。它检测三种主要的语义错误：
    错误c：未定义的函数或标识符不是函数
    错误d：函数参数个数不匹配
    错误e：函数参数类型不匹配
 */
//有大问题，回头再检查
bool SemanticAnalyzer::checkFunctionCall(const std::string& funcName,
                                         const std::vector<std::shared_ptr<TreeNode>>& actualParams,
                                         int line) {
    // 首先检查是否是系统保留函数
    if (isSystemFunction(funcName)) {
        return checkSystemFunctionCall(funcName, actualParams, line);
    }

    SymbolEntry* funcSymbol = symbolTable.findSymbol(funcName);
    if (!funcSymbol) {
        addError(line, "c"); // 未定义的函数
        return false;
    }

    if (!funcSymbol->isFunction()) {
        addError(line, "c"); // 不是函数
        return false;
    }

    // 检查参数个数
    if (actualParams.size() != funcSymbol->paramTypes.size()) {
        addError(line, "d"); // 参数个数不匹配
        return false;
    }

    // 使用新的参数类型检查逻辑
    for (size_t i = 0; i < actualParams.size(); ++i) {
        const auto& expectedParam = funcSymbol->paramTypes[i];

        // 分析实际参数的类型
        ParamType actualType = analyzeParamType(actualParams[i]);

        // 类型匹配检查
        if (!isParamTypeMatch(expectedParam, actualType)) {
            addError(line, "e"); // 参数类型不匹配
            return false;
        }

        // 额外的语义检查：常量数组不能作为参数传递
        if (actualType.isArray && actualType.isConst && !actualType.isArrayElement) {
            // 检查是否是常量数组
            std::string ident = "";
            if (actualParams[i]->nodeType == NodeType::LVAL) {
                ident = extractIdentFromLVal(actualParams[i]);
            }

            if (!ident.empty()) {
                SymbolEntry* paramSymbol = symbolTable.findSymbol(ident);
                if (paramSymbol && paramSymbol->isConstant() && paramSymbol->isArray()) {
                    addError(line, "e"); // 常量数组不能作为参数
                    return false;
                }
            }
        }
    }

    return true;
}

void SemanticAnalyzer::checkReturnStatement(const std::shared_ptr<TreeNode>& expNode, int line) {
    if (currentFunctionType == SymbolType::VoidFunc && expNode != nullptr) {
        addError(line, "f"); // void函数有返回值的return语句
    } else if (currentFunctionType == SymbolType::IntFunc && expNode == nullptr) {
        // 在函数结束时检查，这里只记录return语句的存在
    }
    hasReturnStatement = true;
}

bool SemanticAnalyzer::checkLValAssignment(const std::shared_ptr<TreeNode>& lvalNode, int line) {

    std::string ident;
    for (const auto& child : lvalNode->children) {
        if (child->nodeType == NodeType::TERMINAL && !child->value.empty() &&
            child->value != "[" && child->value != "]") {
            ident = child->value;
            //std::cout<<child->value<<std::endl;
            break;
        }
    }
    //std::cout<<ident<<std::endl;

    if (!ident.empty()) {
        SymbolEntry* symbol = symbolTable.findSymbol(ident);

        /*// ********** 新增的调试输出 **********
        std::cout << "--- DEBUG: checkLValAssignment Check ---\n";
        std::cout << "LVal: " << ident << " (Line: " << line << ")\n";

        if (!symbol) {
            std::cout << "Status: NOT found. (Reporting 'c' error).\n";
        } else {
            bool isConst = symbol->isConstant();
            // 注意：getTypeString 是 SemanticAnalyzer 的成员函数，用于将 SymbolType 转换为字符串
            std::cout << "Status: Found.\n";
            std::cout << "Symbol Type: " << getTypeString(symbol->type) << "\n";
            std::cout << "Is Constant: " << (isConst ? "TRUE" : "FALSE") << "\n";
        }
        std::cout << "---------------------------------------\n";
        // *************************************/

        if (!symbol) {
            addError(line, "c"); // 未定义的名字

            return false;
        } else if (symbol->isConstant()) {
            addError(line, "h"); // 不能改变常量的值
            return false;
        }
        return true;
    }
    return false;
}

void SemanticAnalyzer::checkPrintfStatement(const std::shared_ptr<TreeNode>& node) {
    // 'printf' '(' StringConst { ',' Exp } ')' ';'
    int printfLine = node->line;
    std::string formatString;
    std::vector<std::shared_ptr<TreeNode>> expressions;

    bool foundString = false;
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::TERMINAL && child->value.find('"') != std::string::npos) {
            formatString = child->value;
            foundString = true;
        } else if (foundString && child->nodeType == NodeType::EXP) {
            expressions.push_back(child);
        }
    }

    if (formatString.empty()) {
        addError(printfLine, "l"); // 缺少格式字符串
        return;
    }

    // 统计格式字符 %d 的个数
    int formatSpecifierCount = 0;
    size_t pos = 0;
    while ((pos = formatString.find("%d", pos)) != std::string::npos) {
        formatSpecifierCount++;
        pos += 2; // 跳过 "%d"
    }

    // 检查格式字符与表达式个数是否匹配
    if (formatSpecifierCount != expressions.size()) {
        addError(printfLine, "l"); // 格式字符与表达式个数不匹配
    }
}

int SemanticAnalyzer::getArraySizeFromConstDef(const std::shared_ptr<TreeNode>& node) {
    // ConstDef → Ident ['[' ConstExp ']'] '=' ConstInitVal
    for (size_t i = 0; i < node->children.size(); ++i) {
        if (node->children[i]->nodeType == NodeType::TERMINAL &&
            node->children[i]->value == "[") {
            // 下一个应该是ConstExp
            if (i + 1 < node->children.size()) {
                EvalResult size = evaluateConstExp(node->children[i + 1]);
                if (size.isConstant && size.value > 0) {
                    return size.value;
                }
            }
            break;
        }
    }
    return -1; // 不是数组或大小无效
}

void SemanticAnalyzer::addError(int line, const std::string& code) {
    // 检查是否已经有该行号的错误（一行最多一个错误）
    if (hasErrorOnLine(line)) {
        return;
    }
    semanticErrors.push_back({line, code});
}

bool SemanticAnalyzer::hasErrorOnLine(int line) const {
    for (const auto& error : semanticErrors) {
        if (error.line == line) {
            return true;
        }
    }
    return false;
}

void SemanticAnalyzer::analyze(const std::shared_ptr<TreeNode>& root) {
    if (root && root->nodeType == NodeType::COMP_UNIT) {
        visitCompUnit(root);
    }
}

bool SemanticAnalyzer::hasError() const {
    return !semanticErrors.empty();
}

const std::vector<SemanticError>& SemanticAnalyzer::getSemanticErrors() const {
    return semanticErrors;
}

void SemanticAnalyzer::writeSymbolTable(const std::string& filename) const {
    std::ofstream fout(filename);
    if (!fout.is_open()) {
        std::cerr << "无法打开符号表文件: " << filename << std::endl;
        return;
    }

    auto allSymbols = symbolTable.getAllSymbols();

    // 按作用域和声明顺序排序
    std::stable_sort(allSymbols.begin(), allSymbols.end(),
              [](const SymbolEntry& a, const SymbolEntry& b) {
                  if (a.scope != b.scope) return a.scope < b.scope;
                  return a.line < b.line;
              });

    for (const auto& symbol : allSymbols) {
        fout << symbol.scope << " " << symbol.name << " " << getTypeString(symbol.type) << "\n";
    }

    fout.close();
}

void SemanticAnalyzer::visitCompUnit(const std::shared_ptr<TreeNode>& node) {
    debugPrint("visitCompUnit", node);
    visitDepth++;

    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::CONST_DECL || child->nodeType == NodeType::VAR_DECL) {
            visitDecl(child);
        } else if (child->nodeType == NodeType::FUNC_DEF) {
            visitFuncDef(child);
        } else if (child->nodeType == NodeType::MAIN_FUNC_DEF) {
            visitMainFuncDef(child);
        }
    }

    visitDepth--;
    debugPrint("visitCompUnit", "exit");
}

void SemanticAnalyzer::visitDecl(const std::shared_ptr<TreeNode>& node) {

    debugPrint("visitDecl", node);
    visitDepth++;

    if (node->nodeType == NodeType::CONST_DECL) {
        visitConstDecl(node);
    } else if (node->nodeType == NodeType::VAR_DECL) {
        visitVarDecl(node);
    }

    visitDepth--;
    debugPrint("visitDecl", "exit");
}

void SemanticAnalyzer::visitConstDecl(const std::shared_ptr<TreeNode>& node) {

    debugPrint("visitConstDecl", node);
    visitDepth++;

    // ConstDecl → 'const' BType ConstDef { ',' ConstDef } ';'
    bool isGlobal = isGlobalScope();
    //作用：判断当前是否在全局作用域
    //意义：全局常量与局部常量的处理方式不同（如符号表管理、内存分配等）

    debugPrint("visitConstDecl", "isGlobal: " + std::to_string(isGlobal));

    int constDefCount = 0;

    // 处理所有ConstDef
    for (const auto& child : node->children) {//遍历常量声明节点的所有子节点
        if (child->nodeType == NodeType::CONST_DEF) {//过滤出类型为CONST_DEF的子节点,忽略其他子节点（如关键字'const'、类型BType、逗号、分号等）
            visitConstDef(child, isGlobal);//对每个常量定义调用专门的访问方法,传递isGlobal标志，告知该常量定义的作用域
        }
    }

    debugPrint("visitConstDecl", "processed " + std::to_string(constDefCount) + " ConstDef");

    visitDepth--;
    debugPrint("visitConstDecl", "exit");

}

void SemanticAnalyzer::visitVarDecl(const std::shared_ptr<TreeNode>& node) {

    debugPrint("visitVarDecl", node);
    visitDepth++;

    // VarDecl → ['static'] BType VarDef { ',' VarDef } ';'
    bool isGlobal = isGlobalScope();
    bool isStatic = false;

    // 检查是否有static
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::TERMINAL && child->value == "static") {
            isStatic = true;
            break;
        }
    }

    debugPrint("visitVarDecl", "isGlobal: " + std::to_string(isGlobal) +
                               ", isStatic: " + std::to_string(isStatic));


    int varDefCount = 0;

    // 处理所有VarDef
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::VAR_DEF) {
            visitVarDef(child, isGlobal, isStatic);
        }
    }

    debugPrint("visitVarDecl", "processed " + std::to_string(varDefCount) + " VarDef");

    visitDepth--;
    debugPrint("visitVarDecl", "exit");
}

void SemanticAnalyzer::visitConstDef(const std::shared_ptr<TreeNode>& node, bool isGlobal) {
    debugPrint("visitConstDef", node);
    visitDepth++;

    // 提取标识符名称
    std::string ident;
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::TERMINAL && !child->value.empty() &&
            child->value != "[" && child->value != "]" && child->value != "=") {
            ident = child->value;
            break;
        }
    }

    debugPrint("visitConstDef", "identifier: '" + ident + "', isGlobal: " + std::to_string(isGlobal));

    if (ident.empty()) {
        debugPrint("visitConstDef", "WARNING: empty identifier");
        visitDepth--;
        return;
    }

    // 验证作用域状态
    int currentScope = getCurrentScopeId();
    debugPrint("visitConstDef", "current scope ID: " + std::to_string(currentScope));

    if (currentScope <= 0) {
        debugPrint("visitConstDef", "ERROR: invalid scope ID");
        printf("visitConstDef   b");
        addError(node->line, "b");
        visitDepth--;
        return;
    }

    // =========================================================================
    //                            【主要修改区域】
    // =========================================================================

    // 1. 【新增逻辑】判断是否为数组常量
    bool isArray = false;
    for (const auto& child : node->children) {
        // 检查 ConstDef 的子节点中是否存在方括号 '[' 或数组维度 CONST_EXP
        if (child->nodeType == NodeType::CONST_EXP ||
            (child->nodeType == NodeType::TERMINAL && child->value == "[")) {
            isArray = true;
            break;
        }
    }

    // 2. 【新增逻辑】根据是否为数组，直接设置 SymbolType
    SymbolType type;
    if (isArray) {
        type = SymbolType::ConstIntArray;
    } else {
        type = SymbolType::ConstInt; // 🐛 修复：确保非数组常量被注册为 ConstInt
    }

    // SymbolType type = getVarDefType(node, true, false); // <-- 【删除】旧的错误调用
    debugPrint("visitConstDef", "symbol type: " + getTypeString(type));
    // =========================================================================

    SymbolEntry entry(ident, type, currentScope, node->line);

    // 计算数组大小
    if (entry.isArray()) {
        debugPrint("visitConstDef", "processing array constant");
        entry.arraySize = getArraySizeFromConstDef(node);
        debugPrint("visitConstDef", "array size: " + std::to_string(entry.arraySize));
    }

    // 计算常量值（安全版本）
    if (!entry.isArray()) {
        bool foundInitVal = false;
        for (const auto& child : node->children) {
            if (child->nodeType == NodeType::CONST_INIT_VAL) {
                foundInitVal = true;
                debugPrint("visitConstDef", "found CONST_INIT_VAL with " +
                                            std::to_string(child->children.size()) + " children");

                if (!child->children.empty() && child->children[0]) {
                    if (child->children[0]->nodeType == NodeType::CONST_EXP) {
                        debugPrint("visitConstDef", "evaluating CONST_EXP");
                        EvalResult result = evaluateConstExp(child->children[0]);
                        if (result.isConstant) {
                            entry.value = result.value;
                            debugPrint("visitConstDef", "constant value assigned: " + std::to_string(entry.value));
                        } else {
                            debugPrint("visitConstDef", "WARNING: ConstExp evaluation failed");
                            entry.value = 0;
                        }
                    } else {
                        debugPrint("visitConstDef", "WARNING: unexpected node type in CONST_INIT_VAL");
                        entry.value = 0;
                    }
                } else {
                    debugPrint("visitConstDef", "WARNING: CONST_INIT_VAL has no valid children");
                    entry.value = 0;
                }
                break;
            }
        }

        if (!foundInitVal) {
            debugPrint("visitConstDef", "WARNING: no CONST_INIT_VAL found, using default value 0");
            entry.value = 0;
        }
    }

    // 安全添加到符号表
    debugPrint("visitConstDef", "adding symbol to symbol table: " + ident);
    try {
        if (!symbolTable.addSymbol(entry)) {
            debugPrint("visitConstDef", "symbol redefinition error");
            printf("visitConstDef   b");
            addError(node->line, "b");
        } else {
            debugPrint("visitConstDef", "symbol added successfully");
        }
    } catch (const std::exception& e) {
        debugPrint("visitConstDef", "EXCEPTION in symbol table: " + std::string(e.what()));
        printf("visitConstDef   b");
        addError(node->line, "b");
    }

    visitDepth--;
    debugPrint("visitConstDef", "exit - " + ident);
}

void SemanticAnalyzer::visitVarDef(const std::shared_ptr<TreeNode>& node, bool isGlobal, bool isStatic) {
    // VarDef → Ident ['[' ConstExp ']'] | Ident ['[' ConstExp ']'] '=' InitVal

    debugPrint("visitVarDef", node);
    visitDepth++;

    //提取标识符名称
    std::string ident;
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::TERMINAL && !child->value.empty() &&
            child->value != "[" && child->value != "]" && child->value != "=") {
            ident = child->value;
            break;
        }
    }

    debugPrint("visitVarDef", "identifier: '" + ident + "', isGlobal: " +
                              std::to_string(isGlobal) + ", isStatic: " + std::to_string(isStatic));

    if (!ident.empty()) {

        // 2. 关键修改：统一使用getVarDefType进行类型判断
        SymbolType type = getVarDefType(node, false, isStatic);

        SymbolEntry entry(ident, type, getCurrentScopeId(), node->line);

        // 计算数组大小
        if (entry.isArray()) {
            for (size_t i = 0; i < node->children.size(); ++i) {
                if (node->children[i]->nodeType == NodeType::TERMINAL &&
                    node->children[i]->value == "[") {
                    if (i + 1 < node->children.size() &&
                        node->children[i + 1]->nodeType == NodeType::CONST_EXP) {
                        EvalResult size = evaluateConstExp(node->children[i + 1]);
                        if (size.isConstant && size.value > 0) {
                            entry.arraySize = size.value;
                            debugPrint("visitVarDef", "array size: " + std::to_string(entry.arraySize));
                        }
                    }
                    break;
                }
            }
        }

        if (!symbolTable.addSymbol(entry)) {
            //printf("visitVarDef   b\n");
            std::cout<<ident<<std::endl;

            addError(node->line, "b"); // 名字重定义
        }
    }

    visitDepth--;
    debugPrint("visitVarDef", "exit - " + ident);
}

void SemanticAnalyzer::visitFuncDef(const std::shared_ptr<TreeNode>& node) {
    debugPrint("visitFuncDef", node);
    visitDepth++;

    // FuncDef → FuncType Ident '(' [FuncFParams] ')' Block
    std::string funcName;//funcName：存储函数名称
    SymbolType funcType = SymbolType::VoidFunc;//funcType：函数返回类型，默认为void

    int funcBodyEndLine = node->line; // 初始化为节点行号

    std::shared_ptr<TreeNode> funcBodyNode = nullptr; // <--- 新增：保存函数体Block节点

    // 获取函数类型和名称
    //FUNC_TYPE：确定函数返回类型（int或void）
    //TERMINAL：过滤出函数名，排除括号等符号
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::FUNC_TYPE) {
            funcType = (child->value == "int") ? SymbolType::IntFunc : SymbolType::VoidFunc;

            debugPrint("visitFuncDef", "function type: " + child->value);

        } else if (child->nodeType == NodeType::TERMINAL && !child->value.empty() &&
                   child->value != "(" && child->value != ")") {
            funcName = child->value;

            debugPrint("visitFuncDef", "function name: " + funcName);

            break; // 修正：找到函数名后立即退出循环

        }
    }

    if (!funcName.empty()) {
        // 创建函数符号表项
        SymbolEntry funcEntry(funcName, funcType, getCurrentScopeId(), node->line);
        printf("11111111111111111111111111111111111111111111111111111111111111111111111111111   ");
        std::cout<<funcName<<"  "<<getCurrentScopeId()<<std::endl;


        // 设置当前函数信息
        currentFunctionType = funcType;
        currentFunctionName = funcName;
        hasReturnStatement = false;
        functionStartLine = node->line;

        // 进入函数作用域
        symbolTable.enterScope();

        // 处理形参
        for (const auto& child : node->children) {
            if (child->nodeType == NodeType::FUNC_F_PARAMS) {
                visitFuncFParams(child);
                // 收集参数信息到函数符号表项
                auto allSymbols = symbolTable.getAllSymbols();
                for (const auto& symbol : allSymbols) {
                    if (symbol.isParam && symbol.scope == getCurrentScopeId()) { // 下一个作用域
                        ParamInfo param;
                        param.type = symbol.type;
                        param.isArray = symbol.isArray();
                        param.name = symbol.name;
                        funcEntry.paramTypes.push_back(param);
                    }
                }
            }
        }

        // 添加函数到符号表
        if (!symbolTable.addSymbol(funcEntry)) {
            //printf("visitFuncDef   b");

            addError(node->line, "b"); // 函数名重定义
        }



        // 处理形参（已经在上面的visitFuncFParams中处理）

        // 处理函数体
        for (const auto& child : node->children) {
            if (child->nodeType == NodeType::BLOCK) {
                // 获取函数体结束行号
                funcBodyEndLine = getBlockEndLine(child);
                funcBodyNode = child; // <--- 保存Block节点以供CFA使用
                visitBlock(child, true);
            }
        }

        // *************** 修改后的 "g" 类错误检查 ***************
        // 检查int函数是否缺少返回路径
        if (funcType == SymbolType::IntFunc) {
            if (!funcBodyNode || canFallThrough(funcBodyNode)) {
                // 如果函数体为空，或者控制流可以“穿透”函数体，则报错
                addError(funcBodyEndLine, "g"); // 有返回值的函数缺少return语句
            }
        }
        // *******************************************************

        // 退出函数作用域
        symbolTable.exitScope();
    }

    visitDepth--;
    debugPrint("visitFuncDef", "exit - " + funcName);

}

void SemanticAnalyzer::visitMainFuncDef(const std::shared_ptr<TreeNode>& node) {
    // MainFuncDef → 'int' 'main' '(' ')' Block

    debugPrint("visitMainFuncDef", node);
    visitDepth++;

    int mainBodyEndLine = node->line; // 初始化为节点行号
    std::shared_ptr<TreeNode> mainBodyNode = nullptr; // <--- 新增：保存函数体Block节点

    // 设置当前函数信息
    currentFunctionType = SymbolType::IntFunc;
    currentFunctionName = "main";
    hasReturnStatement = false;
    functionStartLine = node->line;

    debugPrint("visitMainFuncDef", "entering main function, scope=" +
                                   std::to_string(getCurrentScopeId()));

    // 创建main函数符号表项（在全局作用域）
    SymbolEntry mainEntry("main", SymbolType::IntFunc, getCurrentScopeId(), node->line);

    // 进入函数作用域
    symbolTable.enterScope();

    debugPrint("visitMainFuncDef", "entering main function, scope=" +
                                   std::to_string(getCurrentScopeId()));

    // 处理函数体
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::BLOCK) {
            // 获取函数体结束行号
            mainBodyEndLine = getBlockEndLine(child);
            //printf("%d\n",mainBodyEndLine);

            mainBodyNode = child; // <--- 保存Block节点以供CFA使用

            visitBlock(child, true);
        }
    }

    // *************** 修改后的 "g" 类错误检查 ***************
    // 检查main函数是否缺少返回路径
    if (!mainBodyNode || canFallThrough(mainBodyNode)) {
        debugPrint("visitMainFuncDef", "ERROR: main function missing return path");
        addError(mainBodyEndLine, "g"); // 有返回值的函数缺少return语句
    } else {
        debugPrint("visitMainFuncDef", "main function has guaranteed return path");
    }
    // *******************************************************


    // 退出函数作用域
    symbolTable.exitScope();

    debugPrint("visitMainFuncDef", "exited function scope, current: " +
                                   std::to_string(getCurrentScopeId()));

    visitDepth--;
    debugPrint("visitMainFuncDef", "exit");
}

void SemanticAnalyzer::visitFuncFParams(const std::shared_ptr<TreeNode>& node) {//这个函数是语义分析器中处理函数形式参数列表的入口函数

    debugPrint("visitFuncFParams", node);
    visitDepth++;

    int paramCount = 0;

    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::FUNC_F_PARAM) {
            visitFuncFParam(child);
        }
    }
    debugPrint("visitFuncFParams", "processed " + std::to_string(paramCount) + " parameters");

    visitDepth--;
    debugPrint("visitFuncFParams", "exit");

}

void SemanticAnalyzer::visitFuncFParam(const std::shared_ptr<TreeNode>& node) {
    // FuncFParam → BType Ident ['[' ']']

    debugPrint("visitFuncFParam", node);
    visitDepth++;

    std::string paramName;
    bool isArray = false;

    for (const auto& child : node->children) {
        //printf("*");
        if (child->nodeType == NodeType::TERMINAL) {
            //std::cout<<child->value;
            //printf("    ");
            if (child->value == "[") {
                //printf("3333333333333333\n");
                isArray = true;
            } else if (!child->value.empty() && child->value != "int" &&
                       child->value != "]" && child->value != "void") {
                paramName = child->value;
                //printf("444444444444444\n");

                //break; // 修正：找到参数名后立即退出循环
            }
        }
        //std::cout<<std::endl;
    }

    debugPrint("visitFuncFParam", "parameter: '" + paramName + "', isArray: " +
                                  std::to_string(isArray));

    if (!paramName.empty()) {
        SymbolType paramType = isArray ? SymbolType::IntArray : SymbolType::Int;
        //std::cout<<isArray<<std::endl;
        SymbolEntry paramEntry(paramName, paramType, getCurrentScopeId(), node->line, true);

        if (!symbolTable.addSymbol(paramEntry)) {
            addError(node->line, "b"); // 参数名重定义
        }
    }

    visitDepth--;
    debugPrint("visitFuncFParam", "exit - " + paramName);
}

void SemanticAnalyzer::visitBlock(const std::shared_ptr<TreeNode>& node, bool isFunctionBody) {

    debugPrint("visitBlock", node);
    visitDepth++;

    // 只有非函数体的块才需要进入新作用域
    if (!isFunctionBody) {
        symbolTable.enterScope();
        debugPrint("visitBlock", "entered new scope: " + std::to_string(getCurrentScopeId()));
    } else {
        debugPrint("visitBlock", "function body, using existing function scope: " +
                                 std::to_string(getCurrentScopeId()));
    }


    for (const auto& child : node->children) {
        // 跳过大括号终端节点
        if (child->nodeType == NodeType::TERMINAL &&
            (child->value == "{" || child->value == "}")) {
            continue;
        }

        // 直接处理声明和语句节点
        if (child->nodeType == NodeType::CONST_DECL ||
            child->nodeType == NodeType::VAR_DECL) {
            visitDecl(child);
        } else if (child->nodeType == NodeType::STMT) {
            visitStmt(child);
        }
    }

    // 只有非函数体的块才需要退出作用域
    if (!isFunctionBody) {
        symbolTable.exitScope();
        debugPrint("visitBlock", "exited scope, current: " + std::to_string(getCurrentScopeId()));
    }

    visitDepth--;
}

void SemanticAnalyzer::visitStmt(const std::shared_ptr<TreeNode>& node) {

    debugPrint("visitStmt", node);
    visitDepth++;


    if (node->children.empty()) {

        debugPrint("visitStmt", "empty statement");
        visitDepth--;

        return;
    }

    auto firstChild = node->children[0];

    std::string stmtType = "unknown";

    // 赋值语句: LVal '=' Exp ';'
    if (firstChild->nodeType == NodeType::LVAL && node->children.size() > 1 &&
        node->children[1]->value == "=") {//识别特征：LVal '=' Exp ';'

        stmtType = "assignment";
        debugPrint("visitStmt", "assignment statement");

        if (checkLValAssignment(firstChild, node->line)) {
            if (node->children.size() > 2) {
                visitExp(node->children[2]); // 检查右值表达式
            }
        }
    }
    // return语句
    else if (firstChild->value == "return") {

        stmtType = "return";
        debugPrint("visitStmt", "return statement");

        std::shared_ptr<TreeNode> expNode = nullptr;
        for (size_t i = 1; i < node->children.size(); ++i) {
            if (node->children[i]->nodeType == NodeType::EXP) {
                expNode = node->children[i];
                visitExp(expNode);
                break;
            }
        }
        checkReturnStatement(expNode, node->line);
    }
    // break/continue语句
    else if (firstChild->value == "break" || firstChild->value == "continue") {

        stmtType = firstChild->value;
        debugPrint("visitStmt", stmtType + " statement, loopDepth=" + std::to_string(loopDepth));

        if (loopDepth == 0) {
            addError(node->line, "m"); // 在非循环块中使用break/continue
        }
    }
    // 块语句
    else if (firstChild->nodeType == NodeType::BLOCK) {

        stmtType = "block";
        debugPrint("visitStmt", "block statement");

        visitBlock(firstChild);
    }
    // if语句
    else if (firstChild->value == "if") {

        stmtType = "if";
        debugPrint("visitStmt", "if statement");

        // 处理条件表达式
        for (const auto& child : node->children) {
            if (child->nodeType == NodeType::COND) {
                visitCond(child);
            }
        }
        // 处理then语句
        bool foundThen = false;
        for (const auto& child : node->children) {
            if (child->nodeType == NodeType::STMT) {
                if (!foundThen) {
                    visitStmt(child);
                    foundThen = true;
                } else {
                    // else语句
                    visitStmt(child);
                }
            }
        }
    }
    // for语句
    else if (firstChild->value == "for") {

        stmtType = "for";
        debugPrint("visitStmt", "for statement, entering loop");

        loopDepth++;

        // 处理初始化语句
        for (const auto& child : node->children) {
            if (child->nodeType == NodeType::FOR_STMT) {
                visitForStmt(child); // ForStmt本质上是赋值语句
            } else if (child->nodeType == NodeType::COND) {
                visitCond(child);
            } else if (child->nodeType == NodeType::STMT) {
                visitStmt(child);
            }
        }

        loopDepth--;
    }
    // printf语句
    else if (firstChild->value == "printf") {

        stmtType = "printf";
        debugPrint("visitStmt", "printf statement");

        checkPrintfStatement(node);
        // 检查printf中的表达式
        for (const auto& child : node->children) {
            if (child->nodeType == NodeType::EXP) {
                visitExp(child);
            }
        }
    }
    // 表达式语句
    else if (firstChild->nodeType == NodeType::EXP) {
        stmtType = "expression";  // 修正：应该是表达式语句，不是printf
        debugPrint("visitStmt", "printf statement");

        visitExp(firstChild);
    }

    visitDepth--;
    debugPrint("visitStmt", "exit - " + stmtType);
}

void SemanticAnalyzer::visitForStmt(const std::shared_ptr<TreeNode>& node) {
    debugPrint("visitForStmt", node);
    visitDepth++;

    // ForStmt → LVal '=' Exp { ',' LVal '=' Exp }
    // 节点结构：LVal, '=', Exp, ',', LVal, '=', Exp, ...

    // 按组处理赋值语句：每组包含 LVal, '=', Exp
    for (size_t i = 0; i < node->children.size(); ) {
        if (i < node->children.size() &&
            node->children[i]->nodeType == NodeType::LVAL) {

            auto lvalNode = node->children[i];
            checkLValAssignment(lvalNode, node->line);
            visitExp(node->children[i+2]);

            // 移动到下一组（当前组占3个节点：LVal, '=', Exp）
            i += 3;
        }
        else if (i < node->children.size() &&
                 node->children[i]->nodeType == NodeType::TERMINAL &&
                 node->children[i]->value == ",") {
            // 跳过逗号分隔符，继续处理下一组
            i++;
        }
    }

    visitDepth--;
    debugPrint("visitForStmt", "exit");
}

void SemanticAnalyzer::visitLVal(const std::shared_ptr<TreeNode>& node, bool isAssignment) {

    debugPrint("visitLVal", node);
    visitDepth++;

    //printf("99999999999999999999999999999999999\n");

    std::string ident;
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::TERMINAL && !child->value.empty() &&
            child->value != "[" && child->value != "]") {
            ident = child->value;
            //printf("99999999999999999999999999999999999\n");
            break;
        }
    }

    debugPrint("visitLVal", "identifier: '" + ident + "', isAssignment: " +
                            std::to_string(isAssignment));

    if (!ident.empty()) {
        //printf("99999999999999999999999999999999999\n");
        SymbolEntry* symbol = symbolTable.findSymbol(ident);
        if (!symbol) {

            debugPrint("visitLVal", "ERROR: undefined symbol '" + ident + "'");

            addError(node->line, "c"); // 未定义的名字
        } else if (isAssignment && symbol->isConstant()) {

            debugPrint("visitLVal", "ERROR: cannot assign to constant '" + ident + "'");

            addError(node->line, "h"); // 不能改变常量的值
        }else {
            debugPrint("visitLVal", "symbol found: " + ident + ", type: " +
                                    getTypeString(symbol->type));
        }

        // 检查数组访问
        for (const auto& child : node->children) {
            if (child->nodeType == NodeType::EXP) {
                visitExp(child);
            }
        }
    }


    visitDepth--;
}
//神奇的逻辑
void SemanticAnalyzer::visitExp(const std::shared_ptr<TreeNode>& node) {
    debugPrint("visitExp", node);
    visitDepth++;

    // 根据文法 Exp → AddExp，直接委托给 AddExp 处理
    if (!node->children.empty()) {
        // 寻找 ADD_EXP 子节点
        for (const auto& child : node->children) {
            if (child->nodeType == NodeType::ADD_EXP) {
                visitAddExp(child);
                break;
            }
        }
    }

    debugPrint("visitExp", "exit");
    visitDepth--;
}

void SemanticAnalyzer::visitAddExp(const std::shared_ptr<TreeNode>& node) {
    debugPrint("visitAddExp", node);
    visitDepth++;

    // AddExp → MulExp | AddExp ('+' | '−') MulExp
    if (node->children.size() == 1) {
        // 情况1: AddExp → MulExp
        visitMulExp(node->children[0]);
    } else {
        // 情况2: AddExp → AddExp ('+' | '−') MulExp
        // 处理左操作数 (AddExp)
        visitAddExp(node->children[0]);

        // 处理右操作数 (MulExp)
        visitMulExp(node->children[2]);

        // 运算符在 node->children[1]，这里不需要特别处理
    }

    debugPrint("visitAddExp", "exit");
    visitDepth--;
}

void SemanticAnalyzer::visitMulExp(const std::shared_ptr<TreeNode>& node) {
    debugPrint("visitMulExp", node);
    visitDepth++;

    // MulExp → UnaryExp | MulExp ('*' | '/' | '%') UnaryExp
    if (node->children.size() == 1) {
        // 情况1: MulExp → UnaryExp
        visitUnaryExp(node->children[0]);
    } else {
        // 情况2: MulExp → MulExp ('*' | '/' | '%') UnaryExp
        // 处理左操作数 (MulExp)
        visitMulExp(node->children[0]);

        // 处理右操作数 (UnaryExp)
        visitUnaryExp(node->children[2]);

        // 运算符在 node->children[1]，这里不需要特别处理
    }

    debugPrint("visitMulExp", "exit");
    visitDepth--;
}

void SemanticAnalyzer::visitUnaryExp(const std::shared_ptr<TreeNode>& node) {



    debugPrint("visitUnaryExp", node);
    visitDepth++;

    // 检查函数调用
    if (node->children.size() >= 2) {
        auto firstChild = node->children[0];
        auto secondChild = node->children[1];


        /*std::cout << "=== visitUnaryExp: Checking function call at line " << node->line << " ===" << std::endl;
        std::cout << ">>> First child: type=" << static_cast<int>(firstChild->nodeType)
                  << ", value='" << firstChild->value << "'" << std::endl;
        std::cout << ">>> Second child: type=" << static_cast<int>(secondChild->nodeType)
                  << ", value='" << secondChild->value << "'" << std::endl;
*/

        if (firstChild->nodeType == NodeType::TERMINAL && secondChild->value == "(") {
            std::string funcName = firstChild->value;

            debugPrint("visitUnaryExp", "function call: " + funcName);

            std::vector<std::shared_ptr<TreeNode>> actualParams;

            // 收集实际参数
            for (size_t i = 2; i < node->children.size(); ++i) {
                auto child = node->children[i];
                if (child->nodeType == NodeType::EXP ||
                    (child->nodeType == NodeType::FUNC_R_PARAMS && !child->children.empty())) {
                    if (child->nodeType == NodeType::FUNC_R_PARAMS) {
                        // 处理多个参数
                        for (const auto& paramChild : child->children) {
                            if (paramChild->nodeType == NodeType::EXP) {
                                actualParams.push_back(paramChild);
                                visitExp(paramChild);
                            }
                        }
                    } else {
                        actualParams.push_back(child);
                        visitExp(child);
                    }
                }
            }

            checkFunctionCall(funcName, actualParams, node->line);

            visitDepth--;
            debugPrint("visitUnaryExp", "exit - function call: " + funcName);

            return;
        }
    }

    debugPrint("visitUnaryExp", "non-function call case, children: " +
                                std::to_string(node->children.size()));

    // 处理其他一元表达式
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::PRIMARY_EXP) {
            if (!child->children.empty()) {
                auto primaryChild = child->children[0];
                if (primaryChild->nodeType == NodeType::LVAL) {
                    visitLVal(primaryChild, false);
                } else if (primaryChild->nodeType == NodeType::EXP) {
                    visitExp(primaryChild);
                }
            }
        } else if (child->nodeType == NodeType::UNARY_EXP) {

            debugPrint("visitUnaryExp", "recursive call to UnaryExp");

            visitUnaryExp(child);
        }
    }

    visitDepth--;
    debugPrint("visitUnaryExp", "exit");
}

void SemanticAnalyzer::visitCond(const std::shared_ptr<TreeNode>& node) {
    // Cond → LOrExp

    debugPrint("visitCond", node);
    visitDepth++;

    if (!node->children.empty()) {

        debugPrint("visitCond", "evaluating condition expression");

        visitExp(node->children[0]); // 条件表达式也是表达式
    } else {
        debugPrint("visitCond", "WARNING: empty condition");
    }

    visitDepth--;
    debugPrint("visitCond", "exit");
}

/**
 * @brief 递归检查一个AST节点是否可以“穿透”（即执行流可以到达其末尾）。
 * * 这是实现 "g" 类错误（有返回值的函数缺少return）检查的核心。
 */
bool SemanticAnalyzer::canFallThrough(const std::shared_ptr<TreeNode>& node) {
    if (!node) {
        return true; // 空节点（例如可选的else分支）总是可以“穿透”
    }

    switch (node->nodeType) {

        // 关键情况 1: 代码块 (BLOCK)
        case NodeType::BLOCK: {
            // 遍历块中的所有子项 (BlockItem)
            for (const auto& child : node->children) {
                // 我们只关心可执行的语句
                if (child->nodeType == NodeType::STMT) {
                    if (!canFallThrough(child)) {
                        // 如果这个子语句 (child) 保证会 return (返回 false)，
                        // 那么这个块在这一点之后就无法“穿透”了。
                        return false;
                    }
                }
                // 声明 (CONST_DECL, VAR_DECL) 会被跳过，控制流继续
            }
            // 如果遍历完所有子语句都没有遇到 `return false`，
            // 那么这个块可以“穿透”。
            return true;
        }

            // 关键情况 2: 语句 (STMT)
        case NodeType::STMT: {
            if (node->children.empty()) {
                return true; // 空语句 (e.g., ";") 总是可以“穿透”
            }

            auto firstChild = node->children[0];

            // 2a. Return 语句
            if (firstChild->nodeType == NodeType::TERMINAL && firstChild->value == "return") {
                return false; // `return` 语句绝对无法“穿透”
            }

            // 2b. If 语句
            if (firstChild->nodeType == NodeType::TERMINAL && firstChild->value == "if") {
                std::shared_ptr<TreeNode> thenStmt = nullptr;
                std::shared_ptr<TreeNode> elseStmt = nullptr;

                // 查找 then 和 else 分支
                int stmtCount = 0;
                for (const auto& child : node->children) {
                    if (child->nodeType == NodeType::STMT) {
                        if (stmtCount == 0) {
                            thenStmt = child;
                        } else {
                            elseStmt = child;
                        }
                        stmtCount++;
                    }
                }

                if (elseStmt) {
                    // 情况：if-else
                    // 只有当 *两条* 分支都保证 return (都返回 false) 时，
                    // 整个 if-else 语句才无法“穿透”。
                    bool thenFalls = canFallThrough(thenStmt);
                    bool elseFalls = canFallThrough(elseStmt);
                    return thenFalls || elseFalls; // 如果任何一条分支可以“穿透”，则整体可以“穿透”
                } else {
                    // 情况：if (没有 else)
                    // 这是您 `fib` 示例中的关键！
                    // 因为 `else` 分支（即跳过if）总是存在的，
                    // 所以没有 `else` 的 `if` 语句 *永远* 可以“穿透”。
                    return true;
                }
            }

            // 2c. 循环语句 (for)
            if (firstChild->nodeType == NodeType::TERMINAL && firstChild->value == "for") {
                // 简单处理：我们假设循环可能一次都不执行（条件初始为false），
                // 或者循环会正常终止。
                // 因此，循环语句总是可以“穿透”的。
                return true;
            }

            // 2d. 块语句 ( { ... } 作为 STMT 的一部分)
            if (firstChild->nodeType == NodeType::BLOCK) {
                return canFallThrough(firstChild);
            }

            // 2e. 其他所有语句 (赋值, printf, break, continue, 表达式语句等)
            // 这些语句执行完毕后都会继续下一条，因此总是可以“穿透”。
            return true;
        }

            // 默认情况：声明、表达式等，都视为可以“穿透”
        case NodeType::CONST_DECL:
        case NodeType::VAR_DECL:
            return true;

        default:
            return true;
    }
}