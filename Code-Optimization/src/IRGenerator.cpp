#include "IRGenerator.h"
#include <fstream>
#include <iostream>

IRGenerator::IRGenerator(SymbolTable& symTable)
        : symbolTable(symTable) {
    // 初始进入全局作用域 (Scope 1)
    enterScope();

    // [新增] 激活所有全局变量 (Scope 1)
    Scope* globalScope = symbolTable.getScopeById(1);
    if (globalScope) {
        for (const auto& sym : globalScope->getSymbols()) {
            // 通过 lookupSymbol 或 findSymbol 获取指针
            // 注意：因为我们刚 enterScope，lookupSymbol 能查到 Scope 1 的符号
            SymbolEntry* entry = lookupSymbol(sym.name);
            if (entry) {
                activeSymbols.insert(entry);
            }
        }
    }
}

// === 基础辅助函数 ===

// === 重点修改：newTemp ===
// 根据文档 ：对于临时变量，我们应当计算出其偏移量，将其信息添加到符号表中
Operand* IRGenerator::newTemp() {
    std::string tempName = "t" + std::to_string(tempCounter++);

    // 如果在函数内，需要将临时变量注册到新的代码生成符号表中
    if (currentCodeGenInfo) {
        // 1. 获取当前栈帧大小作为偏移量 (紧接在局部变量之后)
        int offset = currentCodeGenInfo->frameSize;

        // 2. 创建表项
        CodeGenSymbolEntry entry(tempName, offset, true, false); // isTemp=true

        // 3. 存入 Map
        currentCodeGenInfo->symbolMap[tempName] = entry;

        // 4. 更新栈帧总大小 (临时变量占4字节)
        currentCodeGenInfo->frameSize += 4;
    }

    return new Operand(tempName, OperandType::TEMP);
}

Operand* IRGenerator::newLabel() {
    return new Operand("L" + std::to_string(labelCounter++), OperandType::LABEL);
}

Operand* IRGenerator::newImm(int value) {
    return new Operand(value);
}

//简单来说，它的任务是：“给我一个变量的名字，我告诉你它在当前上下文中具体是指哪一个变量（即对应的符号表项）。”
Operand* IRGenerator::getVar(const std::string& name) {
    // 从内层向外层查找
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        SymbolEntry* entry = (*it)->findSymbol(name);
        if (entry) {
            // [新增] 检查符号是否已激活
            // 如果 entry 存在但不在 activeSymbols 中，说明它是当前行下方才定义的局部变量
            // 根据 C 语义，此时应不可见，继续找外层
            if (activeSymbols.find(entry) == activeSymbols.end()) {
                continue;
            }

            // === [修改开始] ===
            // 创建 Operand，默认使用 entry->name
            Operand* op = new Operand(entry);

            // 检查是否需要重命名 (局部变量且非静态)
            // 全局变量 (scope==1) 和 静态变量 (StaticInt/Array) 不需要重命名
            bool isGlobal = (entry->scope == 1);
            bool isStatic = (entry->type == SymbolType::StaticInt ||
                             entry->type == SymbolType::StaticIntArray);

            // 如果是局部变量，添加 scope 后缀以区分重名
            if (!isGlobal && !isStatic) {
                op->name = entry->name + "_" + std::to_string(entry->scope);
            }
                // 【新增修复】如果是静态变量，使用其唯一 Label 作为名字
                // 这样 RegisterManager 就能区分 "s" (Layer 2) 和 "s" (Layer 3) 了
            else if (isStatic) {
                if (!entry->label.empty()) {
                    op->name = entry->label; // 例如 "static_s_76"
                } else {
                    // 兜底：如果没有 label，也加上 scope 后缀
                    op->name = entry->name + "_static_" + std::to_string(entry->scope);
                }
            }

            return op;
            // === [修改结束] ===
        }
    }
    std::cerr << "IR Error: Symbol " << name << " not found." << std::endl;
    return nullptr;
}

//在编译像 SysY (C 语言子集) 这样的语言时，字符串常量（例如 printf("Hello\n"); 中的 "Hello\n"）不能像整数立即数那样直接嵌入到代码指令中。它们必须存储在内存的静态数据区（.data 段），并通过**地址标签（Label）**来引用。
std::string IRGenerator::addStringConstant(const std::string& content) {
    std::string label = "str_" + std::to_string(stringCounter++);
    stringConstants[label] = content;
    return label;
}

//将几个符号转化成具体指令
void IRGenerator::emit(IROp op, Operand* result, Operand* arg1, Operand* arg2) {
    instructions.push_back(new IRInstruction(op, result, arg1, arg2));
}

//标签相关
void IRGenerator::emitLabel(Operand* label) {
    emit(IROp::LABEL, label);
}

void IRGenerator::enterScope() {
    iterScopeId++;
    Scope* scope = symbolTable.getScopeById(iterScopeId);
    if (!scope) {
        std::cerr << "IR Error: Sync scope failed. Expected ID " << iterScopeId << std::endl;
        return;
    }
    scopeStack.push_back(scope);
}

void IRGenerator::exitScope() {
    if (!scopeStack.empty()) {
        scopeStack.pop_back();
    }
}

// 这个函数的逻辑与 getVar 几乎一致，但它返回 SymbolEntry*
SymbolEntry* IRGenerator::lookupSymbol(const std::string& name) {
    // 使用 IRGenerator 自己的 scopeStack (从内层向外层查找)
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        SymbolEntry* entry = (*it)->findSymbol(name);
        if (entry) {
            return entry;
        }
    }
    return nullptr;
}

//从一个复合语法树节点（如左值 LVal 或变量定义 VarDef）中，提取出变量的标识符名称
std::string IRGenerator::extractIdent(const std::shared_ptr<TreeNode>& node) {
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::TERMINAL &&
            child->value != "[" && child->value != "]" && child->value != "=") {
            return child->value;
        }
    }
    return "";
}

// === AST 遍历 ===

void IRGenerator::generate(std::shared_ptr<TreeNode> root) {
    if (root) visitCompUnit(root);
}

void IRGenerator::visitCompUnit(const std::shared_ptr<TreeNode>& node) {
    // 遍历编译单元的所有子节点
    // 按照 SysY 文法，顺序通常是：全局变量/常量声明 -> 函数定义 -> Main函数
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::VAR_DECL) {
            visitVarDecl(child); // -> 会调用 visitVarDef
        }
        else if (child->nodeType == NodeType::CONST_DECL) {
            visitConstDecl(child); // -> 会调用 visitConstDef
        }
        else if (child->nodeType == NodeType::FUNC_DEF) {
            visitFuncDef(child);
        }
        else if (child->nodeType == NodeType::MAIN_FUNC_DEF) {
            visitMainFuncDef(child);
        }
    }
}

// === 重点修改：visitFuncDef ===
// 在此处初始化新符号表，并导入语义分析阶段已有的局部变量信息
void IRGenerator::visitFuncDef(const std::shared_ptr<TreeNode>& node) {
    std::string funcName;
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::TERMINAL && child->value != "(" &&
            child->value != ")" && child->value != "int" && child->value != "void") {
            funcName = child->value;
            break;
        }
    }

    Operand* funcLabel = new Operand(funcName, OperandType::LABEL);
    emitLabel(funcLabel);
    emit(IROp::FUNC_ENTRY);

    // 1. 初始化当前函数的 CodeGenInfo
    currentCodeGenInfo = &mipsCodeGenTable[funcName];
    currentCodeGenInfo->funcName = funcName;

    // 【关键】清空之前的参数列表，防止污染
    currentCodeGenInfo->paramList.clear();

    int baseOffset = 12;

    // 3. 从 SemanticAnalyzer 的符号表中获取该函数的所有局部符号
    //    注意：我们需要先 enterScope 才能访问到参数和函数体内的变量
    enterScope();

    // 获取当前作用域的所有符号（参数）
    Scope* funcScope = scopeStack.back();

    for (const auto& sym : funcScope->getSymbols()) {
        if (sym.isParam) {
            SymbolEntry* entry = funcScope->findSymbol(sym.name);
            if (entry) activeSymbols.insert(entry);
        }
    }

    // 遍历 Scope 收集符号
    for (const auto& sym : funcScope->getSymbols()) {
        // ================== [修正] 过滤静态变量 ==================
        bool isStatic = (sym.type == SymbolType::StaticInt ||
                         sym.type == SymbolType::StaticIntArray);

        // 如果是静态变量，跳过，不要加入栈帧表
        if (isStatic) {
            continue;
        }

        // === [修改开始] ===
        // 生成唯一名字：name_scope
        std::string uniqueName = sym.name + "_" + std::to_string(sym.scope);

        // 将 Semantic 符号信息转存到 CodeGen 表中
        CodeGenSymbolEntry entry;
        entry.name = uniqueName; // 使用唯一名
        entry.isParam = sym.isParam;
        entry.isTemp = false;

        // SemanticAnalyzer 计算的 offset 是从 0 开始的，加上 FP/RA 的 8 字节
        entry.offset = sym.offset + baseOffset;
        entry.size = sym.getByteSize();

        // 使用唯一名作为 Key 存入 symbolMap
        currentCodeGenInfo->symbolMap[uniqueName] = entry;

        // ================== [新增] 收集参数名 ==================
        // 为了支持 MIPSGenerator 在函数入口正确保存 $a0-$a3
        if (sym.isParam) {
            currentCodeGenInfo->paramList.push_back(uniqueName);
        }
    }

    // ================== [新增] 参数排序 ==================
    // 由于 symbolMap 可能是无序的，我们需要根据 offset 对 params 进行排序
    // 确保 params[0] 对应 $a0, params[1] 对应 $a1 ...
    std::sort(currentCodeGenInfo->paramList.begin(), currentCodeGenInfo->paramList.end(),
              [&](const std::string& a, const std::string& b) {
                  return currentCodeGenInfo->symbolMap[a].offset < currentCodeGenInfo->symbolMap[b].offset;
              });

    // 更新 frameSize：基础部分 (Semantic 计算的大小) + 8
    SymbolEntry* funcSym = lookupSymbol(funcName);
    if (funcSym) {
        currentCodeGenInfo->frameSize = funcSym->stackFrameSize + 12;
    } else {
        currentCodeGenInfo->frameSize = 12;
    }

    // 4. 处理函数体
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::BLOCK) {
            // 【修改前】 visitBlock(child, true);
            // 【修改后】 必须传入 false，让 Block 正常进入它自己的 Scope (Body Scope)
            // 这样 iterScopeId 才会正确消耗掉这个 ID，防止后续函数错位。
            visitBlock(child, true);
        }
    }

    emit(IROp::FUNC_EXIT);
    exitScope();

    // 清空指针
    currentCodeGenInfo = nullptr;
}

// === 重点修改：visitMainFuncDef ===
void IRGenerator::visitMainFuncDef(const std::shared_ptr<TreeNode>& node) {
    Operand* funcLabel = new Operand("main", OperandType::LABEL);
    emitLabel(funcLabel);
    emit(IROp::FUNC_ENTRY);

    // 1. 初始化 main 的 CodeGenInfo
    currentCodeGenInfo = &mipsCodeGenTable["main"];
    currentCodeGenInfo->funcName = "main";
    int baseOffset = 12;

    enterScope(); // Main scope

    // IRGenerator.cpp -> visitMainFuncDef 中建议删除的部分：
    // ================== 【新增缺失逻辑开始】 ==================
    // 我们必须遍历 main 函数作用域内的所有变量，
    // 将它们注册到 CodeGen 表中，并加上 baseOffset。
    Scope* mainScope = scopeStack.back();

    for (const auto& sym : mainScope->getSymbols()) {
        // 过滤静态变量 (它们在 .data 段)
        bool isStatic = (sym.type == SymbolType::StaticInt ||
                         sym.type == SymbolType::StaticIntArray);
        if (isStatic) continue;

        // === [修改开始] ===
        // 生成唯一名字
        std::string uniqueName = sym.name + "_" + std::to_string(sym.scope);

        // 这里的 sym.offset 是语义分析阶段计算的 (通常从0开始)
        // 我们需要加上 baseOffset (12) 转换为栈帧偏移
        CodeGenSymbolEntry entry;
        entry.name = uniqueName;
        entry.isParam = false; // main 没有参数
        entry.isTemp = false;

        // 【关键点】这里使用了 baseOffset！
        // 如果 sym.offset 是 0 (变量 k)，则 entry.offset = 12
        // 生成汇编时会变成 -12($fp)，安全！
        entry.offset = sym.offset + baseOffset;
        entry.size = sym.getByteSize();

        currentCodeGenInfo->symbolMap[uniqueName] = entry;
    }
    // ================== 【新增缺失逻辑结束】 ==================

    // 2. 获取 Main 函数的预计算大小
    SymbolEntry* mainSym = symbolTable.findSymbol("main");
    if (mainSym) {
        currentCodeGenInfo->frameSize = mainSym->stackFrameSize + 12;
    } else {
        currentCodeGenInfo->frameSize = 12;
    }

    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::BLOCK) {
            // 【修改前】 visitBlock(child, true);
            // 【修改后】
            visitBlock(child, true);
        }
    }

    emit(IROp::FUNC_EXIT);
    exitScope();

    currentCodeGenInfo = nullptr;
}

// 关键修正：作用域同步逻辑
void IRGenerator::visitBlock(const std::shared_ptr<TreeNode>& node, bool isFunctionBody) {
    // 1. 作用域管理
    // 如果不是函数体自带的Block，需要手动开启新作用域
    if (!isFunctionBody) {
        enterScope();
    }

    // 2. 遍历 BlockItem
    for (const auto& item : node->children) {
        // 处理 BlockItem 包装层 (如果 AST 结构是 Block -> BlockItem -> Stmt/Decl)
        std::shared_ptr<TreeNode> child = item;
        if (child->nodeType == NodeType::BLOCK_ITEM && !child->children.empty()) {
            child = child->children[0];
        }

        // 3. 任务分发
        if (child->nodeType == NodeType::STMT) {
            // Stmt 内部涵盖了 Block (嵌套作用域)、If、While、Return 等
            visitStmt(child);
        }
        else if (child->nodeType == NodeType::VAR_DECL) {
            visitVarDecl(child);
        }
        else if (child->nodeType == NodeType::CONST_DECL) {
            visitConstDecl(child);
        }
        // 注意：不再显式检查 NodeType::BLOCK，因为根据文法 Stmt -> Block，
        // 嵌套的 Block 会被 visitStmt 捕获。
    }

    // 4. 退出作用域
    if (!isFunctionBody) {
        exitScope();
    }
}

void IRGenerator::visitVarDecl(const std::shared_ptr<TreeNode>& node) {
    // VarDecl -> BType VarDef { ',' VarDef } ';'
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::VAR_DEF) {
            visitVarDef(child);
        }
    }
}

void IRGenerator::visitConstDecl(const std::shared_ptr<TreeNode>& node) {
    // ConstDecl -> 'const' BType ConstDef { ',' ConstDef } ';'
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::CONST_DEF) {
            visitConstDef(child);
        }
    }
}

// === 重点修改：visitVarDef ===
// 在遇到变量定义时，将其加入新的符号表
void IRGenerator::visitVarDef(const std::shared_ptr<TreeNode>& node) {
    std::string name = extractIdent(node);

    // 1. 直接查表获取 entry (无视激活状态，直接从 Scope 中拿)
    SymbolEntry* entry = lookupSymbol(name);

    if (!entry) {
        std::cerr << "[IR-Error] Symbol not found in table: " << name << std::endl;
        return;
    }

    // ================== [修改点 1: 手动构造 Operand] ==================
    // 原代码: Operand* varOp = getVar(name);
    // 修改原因: 此时变量尚未激活，调用 getVar 会失败或错误地返回外层变量。
    Operand* varOp = new Operand(entry);

    // 手动执行命名修饰逻辑 (必须与 getVar 中的逻辑保持完全一致)
    bool isGlobal = (entry->scope == 1);
    bool isStaticType = (entry->type == SymbolType::StaticInt ||
                         entry->type == SymbolType::StaticIntArray);

    if (!isGlobal && !isStaticType) {
        // 局部变量：加 scope 后缀，例如 "a_2"
        varOp->name = entry->name + "_" + std::to_string(entry->scope);
    } else if (isStaticType) {
        // 静态变量：优先用 Label
        if (!entry->label.empty()) varOp->name = entry->label;
        else varOp->name = entry->name + "_static_" + std::to_string(entry->scope);
    }
    // 全局变量保持原名 (在 Operand 构造时已赋值)
    // ==============================================================

    // ================== [保持原有逻辑] 同步局部变量到 CodeGen 表 ==================
    if (currentCodeGenInfo && entry->scope != 1 && !isStaticType) {
        std::string uniqueName = varOp->name; // 使用刚才生成的 unique name

        // 检查是否存在 (防止重复添加)
        if (!currentCodeGenInfo->symbolMap.count(uniqueName)) {
            CodeGenSymbolEntry cgEntry;
            cgEntry.name = uniqueName;
            cgEntry.offset = entry->offset + 12; // BaseOffset + 12
            cgEntry.size = entry->getByteSize();
            cgEntry.isParam = false;
            cgEntry.isTemp = false;

            currentCodeGenInfo->symbolMap[uniqueName] = cgEntry;
        }
    }
    // =======================================================================

    // 查找 InitVal 节点
    std::shared_ptr<TreeNode> initValNode = nullptr;
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::INIT_VAL) {
            initValNode = child;
            break;
        }
    }

    // 全局/静态变量处理
    if (isGlobal || isStaticType) {
        // ================== [修改点 2: 激活全局符号] ==================
        // 即使不需要生成运行时代码，也必须激活符号，以便后续代码可以使用它
        // (虽然构造函数可能已激活全局变量，但静态局部变量必须在这里激活)
        activeSymbols.insert(entry);
        return;
    }

    // === 局部变量初始化逻辑 (生成运行时指令) ===

    if (entry->isArray()) {
        int arraySize = entry->arraySize;
        int elementIndex = 0;
        if (initValNode) {
            for (const auto& child : initValNode->children) {
                if (child->nodeType == NodeType::EXP) {
                    // 注意：这里的 visitExp 可能会调用 getVar。
                    // 由于当前 entry 还没加入 activeSymbols，getVar 会正确地查找外层同名变量 (如果有)。
                    Operand* valOp = visitExp(child);
                    Operand* offsetOp = newImm(elementIndex * 4);
                    emit(IROp::STORE, valOp, varOp, offsetOp);
                    elementIndex++;
                }
            }
        }
        // 零填充
        if (initValNode) {
            Operand* zeroOp = newImm(0);
            while (elementIndex < arraySize) {
                Operand* offsetOp = newImm(elementIndex * 4);
                emit(IROp::STORE, zeroOp, varOp, offsetOp);
                elementIndex++;
            }
        }
    } else {
        // 标量初始化
        if (initValNode && !initValNode->children.empty()) {
            std::shared_ptr<TreeNode> childNode = initValNode->children[0];
            // 简单处理嵌套的花括号
            if (childNode->nodeType != NodeType::EXP && initValNode->children.size() > 0) {
                for(auto c : initValNode->children) {
                    if(c->nodeType == NodeType::EXP) {
                        childNode = c;
                        break;
                    }
                }
            }

            if (childNode->nodeType == NodeType::EXP) {
                Operand* valOp = visitExp(childNode);
                if (varOp && valOp) emit(IROp::ASSIGN, varOp, valOp);
            }
        }
    }

    // ================== [修改点 3: 初始化完成后激活符号] ==================
    // 核心修改：所有初始化指令生成完毕后，才允许该符号被后续代码访问
    // 从而实现了 "int a = a + 1" 中右边的 a 指向外层变量的逻辑
    activeSymbols.insert(entry);
}

void IRGenerator::visitConstDef(const std::shared_ptr<TreeNode>& node) {
    // CONST_DEF -> Ident [ '[' ConstExp ']' ] '=' ConstInitVal

    // 1. 获取变量名并查找符号表
    std::string name = extractIdent(node);
    SymbolEntry* entry = lookupSymbol(name);

    if (!entry) return;

    // ================== [修改点 1: 手动构造 Operand] ==================
    // 原代码: Operand* varOp = getVar(name);
    // 同样避免过早调用 getVar 导致失败
    Operand* varOp = new Operand(entry);

    bool isGlobal = (entry->scope == 1);
    bool isStaticType = (entry->type == SymbolType::StaticInt ||
                         entry->type == SymbolType::StaticIntArray);

    // 手动命名修饰
    if (!isGlobal && !isStaticType) {
        varOp->name = entry->name + "_" + std::to_string(entry->scope);
    } else if (isStaticType) {
        if (!entry->label.empty()) varOp->name = entry->label;
        else varOp->name = entry->name + "_static_" + std::to_string(entry->scope);
    }
    // ==============================================================

    // 2. 同步局部常量到 MIPS 代码生成符号表
    if (currentCodeGenInfo && entry->scope != 1 && !isStaticType) {
        std::string uniqueName = varOp->name; // 使用一致的唯一名

        // 检查防止重复
        if (!currentCodeGenInfo->symbolMap.count(uniqueName)) {
            CodeGenSymbolEntry cgEntry;
            cgEntry.name = uniqueName;
            cgEntry.offset = entry->offset + 12;
            cgEntry.size = entry->getByteSize();
            cgEntry.isParam = false;
            cgEntry.isTemp = false;

            currentCodeGenInfo->symbolMap[uniqueName] = cgEntry;
        }
    }

    // 3. 全局常量不需要生成 IR 指令
    if (isGlobal || isStaticType) {
        // ================== [修改点 2: 激活全局符号] ==================
        activeSymbols.insert(entry);
        return;
    }

    // 4. 查找 ConstInitVal 节点
    std::shared_ptr<TreeNode> constInitValNode = nullptr;
    for (const auto& child : node->children) {
        if (child->nodeType == NodeType::CONST_INIT_VAL) {
            constInitValNode = child;
            break;
        }
    }

    if (!constInitValNode) return;

    // === 局部常量初始化逻辑 (运行时执行) ===

    // 情况 A: 数组常量
    if (entry->isArray()) {
        int arraySize = entry->arraySize;
        int elementIndex = 0;

        // 遍历初始化列表
        for (const auto& child : constInitValNode->children) {
            if (child->nodeType == NodeType::CONST_EXP || child->nodeType == NodeType::EXP) {
                // 计算初始化值
                Operand* valOp = visitExp(child);
                Operand* offsetOp = newImm(elementIndex * 4);

                emit(IROp::STORE, valOp, varOp, offsetOp);
                elementIndex++;
            }
        }

        // 零填充
        if (elementIndex < arraySize) {
            Operand* zeroOp = newImm(0);
            while (elementIndex < arraySize) {
                Operand* offsetOp = newImm(elementIndex * 4);
                emit(IROp::STORE, zeroOp, varOp, offsetOp);
                elementIndex++;
            }
        }
    }
        // 情况 B: 标量常量 (const int a = 5;)
    else {
        std::shared_ptr<TreeNode> valNode = nullptr;
        for(auto c : constInitValNode->children) {
            if(c->nodeType == NodeType::CONST_EXP || c->nodeType == NodeType::EXP) {
                valNode = c;
                break;
            }
        }

        if (valNode) {
            Operand* valOp = visitExp(valNode);
            if (varOp && valOp) {
                emit(IROp::ASSIGN, varOp, valOp);
            }
        }
    }

    // ================== [修改点 3: 初始化完成后激活符号] ==================
    activeSymbols.insert(entry);
}

void IRGenerator::visitStmt(const std::shared_ptr<TreeNode>& node) {
    if (node->children.empty()) return;
    auto first = node->children[0];

    // 1. Return
    if (first->value == "return") {
        Operand* retVal = nullptr;
        for (const auto& c : node->children) {
            if (c->nodeType == NodeType::EXP) {
                retVal = visitExp(c);
                break;
            }
        }
        emit(IROp::RET, retVal);
    }
    // 2. Block
    else if (first->nodeType == NodeType::BLOCK) {
        visitBlock(first, false);
    }
    // 3. If
    // ... Inside visitStmt ...
    else if (first->value == "if") {
        std::cout << "\n[IR-Debug] >>> Entering IF Stmt at Line " << node->line << std::endl;

        std::shared_ptr<TreeNode> condNode = nullptr;
        std::shared_ptr<TreeNode> trueStmt = nullptr; // 对应文法中的第一个 Stmt
        std::shared_ptr<TreeNode> falseStmt = nullptr; // 对应 else 后的 Stmt
        bool foundElse = false;

        // 1. 严格按照文法扫描子节点
        // 文法: 'if' '(' Cond ')' Stmt [ 'else' Stmt ]
        for (const auto& c : node->children) {
            if (c->nodeType == NodeType::COND) {
                condNode = c;
                std::cout << "[IR-Debug] Found Condition" << std::endl;
            }
            else if (c->nodeType == NodeType::TERMINAL && c->value == "else") {
                // 关键修正：依靠 "else" 关键字标记分界线
                foundElse = true;
                std::cout << "[IR-Debug] Found 'else' keyword" << std::endl;
            }
            else if (c->nodeType == NodeType::STMT) {
                // 如果还没有遇到 else，这个 STMT 就是 True 分支
                if (!foundElse) {
                    trueStmt = c;
                    std::cout << "[IR-Debug] Found True-Branch Stmt" << std::endl;
                }
                    // 如果已经遇到了 else，这个 STMT 就是 False 分支
                else {
                    falseStmt = c;
                    std::cout << "[IR-Debug] Found False-Branch Stmt (Else block)" << std::endl;
                }
            }
        }

        // 2. 检查必要组件 (Cond必须存在)
        if (!condNode) {
            std::cerr << "[IR-Error] CRITICAL: IF statement missing condition at line " << node->line << std::endl;
            return;
        }

        Operand* L_true = newLabel();
        Operand* L_false = newLabel(); // 如果有else，这是else的入口；如果没有，这是跳出的出口
        Operand* L_next = newLabel();  // 整个if语句的结束

        // 3. 生成控制流代码
        if (falseStmt) {
            // === 情况 1: 带有 Else 分支 ===
            // if (Cond) Stmt1 else Stmt2
            std::cout << "[IR-Debug] Generating IF-ELSE structure..." << std::endl;

            // Cond: 真 -> L_true, 假 -> L_false
            std::cout << "[IR-Debug]   -> Calling visitCond (Target: True=" << L_true->name
                      << ", False=" << L_false->name << ")..." << std::endl;
            visitCond(condNode, L_true, L_false);
            std::cout << "[IR-Debug]   -> visitCond returned." << std::endl;

            // True 分支
            emitLabel(L_true);
            if (trueStmt) {
                std::cout << "[IR-Debug]   -> Visiting True Branch Stmt..." << std::endl;
                visitStmt(trueStmt);
                std::cout << "[IR-Debug]   -> True Branch Stmt visited." << std::endl;
            }else {
                std::cout << "[IR-Debug]   -> True Branch is empty." << std::endl;
            }
            // 执行完 True 分支后，必须跳转到 Next，跳过 Else 分支
            emit(IROp::JUMP, L_next);

            // False (Else) 分支
            emitLabel(L_false);
            std::cout << "[IR-Debug]   -> Visiting False (Else) Branch Stmt..." << std::endl;
            visitStmt(falseStmt);
            std::cout << "[IR-Debug]   -> False Branch Stmt visited." << std::endl;

            // 结束
            emitLabel(L_next);
            std::cout << "[IR-Debug]   -> Emitted L_next (" << L_next->name << ")" << std::endl;

        } else {
            // === 情况 2: 没有 Else 分支 ===
            // if (Cond) Stmt1
            // 【注意】你的死锁目前发生在这里！
            std::cout << "[IR-Debug] Generating IF (No Else) structure..." << std::endl;

            // Cond: 真 -> L_true, 假 -> L_next (直接跳出)
            std::cout << "[IR-Debug]   -> Calling visitCond (Target: True=" << L_true->name
                      << ", False=" << L_next->name << ")..." << std::endl;
            // 此时 L_false 标签实际上没有用到，或者可以理解为 L_false 就是 L_next
            //就卡这一句了
            visitCond(condNode, L_true, L_next);

            std::cout << "[IR-Debug]   -> visitCond returned." << std::endl;

            // True 分支
            emitLabel(L_true);
            if (trueStmt) {
                std::cout << "[IR-Debug]   -> Visiting True Branch Stmt..." << std::endl;

                visitStmt(trueStmt);

                std::cout << "[IR-Debug]   -> True Branch Stmt visited." << std::endl;
            }else {
                std::cout << "[IR-Debug]   -> True Branch is empty." << std::endl;
            }

            // 结束
            emitLabel(L_next);
            std::cout << "[IR-Debug]   -> Emitted L_next (" << L_next->name << ")" << std::endl;
        }

        std::cout << "[IR-Debug] <<< Finished IF Stmt at Line " << node->line << std::endl;
    }
    // 4. For
    else if (first->value == "for") {
        // 'for' '(' [ForStmt] ';' [Cond] ';' [ForStmt] ')' Stmt
        std::shared_ptr<TreeNode> initNode = nullptr;
        std::shared_ptr<TreeNode> condNode = nullptr;
        std::shared_ptr<TreeNode> stepNode = nullptr;
        std::shared_ptr<TreeNode> bodyNode = nullptr;

        int semicolonCount = 0; // 新增：分号计数器

        for (const auto& c : node->children) {
            // 1. 也是最重要的：检测分号来确定当前处于 for 循环的哪个部分
            if (c->nodeType == NodeType::TERMINAL && c->value == ";") {
                semicolonCount++;
                continue;
            }

            // 2. 根据分号数量归类节点
            if (c->nodeType == NodeType::FOR_STMT) {
                if (semicolonCount == 0) {
                    // 第一个分号之前出现的 FOR_STMT 是初始化
                    initNode = c;
                } else if (semicolonCount == 2) {
                    // 第二个分号之后出现的 FOR_STMT 是步进
                    stepNode = c;
                }
            }
            else if (c->nodeType == NodeType::COND) {
                // COND 应该只出现在两个分号中间
                condNode = c;
            }
            else if (c->nodeType == NodeType::STMT) {
                // 循环体
                bodyNode = c;
            }
        }

        Operand* L_start = newLabel();
        Operand* L_cond = newLabel(); // Loop check
        Operand* L_body = newLabel();
        Operand* L_step = newLabel();
        Operand* L_end = newLabel();

        // Init
        if (initNode) visitForStmtNode(initNode);

        emitLabel(L_start); // 循环开始点

        // Cond
        if (condNode) {
            visitCond(condNode, L_body, L_end);
        } else {
            // 无条件，直接跳入 Body
            emit(IROp::JUMP, L_body);
        }

        emitLabel(L_body);

        breakStack.push(L_end);
        continueStack.push(L_step);

        if (bodyNode) visitStmt(bodyNode);

        breakStack.pop();
        continueStack.pop();

        emitLabel(L_step);
        // Step
        if (stepNode) visitForStmtNode(stepNode);

        // Jump back to check condition (or start if no cond, but cond logic handles flow)
        if (condNode) emit(IROp::JUMP, L_start);
        else emit(IROp::JUMP, L_body); // 无限循环

        emitLabel(L_end);
    }
    // 5. Break/Continue
    else if (first->value == "break") {
        if (!breakStack.empty()) emit(IROp::JUMP, breakStack.top());
    }
    else if (first->value == "continue") {
        if (!continueStack.empty()) emit(IROp::JUMP, continueStack.top());
    }
    // 6. Printf
    else if (first->value == "printf") {
        // printf(str, exp...)
        std::string rawStr;
        std::vector<Operand*> args;
        for (const auto& c : node->children) {
            if (c->nodeType == NodeType::TERMINAL && c->value.find('"') != std::string::npos) {
                rawStr = c->value;
            } else if (c->nodeType == NodeType::EXP) {
                args.push_back(visitExp(c));
            }
        }

        // 去引号
        if (rawStr.size() >= 2) rawStr = rawStr.substr(1, rawStr.size() - 2);

        // 解析格式串并生成指令
        size_t argIdx = 0;
        std::string buffer;
        for (size_t i = 0; i < rawStr.length(); ++i) {
            if (rawStr[i] == '%' && i+1 < rawStr.length() && rawStr[i+1] == 'd') {
                if (!buffer.empty()) {
                    std::string lbl = addStringConstant(buffer);
                    // 【修正】result=nullptr, arg1=Operand
                    emit(IROp::PRINTSTR, nullptr, new Operand(lbl, OperandType::LABEL));
                    buffer.clear();
                }
                if (argIdx < args.size()) {
                    // 【修正】result=nullptr, arg1=Operand
                    emit(IROp::PRINTINT, nullptr, args[argIdx++]);
                }
                i++;
            } else if (rawStr[i] == '\\' && i+1 < rawStr.length() && rawStr[i+1] == 'n') {
                buffer += "\n";
                i++;
            } else {
                buffer += rawStr[i];
            }
        }
        if (!buffer.empty()) {
            std::string lbl = addStringConstant(buffer);
            // 【修正】result=nullptr, arg1=Operand
            emit(IROp::PRINTSTR, nullptr, new Operand(lbl, OperandType::LABEL));
        }
    }
    // 7. Assignment (LVal = Exp)
    else if (first->nodeType == NodeType::LVAL && node->children.size() > 1 && node->children[1]->value == "=") {
        // LVal 作为左值，我们需要它的地址（如果是数组）或符号（如果是变量）
        // 对于普通变量 x=1，visitLVal返回 VAR(x)，直接 ASSIGN
        // 对于数组 a[i]=1，visitLVal返回 TEMP(address)，使用 STORE

        Operand* lhs = visitLVal(first, true); // true = 请求地址/左值语义
        Operand* rhs = visitExp(node->children[2]);

        if (lhs->type == OperandType::TEMP) {
            // 返回的是地址，说明是数组元素赋值
            // STORE rhs -> *lhs
            // Target: Memory[lhs + 0] = rhs
            emit(IROp::STORE, rhs, lhs, newImm(0));
        } else {
            // 返回的是变量符号
            emit(IROp::ASSIGN, lhs, rhs);
        }
    }
        // 8. Exp Stmt
    else if (first->nodeType == NodeType::EXP) {
        visitExp(first);
    }
}

// 处理 ForStmt 节点： LVal '=' Exp { ',' LVal '=' Exp }
void IRGenerator::visitForStmtNode(const std::shared_ptr<TreeNode>& node) {
    // 你的 Parser.cpp 是这样构建的:
    // addChild(LVal)
    // addChild(=)
    // addChild(Exp)
    // [addChild(,), addChild(LVal)...]

    for (size_t i = 0; i < node->children.size(); ) {
        if (node->children[i]->nodeType == NodeType::LVAL) {
            auto lvalNode = node->children[i];
            // i+1 是 "=", i+2 是 Exp
            if (i + 2 < node->children.size() && node->children[i+2]->nodeType == NodeType::EXP) {
                auto expNode = node->children[i+2];

                Operand* lhs = visitLVal(lvalNode, true);
                Operand* rhs = visitExp(expNode);

                if (lhs->type == OperandType::TEMP) {
                    emit(IROp::STORE, rhs, lhs, newImm(0));
                } else {
                    emit(IROp::ASSIGN, lhs, rhs);
                }
            }
            i += 3; // 跳过一组
        } else {
            i++; // 跳过逗号
        }
    }
}

// === 表达式 ===

Operand* IRGenerator::visitExp(const std::shared_ptr<TreeNode>& node) {
    if (!node->children.empty()) return visitAddExp(node->children[0]);
    return nullptr;
}

Operand* IRGenerator::visitAddExp(const std::shared_ptr<TreeNode>& node) {
    if (node->children.size() == 1) return visitMulExp(node->children[0]);

    Operand* left = visitAddExp(node->children[0]); // 递归左侧
    Operand* right = visitMulExp(node->children[2]); // 右侧
    std::string op = node->children[1]->value;

    Operand* res = newTemp();
    IROp irOp = (op == "+") ? IROp::ADD : IROp::SUB;
    emit(irOp, res, left, right);
    return res;
}

Operand* IRGenerator::visitMulExp(const std::shared_ptr<TreeNode>& node) {
    if (node->children.size() == 1) return visitUnaryExp(node->children[0]);

    Operand* left = visitMulExp(node->children[0]);
    Operand* right = visitUnaryExp(node->children[2]);
    std::string op = node->children[1]->value;

    Operand* res = newTemp();
    IROp irOp = IROp::MUL;
    if (op == "/") irOp = IROp::DIV;
    else if (op == "%") irOp = IROp::MOD;

    emit(irOp, res, left, right);
    return res;
}

Operand* IRGenerator::visitUnaryExp(const std::shared_ptr<TreeNode>& node) {
    auto first = node->children[0];

    // UnaryOp UnaryExp
    if (first->nodeType == NodeType::UNARY_OP) {
        std::string op = first->value;
        Operand* src = visitUnaryExp(node->children[1]);
        if (op == "+") return src;

        Operand* res = newTemp();
        if (op == "-") emit(IROp::NEG, res, src);
        else if (op == "!") emit(IROp::NOT, res, src);
        return res;
    }
        // Func Call
    else if (first->nodeType == NodeType::TERMINAL && node->children.size() > 1 && node->children[1]->value == "(") {
        std::string funcName = first->value;
        if (funcName == "getint") {
            Operand* res = newTemp();
            emit(IROp::GETINT, res);
            return res;
        }

        // Params
        if (node->children.size() > 2 && node->children[2]->nodeType == NodeType::FUNC_R_PARAMS) {
            auto rparams = node->children[2];
            // Parser.cpp 中 FuncRParams -> Exp { , Exp }
            for (const auto& c : rparams->children) {
                if (c->nodeType == NodeType::EXP) {
                    // 【修正】将参数放在 arg1 (第3个参数)，result 设为 nullptr
                    // 这样 MIPSGenerator 可以安全地访问 instr->arg1
                    emit(IROp::PARAM, nullptr, visitExp(c));
                }
            }
        }

        Operand* ret = newTemp();
        emit(IROp::CALL, ret, new Operand(funcName, OperandType::LABEL));
        return ret;
    }
    // PrimaryExp
    return visitPrimaryExp(first);
}

Operand* IRGenerator::visitPrimaryExp(const std::shared_ptr<TreeNode>& node) {
    auto first = node->children[0];
    if (first->nodeType == NodeType::EXP) return visitExp(first);
    else if (first->nodeType == NodeType::NUMBER) return newImm(std::stoi(first->children[0]->value));
    else if (first->nodeType == NodeType::LVAL) return visitLVal(first, false); // 右值
    // '(' Exp ')' handled by first check if child is EXP?
    // Parser: PrimaryExp -> '(' Exp ')' | LVal | Number
    // If it's '(', then children[0] is terminal '(', children[1] is Exp
    if (first->value == "(") return visitExp(node->children[1]);
    return nullptr;
}

// 关键逻辑：isAddress
// 如果 isAddress=true (左值)，返回代表地址的 TEMP (如果是数组) 或 VAR (如果是标量)
// 如果 isAddress=false (右值)，返回代表值的 TEMP/VAR
Operand* IRGenerator::visitLVal(const std::shared_ptr<TreeNode>& node, bool isAddress) {
    std::string name = extractIdent(node);
    Operand* symOp = getVar(name); // VAR

    // 检查是否有下标
    std::shared_ptr<TreeNode> indexExp;
    for (const auto& c : node->children) {
        if (c->nodeType == NodeType::EXP) {
            indexExp = c; break;
        }
    }

    if (indexExp) {
        // 数组访问: name[index]
        Operand* idx = visitExp(indexExp);
        Operand* offset = newTemp();
        emit(IROp::MUL, offset, idx, newImm(4)); // offset = i * 4

        // 计算地址: addr = base_addr + offset
        // 注意：SysY中数组参数是指针，局部数组是栈地址。
        // 为了统一，我们使用 GET_ADDR 指令。
        // 如果 symOp 是局部数组，GET_ADDR 会加上 offset。
        // 如果 symOp 是指针（参数），GET_ADDR 会做加法。
        Operand* addr = newTemp();
        emit(IROp::GET_ADDR, addr, symOp, offset);

        if (isAddress) {
            return addr; // 返回地址
        } else {
            Operand* val = newTemp();
            emit(IROp::LOAD, val, addr, newImm(0)); // val = *addr
            return val;
        }
    } else {
        // 标量或数组全名(指针)
        // 如果是标量，直接返回 symbol
        // 如果是数组名(传参)，根据 SysY 语义，它是地址。
        if (symOp->symbol && symOp->symbol->isArray()) {
            // 引用数组名 = 数组首地址
            // 生成取地址指令
            Operand* addr = newTemp();
            emit(IROp::GET_ADDR, addr, symOp, newImm(0));
            return addr;
        }
        return symOp;
    }
}

// === 短路求值 ===

void IRGenerator::visitCond(const std::shared_ptr<TreeNode>& node, Operand* trueLabel, Operand* falseLabel) {
    visitLOrExp(node->children[0], trueLabel, falseLabel);
}

void IRGenerator::visitLOrExp(const std::shared_ptr<TreeNode>& node, Operand* trueLabel, Operand* falseLabel) {
    // LOrExp -> LAndExp { || LAndExp }
    if (node->children.size() == 1) {
        visitLAndExp(node->children[0], trueLabel, falseLabel);
    } else {
        // 有 || 运算
        // children[0]: LOrExp (递归)
        // children[1]: ||
        // children[2]: LAndExp (下沉)

        Operand* checkRightLabel = newLabel(); // 如果左边为假，需要检查右边

        // 1. 递归处理左边 (LOrExp)
        // 逻辑：如果左边为真，直接去 trueLabel (短路)；如果为假，去 checkRightLabel
        visitLOrExp(node->children[0], trueLabel, checkRightLabel);

        emitLabel(checkRightLabel);

        // 2. 处理右边 (LAndExp)
        // 逻辑：右边是最后决定因素。真->trueLabel, 假->falseLabel
        visitLAndExp(node->children[2], trueLabel, falseLabel);
    }
}

void IRGenerator::visitLAndExp(const std::shared_ptr<TreeNode>& node, Operand* trueLabel, Operand* falseLabel) {
    // LAndExp -> EqExp | LAndExp '&&' EqExp

    if (node->children.size() == 1) {
        // 只有一项，下沉到 EqExp
        // 这里需要将 EqExp 的数值结果转换为控制流跳转
        Operand* val = visitEqExp(std::static_pointer_cast<TreeNode>(node->children[0]));
        // 【修正】将 falseLabel 放在 result 位置 (第2个参数)
        emit(IROp::BEQZ, falseLabel, val);
        // 如果非0，自然fall through到后续代码（或者显式跳True，取决于你的控制流设计）
        // 为了保险，通常这里应该处理 True 的情况。
        // 但在短路逻辑中，如果是链条的最后一环，外层会处理 True 的流向。
        // 如果需要显式跳转：emit(IROp::JUMP, nullptr, nullptr, trueLabel);
        // 【修正】将 trueLabel 放在 result 位置 (第2个参数)
        // 这样与 visitStmt 中的 emit(IROp::JUMP, L_next) 保持一致
        emit(IROp::JUMP, trueLabel);
    } else {
        // 有 && 运算
        // children[0]: LAndExp
        // children[1]: &&
        // children[2]: EqExp

        Operand* checkRightLabel = newLabel();

        // 1. 递归处理左边 (LAndExp)
        // 逻辑：如果左边为假，直接去 falseLabel (短路)；如果为真，去 checkRightLabel
        visitLAndExp(std::static_pointer_cast<TreeNode>(node->children[0]), checkRightLabel, falseLabel);

        emitLabel(checkRightLabel);

        // 2. 处理右边 (EqExp)
        Operand* val = visitEqExp(std::static_pointer_cast<TreeNode>(node->children[2]));
        // 【修正】同上
        emit(IROp::BEQZ, falseLabel, val);
        emit(IROp::JUMP, trueLabel);
    }
}

// 补充 EqExp 和 RelExp
Operand* IRGenerator::visitEqExp(const std::shared_ptr<TreeNode>& node) {
    // EqExp -> RelExp | EqExp ('==' | '!=') RelExp

    // 如果只有一个子节点，转交给 RelExp
    if (node->children.size() == 1) {
        return visitRelExp(std::static_pointer_cast<TreeNode>(node->children[0]));
    }

    // 递归计算左操作数 (现在可以接收返回值了！)
    Operand* left = visitEqExp(std::static_pointer_cast<TreeNode>(node->children[0]));

    // 计算右操作数
    Operand* right = visitRelExp(std::static_pointer_cast<TreeNode>(node->children[2]));

    // 生成比较指令
    std::string op = node->children[1]->value;
    Operand* result = newTemp();
    IROp irop = (op == "==") ? IROp::EQ : IROp::NEQ;

    emit(irop, result, left, right); // result = (left op right)
    return result;
}

// 鉴于篇幅，这里提供一个处理 Cond 的通用简化方案：
// 将所有比较运算生成为值 (0/1)，然后在逻辑运算层做 BEQZ。
// 仅在 LOr/LAnd 处打断流。

Operand* IRGenerator::visitRelExp(const std::shared_ptr<TreeNode>& node) {
    // RelExp -> AddExp | RelExp ('<' | '>' | '<=' | '>=') AddExp

    // 如果只有一个子节点，直接转交给 AddExp 计算数值
    if (node->children.size() == 1) {
        return visitAddExp(std::static_pointer_cast<TreeNode>(node->children[0]));
    }

    // 递归计算左操作数
    Operand* left = visitRelExp(std::static_pointer_cast<TreeNode>(node->children[0]));

    // 计算右操作数
    Operand* right = visitAddExp(std::static_pointer_cast<TreeNode>(node->children[2]));

    // 生成比较指令
    std::string op = node->children[1]->value;
    Operand* result = newTemp();
    IROp irop;

    if (op == "<") irop = IROp::LT;
    else if (op == ">") irop = IROp::GT;
    else if (op == "<=") irop = IROp::LE;
    else if (op == ">=") irop = IROp::GE;

    emit(irop, result, left, right); // result = (left op right)
    return result; // 返回存放结果(0或1)的临时变量
}

void IRGenerator::printIR(const std::string& filename) {
    std::ofstream fout(filename);

    // 1. 检查文件是否成功打开
    if (!fout.is_open()) {
        std::cerr << "IRGenerator Error: Could not open file " << filename << " for writing." << std::endl;
        return;
    }

    // 2. (可选) 先打印字符串常量表 (类似于汇编的 .data 段)
    // 这对于调试 printf 语句非常有帮助
    if (!stringConstants.empty()) {
        fout << "#String Constants (.data)\n";
        for (const auto& kv : stringConstants) {
            // kv.first 是标签 (如 str_0), kv.second 是内容 (如 "Hello\n")
            // 这里简单处理一下转义字符，以便肉眼可读
            std::string content = kv.second;
            std::string escaped;
            for (char c : content) {
                if (c == '\n') escaped += "\\n";
                else escaped += c;
            }
            fout << kv.first << ": \"" << escaped << "\"\n";
        }
        fout << "\n#Instructions (.text)\n";
    }

    // 3. 遍历指令列表并打印
    for (auto* instr : instructions) {
        if (instr) {
            // 调用 IRInstruction 定义好的 toString 方法
            fout << instr->toString() << "\n";
        }
    }

    fout.close();
    std::cout << "中间代码已成功输出到: " << filename << std::endl;
}

// 【新增】实现 dump 函数
void IRGenerator::dumpMipsCodeGenTable(const std::string& filename) {
    std::ofstream fout(filename);
    if (!fout.is_open()) {
        std::cerr << "无法打开调试文件: " << filename << std::endl;
        return;
    }
    // =========================================================
    // 第一部分：全局变量与静态变量 (.data 段布局)
    // =========================================================
    fout << "=== 全局与静态变量 (.data 段) ===\n";
    fout << "说明: 这些变量存储在静态数据区，不占用函数栈帧\n";

    // 1. 从符号表中获取所有符号
    auto allSymbols = symbolTable.getAllSymbols();
    std::vector<SymbolEntry> dataSegmentSymbols;

    // 2. 筛选出全局变量和静态变量
    for (const auto& sym : allSymbols) {
        // 排除函数定义
        if (sym.isFunction()) continue;

        bool isGlobal = (sym.scope == 1);
        bool isStatic = (sym.type == SymbolType::StaticInt ||
                         sym.type == SymbolType::StaticIntArray);

        if (isGlobal || isStatic) {
            dataSegmentSymbols.push_back(sym);
        }
    }

    // 3. 排序：先按作用域(Global first)，再按名称
    std::sort(dataSegmentSymbols.begin(), dataSegmentSymbols.end(),
              [](const SymbolEntry& a, const SymbolEntry& b) {
                  if (a.scope != b.scope) return a.scope < b.scope;
                  return a.name < b.name;
              });

    // 4. 输出表格
    if (dataSegmentSymbols.empty()) {
        fout << "  (无全局或静态变量)\n";
    } else {
        fout << "    " << std::left << std::setw(20) << "Name"
             << std::left << std::setw(25) << "Label (.data)"
             << std::left << std::setw(10) << "Size"
             << std::left << std::setw(15) << "Type"
             << "Scope" << "\n";
        fout << "    " << std::string(75, '-') << "\n";

        for (const auto& sym : dataSegmentSymbols) {
            std::string labelStr = sym.label.empty() ? sym.name : sym.label;
            std::string typeStr = SymbolTable::getTypeString(sym.type);
            std::string scopeStr = (sym.scope == 1) ? "Global" : ("Static (Scope " + std::to_string(sym.scope) + ")");

            fout << "    " << std::left << std::setw(20) << sym.name
                 << std::left << std::setw(25) << labelStr
                 << std::left << std::setw(10) << sym.size
                 << std::left << std::setw(15) << typeStr
                 << scopeStr << "\n";
        }
    }
    fout << "\n\n";

    fout << "=== MIPS 代码生成符号表 (栈帧布局) ===\n";
    fout << "布局说明: Offset 是相对于 $fp 的偏移量\n";
    fout << "预期顺序: FP/RA(0-8) -> 参数 -> 局部变量 -> 临时变量\n\n";

    for (const auto& [funcName, info] : mipsCodeGenTable) {
        fout << "Function: " << funcName << "\n";
        fout << "  Total Frame Size: " << info.frameSize << " bytes\n";

        // 为了按 Offset 排序显示，我们需要将 map 转为 vector
        std::vector<CodeGenSymbolEntry> entries;
        for (const auto& [symName, entry] : info.symbolMap) {
            entries.push_back(entry);
        }

        // 按 offset 从小到大排序
        std::sort(entries.begin(), entries.end(),
                  [](const CodeGenSymbolEntry& a, const CodeGenSymbolEntry& b) {
                      return a.offset < b.offset;
                  });

        fout << "  Symbol Layout:\n";
        fout << "    " << std::left << std::setw(15) << "Name"
             << std::left << std::setw(10) << "Offset"
             << std::left << std::setw(10) << "Size"
             << "Type" << "\n";
        fout << "    " << std::string(45, '-') << "\n";

        // 模拟打印 FP 和 RA (便于理解)
        fout << "    " << std::left << std::setw(15) << "$fp (old)"
             << std::left << std::setw(10) << "0" << "4         System\n";
        fout << "    " << std::left << std::setw(15) << "$ra"
             << std::left << std::setw(10) << "4" << "4         System\n";

        for (const auto& entry : entries) {
            std::string typeStr = "Local";
            if (entry.isParam) typeStr = "Param";
            else if (entry.isTemp) typeStr = "Temp";

            fout << "    " << std::left << std::setw(15) << entry.name
                 << std::left << std::setw(10) << entry.offset
                 << std::left << std::setw(10) << entry.size
                 << typeStr << "\n";
        }
        fout << "\n" << std::string(50, '=') << "\n\n";
    }

    fout.close();
    std::cout << "MIPS 符号表调试信息已输出至: " << filename << std::endl;
}