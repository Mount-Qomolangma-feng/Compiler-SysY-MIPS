#ifndef COMPILER_TREENODE_H
#define COMPILER_TREENODE_H

#include <string>
#include <vector>
#include <memory>

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

    // 终结符节点
    TERMINAL
};
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

    // 用于调试，暂时用不到，可以先不实现
    void print(int depth = 0) const;
};

#endif // COMPILER_TREENODE_H