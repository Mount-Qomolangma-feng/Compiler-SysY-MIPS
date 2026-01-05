#ifndef COMPILER_TREENODE_H
#define COMPILER_TREENODE_H

#include <string>
#include <vector>
#include <memory>
#include <iostream> // <-- 确保包含

// 语法树节点类型
enum class NodeType {
    // 编译单元
    COMP_UNIT,

    // 声明相关
    CONST_DECL, VAR_DECL, CONST_DEF, VAR_DEF,
    CONST_INIT_VAL, INIT_VAL,

    // 函数相关
    FUNC_DEF, MAIN_FUNC_DEF, FUNC_TYPE,
    FUNC_F_PARAMS, FUNC_F_PARAM, FUNC_R_PARAMS,

    // 语句相关
    BLOCK, BLOCK_ITEM, STMT, FOR_STMT,

    // 表达式相关
    EXP, COND, LVAL, PRIMARY_EXP, UNARY_EXP,
    MUL_EXP, ADD_EXP, REL_EXP, EQ_EXP,
    LAND_EXP, LOR_EXP, CONST_EXP,

    // 数值类型 - 添加这一行
    NUMBER,

    // 运算符
    UNARY_OP,

    DECL,

    // 终结符节点
    TERMINAL
};

// [新增] 辅助函数：将 NodeType 转换为可读的字符串
// (直接实现在头文件中)
inline std::string nodeTypeToString(NodeType type) {
    switch (type) {
        case NodeType::COMP_UNIT: return "COMP_UNIT";
        case NodeType::CONST_DECL: return "CONST_DECL";
        case NodeType::VAR_DECL: return "VAR_DECL";
        case NodeType::CONST_DEF: return "CONST_DEF";
        case NodeType::VAR_DEF: return "VAR_DEF";
        case NodeType::CONST_INIT_VAL: return "CONST_INIT_VAL";
        case NodeType::INIT_VAL: return "INIT_VAL";
        case NodeType::FUNC_DEF: return "FUNC_DEF";
        case NodeType::MAIN_FUNC_DEF: return "MAIN_FUNC_DEF";
        case NodeType::FUNC_TYPE: return "FUNC_TYPE";
        case NodeType::FUNC_F_PARAMS: return "FUNC_F_PARAMS";
        case NodeType::FUNC_F_PARAM: return "FUNC_F_PARAM";
        case NodeType::FUNC_R_PARAMS: return "FUNC_R_PARAMS";
        case NodeType::BLOCK: return "BLOCK";
        case NodeType::BLOCK_ITEM: return "BLOCK_ITEM";
        case NodeType::STMT: return "STMT";
        case NodeType::FOR_STMT: return "FOR_STMT";
        case NodeType::EXP: return "EXP";
        case NodeType::COND: return "COND";
        case NodeType::LVAL: return "LVAL";
        case NodeType::PRIMARY_EXP: return "PRIMARY_EXP";
        case NodeType::UNARY_EXP: return "UNARY_EXP";
        case NodeType::MUL_EXP: return "MUL_EXP";
        case NodeType::ADD_EXP: return "ADD_EXP";
        case NodeType::REL_EXP: return "REL_EXP";
        case NodeType::EQ_EXP: return "EQ_EXP";
        case NodeType::LAND_EXP: return "LAND_EXP";
        case NodeType::LOR_EXP: return "LOR_EXP";
        case NodeType::CONST_EXP: return "CONST_EXP";
        case NodeType::NUMBER: return "NUMBER";
        case NodeType::UNARY_OP: return "UNARY_OP";
        case NodeType::DECL: return "DECL";
        case NodeType::TERMINAL: return "TERMINAL";
        default: return "UNKNOWN";
    }
}

//语法树还没有实现将错误信息加入语法树，在做语义分析作业时，应该实现这一功能
class TreeNode {
public:
    NodeType nodeType;        //节点的语法类型
    std::string value;        // 节点的值（对于终结符）
    int line;                 // 行号
    std::vector<std::shared_ptr<TreeNode>> children;    //TreeNode类型的容器，用来表示子节点

    //构造函数，用来初始化节点对象
    TreeNode(NodeType type, int lineNum, const std::string& val = "")
            : nodeType(type), value(val), line(lineNum) {}
    //向当前节点添加子节点
    void addChild(std::shared_ptr<TreeNode> child) {
        children.push_back(child);
    }

    // [修改] 直接在类定义中实现 print 函数
    void print(std::ostream& out, int depth = 0) const {
        // 1. 创建缩进
        std::string indent(depth * 2, ' '); // 每层缩进2个空格

        // 2. 打印当前节点信息
        out << indent << "|- " << nodeTypeToString(nodeType); // 打印类型
        out << " (Line: " << line << ")"; // 打印行号

        // 3. 如果有值（如 终结符、函数类型等），则打印值
        if (!value.empty()) { //
            // 对字符串值进行转义，防止换行符破坏格式
            if (value == "\n") {
                out << " [Value: \\n]";
            } else {
                out << " [Value: " << value << "]"; //
            }
        }
        out << "\n"; // 换行

        // 4. 递归打印所有子节点
        for (const auto& child : children) { //
            if (child) {
                child->print(out, depth + 1); // 深度+1
            }
        }
    }
};

#endif // COMPILER_TREENODE_H