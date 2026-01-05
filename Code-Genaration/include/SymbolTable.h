#ifndef COMPILER_SYMBOLTABLE_H
#define COMPILER_SYMBOLTABLE_H

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>
#include <stdexcept>

// 【新增】用于文件输出和格式化的头文件
#include <fstream>
#include <iostream>
#include <iomanip> // 用于 std::setw
#include <sstream> // 用于字符串构建

#include <algorithm>

// 符号类型
enum class SymbolType {
    ConstInt,           // int型常量
    Int,                // int型变量  
    VoidFunc,           // void型函数
    IntFunc,            // int型函数
    ConstIntArray,      // int型常量数组
    IntArray,           // int型变量数组
    StaticInt,          // int型静态变量
    StaticIntArray      // int型静态变量数组
};

// 函数参数信息
//向函数中传入的参数的信息
struct ParamInfo {
    SymbolType type;//参数类型
    bool isArray;//是否是数组类型
    std::string name;//参数名称
};

// 符号表项
//符号表项，表示一个符号（变量、常量、函数）的完整信息
struct SymbolEntry {
    std::string name;           // 符号名称（如变量名、函数名）
    SymbolType type;            // 符号类型（来自SymbolType枚举）
    int scope;                  // 作用域序号
    int line;                   // 声明行号（在源文件中的位置）
    bool isParam;               // 是否是函数参数

    // === 代码生成新增字段 ===
    int offset;                 // 相对偏移量 (局部变量相对于FP，全局变量通常为0)
    int size;                   // 占用字节大小 (int=4, array=4*length)
    std::string label;          // 汇编标签名 (主要用于全局变量)
    int stackFrameSize;         // (仅函数有效) 栈帧总大小
    // ======================

    std::vector<ParamInfo> paramTypes; // 函数参数类型列表（仅函数使用）
    int arraySize;              // 数组大小（如果不是数组则为-1）
    int value;                  // 常量值（如果是常量）

    std::vector<int> arrayInitValues;  // 数组初始化值

    //构造函数，初始化符号表项
    SymbolEntry(const std::string& n, SymbolType t, int s, int l, bool ip = false)
            : name(n), type(t), scope(s), line(l), isParam(ip),
              offset(0), size(4), stackFrameSize(0), // 初始化新增字段
              arraySize(-1), value(0) {

        // 如果是全局变量，标签通常就是名字
        // 注意：在生成MIPS时可能需要为全局变量加前缀以避免与指令重名
        if (scope == 1) {
            label = name;
        }
    }

    // 检查是否是常量（ConstInt或ConstIntArray）
    bool isConstant() const {
        return type == SymbolType::ConstInt || type == SymbolType::ConstIntArray;
    }

    // 检查是否是数组（三种数组类型之一）
    bool isArray() const {
        return type == SymbolType::ConstIntArray || type == SymbolType::IntArray ||
               type == SymbolType::StaticIntArray;
    }

    // 检查是否是函数（IntFunc或VoidFunc）
    bool isFunction() const {
        return type == SymbolType::IntFunc || type == SymbolType::VoidFunc;
    }

    // 获取数组元素值的方法
    int getArrayElementValue(int index) const {
        if (index >= 0 && index < arrayInitValues.size()) {
            return arrayInitValues[index];
        } else if (index >= 0 && index < arraySize) {
            return 0;  // 未初始化元素默认为0
        }
        return 0;  // 越界返回0
    }

    // 获取类型占用的字节数
    int getByteSize() const {
        // 如果是函数参数中的数组（如 int a[]），它实际上是一个指针，占4字节
        if (isParam && isArray()) {
            return 4;
        }
        // 如果是普通数组定义
        if (isArray()) {
            return std::max(1, arraySize) * 4;
        }
        // 普通 int
        return 4;
    }
};

// 作用域
class Scope {
private:
    std::unordered_map<std::string, SymbolEntry> symbols;
    //众所周知，一个作用域中有很多符号项
    //string就是这些符号项的一系列名称
    //SymbolEntry就是这些名称对应的一系列符号表项
    //这样就建立了一个映射

    // 2. 添加新的vector类型的数据结构
    std::vector<SymbolEntry> symbols_in_order;

    int scopeId;// 当前作用域的唯一ID

public:
    // 构造函数：使用指定ID创建作用域
    Scope(int id) : scopeId(id) {}

    // 向当前作用域添加符号，返回是否成功（失败表示重定义）
    bool addSymbol(const SymbolEntry& entry) {
        if (symbols.find(entry.name) != symbols.end()) {//对于symbols.find(entry.name)，如果没找到：返回 symbols.end()（相当于"没找到"的标志）
            return false; // 重定义
        }
        // 同时添加到 map (用于查找)
        symbols.emplace(entry.name, entry);

        // 和 vector (用于保证顺序)
        symbols_in_order.push_back(entry);

        return true;
    }

    // 更新符号信息（用于回填栈大小等）
    void updateSymbol(const std::string& name, const SymbolEntry& newEntry) {
        // 更新 map
        auto mapIt = symbols.find(name);
        if (mapIt != symbols.end()) {
            mapIt->second = newEntry;
        }
        // 更新 vector
        for (auto& entry : symbols_in_order) {
            if (entry.name == name) {
                entry = newEntry;
                break;
            }
        }
    }

    // 在当前作用域中查找符号，返回指针（未找到返回nullptr）
    SymbolEntry* findSymbol(const std::string& name) {

        auto it = symbols.find(name);
        if (it != symbols.end()) {
            return &(it->second);
        }
        return nullptr;
    }

    // 获取当前作用域的所有符号（只读访问）
    // 修改getSymbols的逻辑，从新的数据结构中提取数据
    const std::vector<SymbolEntry>& getSymbols() const {
        // 返回 vector 而不是 map，以保证顺序
        return symbols_in_order;
    }

    // 获取当前作用域的ID
    int getScopeId() const { return scopeId; }
};

// 符号表
class SymbolTable {
private:
    std::vector<std::unique_ptr<Scope>> scopes;//保存程序中创建的所有作用域
    int currentScopeId;//作用域ID生成器，用于生成唯一的作用域ID，初始为1，每创建一个新作用域就+1
    std::vector<int> scopeStack; // 作用域栈，跟踪当前正在使用的作用域，后进先出，模拟作用域的进入和退出

    // 【新增】按作用域ID直接访问的映射表
    std::unordered_map<int, Scope*> scopeById;



public:
    SymbolTable() : currentScopeId(1) {//将1的ID赋值给符号表
        // 创建全局作用域
        enterScope();//下一个可用的作用域ID为2
    }

    // 进入新作用域
    void enterScope() {
        int newScopeId = currentScopeId;
        auto newScope = std::make_unique<Scope>(newScopeId);

        // 同时添加到两个数据结构中
        scopes.push_back(std::move(newScope));
        scopeById[newScopeId] = scopes.back().get(); // 保存指针
        scopeStack.push_back(newScopeId);

        currentScopeId++;
    }

    // 退出当前作用域
    void exitScope() {
        if (!scopeStack.empty()) {
            int removedScopeId = scopeStack.back();
            scopeStack.pop_back();
        }
    }

    // 获取当前作用域ID
    int getCurrentScopeId() const {
        if (scopeStack.empty()) {
            throw std::runtime_error("Scope stack is empty - programming error!");
        }
        return scopeStack.back();
    }

    // 【新增】按作用域ID获取作用域
    Scope* getScopeById(int scopeId) {
        auto it = scopeById.find(scopeId);
        if (it != scopeById.end()) {
            return it->second;
        }
        return nullptr;
    }

    // 添加符号到当前作用域
    bool addSymbol(const SymbolEntry& entry) {
        // 1. 检查作用域栈是否为空
        //    (如果为空，getCurrentScopeId() 会抛出异常，我们在此处安全返回)
        if (scopes.empty()) return false;

        // 2. 获取当前 *激活* 的作用域ID (来自 scopeStack.back())
        int currentId = entry.scope;

        // 3. 验证ID的有效性
        //    (ID从1开始，而scopes向量索引从0开始)
        if (currentId <= 0 || (size_t)currentId > scopes.size()) {
            return false; // 严重错误：ID与scopes向量不同步
        }

        // 4. 访问正确的作用域 (scopes[ID - 1]) 并添加符号
        return scopes[currentId - 1]->addSymbol(entry);
    }

    // 修改 findSymbol 函数
    // 修改 findSymbol 函数 (带详细调试信息)
    SymbolEntry* findSymbol(const std::string& name) {
        // [DEBUG] 打印搜索头部信息
        std::cout << "\n[SymbolTable Debug] Searching for symbol: '" << name << "'" << std::endl;

        // 打印当前的作用域栈状态
        std::cout << "  > Current Scope Stack (Inner -> Outer): [ ";
        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
            std::cout << *it << " ";
        }
        std::cout << "]" << std::endl;

        // 按照作用域栈的顺序从内层到外层查找符号
        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
            int scopeId = *it;
            Scope* scope = getScopeById(scopeId);

            std::cout << "  > Checking Scope ID: " << scopeId;

            if (scope) {
                // [关键调试信息]：打印当前作用域内已存在的所有符号
                std::cout << "\n    Existing symbols in Scope " << scopeId << ": { ";
                const auto& symbols = scope->getSymbols();
                if (symbols.empty()) {
                    std::cout << "(empty)";
                } else {
                    for (size_t i = 0; i < symbols.size(); ++i) {
                        std::cout << symbols[i].name;
                        if (i < symbols.size() - 1) std::cout << ", ";
                    }
                }
                std::cout << " }" << std::endl;

                // 执行查找
                SymbolEntry* symbol = scope->findSymbol(name);
                if (symbol != nullptr) {
                    std::cout << "    [SUCCESS] Found '" << name << "' in Scope " << scopeId
                              << " (Type: " << getTypeString(symbol->type)
                              << ", Offset: " << symbol->offset << ")" << std::endl;
                    return symbol; // 找到后立即返回
                } else {
                    std::cout << "    [INFO] '" << name << "' not found in Scope " << scopeId << ", moving to outer scope..." << std::endl;
                }
            } else {
                std::cout << " [ERROR] Scope pointer for ID " << scopeId << " is NULL!" << std::endl;
            }
        }

        std::cout << "  [FAILURE] Symbol '" << name << "' not found in any active scope." << std::endl;
        return nullptr;
    }

    // 更新函数符号的栈帧大小（需要在语义分析完函数体后调用）
    void updateFuncSymbolSize(const std::string& funcName, int totalSize) {
        SymbolEntry* entry = findSymbol(funcName);
        if (entry && entry->isFunction()) {
            entry->stackFrameSize = totalSize;

            // 需要更新 Scope 中的副本以保持数据一致性
            Scope* scope = getScopeById(entry->scope);
            if (scope) {
                scope->updateSymbol(funcName, *entry);
            }
        }
    }

    // 获取所有符号（按作用域排序）
    //收集符号表中所有作用域的所有符号，将它们按作用域顺序组织成一个向量返回。
    // 5. 修改getAllSymbols的逻辑，符合新的数据结构
    std::vector<SymbolEntry> getAllSymbols() const {
        std::vector<SymbolEntry> allSymbols;
        for (const auto& scope : scopes) {
            // scope->getSymbols() 现在返回 const std::vector<SymbolEntry>&
            // 不再需要遍历 'pair'
            for (const auto& entry : scope->getSymbols()) {
                allSymbols.push_back(entry); // 'entry' 现在就是 SymbolEntry
            }
        }
        return allSymbols;
    }

    // 检查符号是否在当前作用域已定义
    bool isDefinedInCurrentScope(const std::string& name) const {
        if (scopes.empty()) return false;
        return scopes.back()->findSymbol(name) != nullptr;
    }

    // 获取全局作用域ID
    int getGlobalScopeId() const { return 1; }

    // 格式化符号表为可读字符串
    std::string formatSymbolTable() const {
        std::stringstream ss;

        auto allSymbols = getAllSymbols();

        // 按作用域排序
        std::stable_sort(allSymbols.begin(), allSymbols.end(),
                         [](const SymbolEntry& a, const SymbolEntry& b) {
                             if (a.scope != b.scope) return a.scope < b.scope;
                             return a.line < b.line;
                         });

        ss << "========== 符号表 (含MIPS布局信息) ==========\n";
        ss << "总符号数量: " << allSymbols.size() << "\n\n";

        int currentScope = -1;
        for (const auto& symbol : allSymbols) {
            // 【新增过滤逻辑】：跳过 main 函数
            if (symbol.name == "main" && symbol.scope == 1 && symbol.isFunction()) {
                continue;
            }

            // 新的作用域开始
            if (symbol.scope != currentScope) {
                currentScope = symbol.scope;
                ss << "\n--- 作用域 " << currentScope;
                if (currentScope == getGlobalScopeId()) {
                    ss << " (全局作用域)";
                } else {
                    ss << " (局部作用域)";
                }
                ss << " ---\n";
            }

            // 输出符号信息
            ss << "  " << symbol.name << ":\n";
            ss << "    类型: " << getTypeString(symbol.type) << "\n";
            ss << "    行号: " << symbol.line << "\n";

            // === 新增：代码生成布局信息 ===
            ss << "    占用空间: " << symbol.size << " 字节\n";

            if (symbol.isFunction()) {
                ss << "    栈帧总大小: " << symbol.stackFrameSize << " (用于函数序言/尾声)\n";
            } else if (symbol.scope == getGlobalScopeId() || symbol.type == SymbolType::StaticInt || symbol.type == SymbolType::StaticIntArray) {
                // 全局变量或静态变量
                ss << "    汇编标签: " << (symbol.label.empty() ? symbol.name : symbol.label) << " (.data段)\n";
            } else {
                // 局部变量/参数
                ss << "    栈偏移量: " << symbol.offset << " (相对于FP/SP)\n";
            }
            // ===========================

            ss << "    是否参数: " << (symbol.isParam ? "是" : "否") << "\n";

            // 数组信息
            if (symbol.isArray()) {
                ss << "    数组大小: " << (symbol.arraySize > 0 ? std::to_string(symbol.arraySize) : "未知") << "\n";

                // 数组初始化值
                if (!symbol.arrayInitValues.empty()) {
                    ss << "    初始化值: [";
                    for (size_t i = 0; i < symbol.arrayInitValues.size(); ++i) {
                        if (i > 0) ss << ", ";
                        ss << symbol.arrayInitValues[i];
                    }
                    ss << "]\n";
                }
            }

            // 常量值信息
            if (symbol.isConstant() && !symbol.isArray()) {
                ss << "    常量值: " << symbol.value << "\n";
            }

            // 函数参数信息
            if (symbol.isFunction() && !symbol.paramTypes.empty()) {
                ss << "    参数列表 (" << symbol.paramTypes.size() << " 个):\n";
                for (size_t i = 0; i < symbol.paramTypes.size(); ++i) {
                    const auto& param = symbol.paramTypes[i];
                    ss << "      " << (i + 1) << ". " << param.name << ": "
                       << (param.isArray ? "数组 " : "")
                       << getTypeString(param.type) << "\n";
                }
            }

            ss << "\n";
        }

        // 添加作用域统计
        ss << "作用域统计:\n";
        std::unordered_map<int, int> scopeCount;
        for (const auto& symbol : allSymbols) {
            scopeCount[symbol.scope]++;
        }
        for (const auto& [scopeId, count] : scopeCount) {
            ss << "  作用域 " << scopeId << ": " << count << " 个符号\n";
        }

        ss << "============================\n";
        return ss.str();
    }

// 将格式化的符号表写入文件
    void writeFormattedSymbolTable(const std::string& filename) const {
        std::ofstream fout(filename);
        if (!fout.is_open()) {
            std::cerr << "无法打开符号表文件: " << filename << std::endl;
            return;
        }

        fout << formatSymbolTable();
        fout.close();
        std::cout << "符号表已输出到: " << filename << std::endl;
    }

// 辅助函数：将 SymbolType 转换为字符串
    static std::string getTypeString(SymbolType type) {
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


};

#endif // COMPILER_SYMBOLTABLE_H