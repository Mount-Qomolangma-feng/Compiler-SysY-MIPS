#include "Parser.h"
#include <algorithm>
#include <iostream>

//构造函数
// 在Parser构造函数中初始化
Parser::Parser(const std::vector<Token>& tokens, const std::vector<LexError>& lexErrors)
        : tokens(tokens), lexErrors(lexErrors), currentTokenIndex(0),
          outputEnabled(false), outputStream(nullptr), previousTokenLine(1),
          currentFunctionCallLine(0) {}  // 添加初始化

          //获取当前token
const Token& Parser::peek() const {
    static Token emptyToken{"", "", 0};
    if (currentTokenIndex < tokens.size()) {
        return tokens[currentTokenIndex];
    }
    return emptyToken;
}

const Token& Parser::advance() {
    if (currentTokenIndex < tokens.size()) {
        // 记录前一个token的行号
        if (currentTokenIndex > 0) {
            previousTokenLine = tokens[currentTokenIndex - 1].line;
        }

        if (outputEnabled) {
            outputToken(tokens[currentTokenIndex]);//向文件流中输出
        }
        return tokens[currentTokenIndex++];
    }
    static Token emptyToken{"", "", 0};
    return emptyToken;
}
//如果检查成功了，就前进，并输入token
bool Parser::match(const std::string& expectedCode) {
    if (check(expectedCode)) {
        //特别注意，match函数是带有advance的
        advance();
        return true;
    }
    return false;
}
//检查当前token是否符合目标
bool Parser::check(const std::string& expectedCode) const {
    return currentTokenIndex < tokens.size() &&
           tokens[currentTokenIndex].code == expectedCode;
}
//错误处理函数
void Parser::error(int line, const std::string& code,const std::vector<std::string>& syncTokens) {//code指的是i,j,k

    syntaxErrors.push_back({line, code});
    syncTo(syncTokens);

}
//下面这一堆是老代码
/*
    // 对于缺少分号的错误，使用前一个token的行号更准确
    int errorLine = line;

    // 对于特定错误类型使用更合适的行号
    if (code == "j" && currentFunctionCallLine > 0) {
        errorLine = currentFunctionCallLine;
    } else if (code == "i") {
        errorLine = previousTokenLine;
    }

    // 避免重复错误报告
    bool duplicate = false;
    for (const auto& err : syntaxErrors) {
        if (err.line == errorLine && err.code == code) {
            duplicate = true;
            break;
        }
    }

    if (!duplicate) {
        syntaxErrors.push_back({errorLine, code});
    }
*/

void Parser::error2(int line, const std::string& code)
{
    syntaxErrors.push_back({line, code});
}

bool Parser::isStartOfExp() const {
    // 表达式的开始符号
    return check("IDENFR") ||     // 标识符（变量、函数调用）
           check("INTCON") ||     // 整数常量
           check("LPARENT") ||    // 左括号（子表达式）
           check("PLUS") ||       // 正号（一元运算符）
           check("MINU") ||       // 负号（一元运算符）
           check("NOT");          // 逻辑非（一元运算符）
}

void Parser::enableOutput(std::ostream& stream) {
    outputEnabled = true;
    outputStream = &stream;
}

void Parser::disableOutput() {
    outputEnabled = false;
    outputStream = nullptr;
}
//输出码和类型
void Parser::outputToken(const Token& token) {
    if (outputEnabled && outputStream) {
        *outputStream << token.code << " " << token.lexeme << "\n";
    }
}
//输出终结符类型
void Parser::outputNonTerminal(const std::string& name) {
    if (outputEnabled && outputStream) {
        *outputStream << "<" << name << ">\n";
    }
}
//我在想，有没有一种可能，项目目前阶段根本就用不到syncto
void Parser::syncTo(const std::vector<std::string>& syncTokens) {

    while (currentTokenIndex < tokens.size()) {
        // 检查当前token是否是同步token
        bool foundSync = false;
        for (const auto& syncToken : syncTokens) {
            if (check(syncToken)) {
                foundSync = true;
                break;
            }
        }
        if (foundSync) {
            return;
        }

        advance();
    }
}

// 编译单元 CompUnit → {Decl} {FuncDef} MainFuncDef,整个项目的入口
std::shared_ptr<TreeNode> Parser::parseCompUnit() {
    auto node = std::make_shared<TreeNode>(NodeType::COMP_UNIT, peek().line);

    // {Decl} - 但需要排除函数定义的情况
    while (check("CONSTTK") || (check("INTTK") &&
                                !(currentTokenIndex + 2 < tokens.size() &&
                                tokens[currentTokenIndex + 2].code == "LPARENT"))) {
        node->addChild(parseDecl());
    }

    // {FuncDef}
    while (check("VOIDTK") || check("INTTK")) {
        if (check("INTTK") && currentTokenIndex + 1 < tokens.size() &&
            tokens[currentTokenIndex + 1].lexeme == "main") {
            break; // 遇到main函数定义，跳出
        }
        node->addChild(parseFuncDef());
    }

    // MainFuncDef
    node->addChild(parseMainFuncDef());

    outputNonTerminal("CompUnit");
    return node;
}

// 声明 Decl → ConstDecl | VarDecl，变量声明部分的入口
std::shared_ptr<TreeNode> Parser::parseDecl() {
    if (check("CONSTTK")) {
        return parseConstDecl();
    } else {
        return parseVarDecl();
    }
}

// 常量声明 ConstDecl → 'const' BType ConstDef { ',' ConstDef } ';'
std::shared_ptr<TreeNode> Parser::parseConstDecl() {
    auto node = std::make_shared<TreeNode>(NodeType::CONST_DECL, peek().line);

    // 定义同步集合
    std::vector<std::string> constDeclFollow = {"INTTK", "VOIDTK", "CONSTTK", "RBRACE", "MAINTK", "SEMICN"};

    match("CONSTTK");

    node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "const"));

    // BType
    node->addChild(parseBType());

    // ConstDef
    node->addChild(parseConstDef());

    // { ',' ConstDef }
    while (match("COMMA")) {//消耗掉带,的那一堆
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ","));
        node->addChild(parseConstDef());
    }

    // ';'
    // 分号检查 - 使用新的error函数

    if (!match("SEMICN")) {
        int errLine = (currentTokenIndex > 0 ?
                       tokens[currentTokenIndex - 1].line :
                       peek().line);
        error2(errLine, "i");


        //error2(previousTokenLine, "i");
    }
    else{
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ";"));
    }

    outputNonTerminal("ConstDecl");
    return node;
}

// 基本类型 BType → 'int'
//只有确定了当前token是int才能用
std::shared_ptr<TreeNode> Parser::parseBType() {
    auto node = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "int");
    match("INTTK");
    return node;
}

// 常量定义 ConstDef → Ident [ '[' ConstExp ']' ] '=' ConstInitVal
//例如a=1,b=2之类的
std::shared_ptr<TreeNode> Parser::parseConstDef() {
    auto node = std::make_shared<TreeNode>(NodeType::CONST_DEF, peek().line);

    // Ident (必需但无对应错误码)
    if (!match("IDENFR")) {
        syncTo({"COMMA", "SEMICN"});
        return node;
    }
    else{
        // 将标识符名称作为终结符添加到语法树中
        // 注意：这里使用previousTokenLine，因为match已经消费了token
        auto identNode = std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, tokens[currentTokenIndex - 1].lexeme);
        node->addChild(identNode);
    }

    // [ '[' ConstExp ']' ]
    if (match("LBRACK")) {
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "["));
        node->addChild(parseConstExp());
        if (!match("RBRACK")) {
            // 报错k + 同步token集
            error2(peek().line, "k");
        }
        else{
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "]"));
        }
    }

    // '='
    // '=' (必需但无对应错误码)
    if (!match("ASSIGN")) {
        syncTo({"COMMA", "SEMICN"});
        return node;  // 跳过ConstInitVal解析
    }
    else{
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "="));
    }


    // ConstInitVal
    node->addChild(parseConstInitVal());//常量的值（多少）
    outputNonTerminal("ConstDef");

    return node;
}

// 常量初值 ConstInitVal → ConstExp | '{' [ ConstExp { ',' ConstExp } ] '}'
//赋值语句等号右边那一堆，可能有为数组赋值的大括号
std::shared_ptr<TreeNode> Parser::parseConstInitVal() {
    auto node = std::make_shared<TreeNode>(NodeType::CONST_INIT_VAL, peek().line);

    if (match("LBRACE")) {
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "{"));
        // 处理可能的初始化列表
        if (!check("RBRACE")) {
            node->addChild(parseConstExp());
            while (match("COMMA")) {
                node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ","));
                node->addChild(parseConstExp());
            }
        }

        // 尝试匹配右花括号，但不报错
        match("RBRACE"); // 静默尝试匹配，不报错
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "}"));
    }
        // 简单常量表达式
    else {
        node->addChild(parseConstExp());
    }
    outputNonTerminal("ConstInitVal");

    return node;
}

// 变量声明 VarDecl → [ 'static' ] BType VarDef { ',' VarDef } ';'
//如 int a, b[10];
std::shared_ptr<TreeNode> Parser::parseVarDecl() {
    auto node = std::make_shared<TreeNode>(NodeType::VAR_DECL, peek().line);

    // [ 'static' ]
    if (match("STATICTK")) {
        // 已处理
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "static"));
    }

    // BType
    node->addChild(parseBType());

    // VarDef
    node->addChild(parseVarDef());

    // { ',' VarDef }
    while (match("COMMA")) {
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ","));
        node->addChild(parseVarDef());//a,a=10,a[3]之类的
    }

    // 在检查分号之前保存当前行号作为分号应该出现的位置
    int semicolonExpectedLine = previousTokenLine;

    // ';' 带错误恢复
    if (!match("SEMICN")) {
        // 使用带同步恢复的错误处理
        int errLine = (currentTokenIndex > 0 ?
                       tokens[currentTokenIndex - 1].line :
                       peek().line);
        error2(errLine, "i");

    }
    else{
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ";"));
    }

    outputNonTerminal("VarDecl");
    return node;
}

// 变量定义 VarDef → Ident [ '[' ConstExp ']' ] | Ident [ '[' ConstExp ']' ] '=' InitVal
//a,a=10,a[3]之类的
std::shared_ptr<TreeNode> Parser::parseVarDef() {
    auto node = std::make_shared<TreeNode>(NodeType::VAR_DEF, peek().line);

    // 必须存在的标识符
    match("IDENFR");

    // 将标识符名称作为终结符添加到语法树中
    auto identNode = std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, tokens[currentTokenIndex - 1].lexeme);
    node->addChild(identNode);

    // [ '[' ConstExp ']' ]
    // 可选的一维数组维度 [ConstExp]
    if (match("LBRACK")) {
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "["));
        node->addChild(parseConstExp()); // 数组大小

        // 带同步恢复的错误处理
        if (!match("RBRACK")) {
            error2(peek().line, "k");
        }
        else{
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "]"));
        }
    }

    // [ '=' InitVal ]
    if (match("ASSIGN")) {
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "="));
        node->addChild(parseInitVal());//表达式的右值
    }

    outputNonTerminal("VarDef");

    return node;
}

// 变量初值 InitVal → Exp | '{' [ Exp { ',' Exp } ] '}'
//等号式的右值
std::shared_ptr<TreeNode> Parser::parseInitVal() {
    auto node = std::make_shared<TreeNode>(NodeType::INIT_VAL, peek().line);

    if (match("LBRACE")) {
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "{"));
        if (!check("RBRACE")) {
            node->addChild(parseExp());//解析表达式
            while (match("COMMA")) {
                node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ","));
                node->addChild(parseExp());
            }
        }
        // 移除了错误处理部分
        match("RBRACE"); // 仅尝试匹配，不报错
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "}"));
    } else {
        node->addChild(parseExp());
    }

    outputNonTerminal("InitVal");

    return node;
}

// 函数定义 FuncDef → FuncType Ident '(' [FuncFParams] ')' Block
//分析主函数前的那一堆函数定义
std::shared_ptr<TreeNode> Parser::parseFuncDef() {
    auto node = std::make_shared<TreeNode>(NodeType::FUNC_DEF, peek().line);

    // FuncType
    node->addChild(parseFuncType());//专门用于解析函数返回类型，如int,void之类的

    match("IDENFR");//解析函数名

    // 将标识符名称作为终结符添加到语法树中
    auto identNode = std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, tokens[currentTokenIndex - 1].lexeme);
    node->addChild(identNode);

    match("LPARENT");

    node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "("));

    bool judge= false;

    // [FuncFParams]
    if (!check("RPARENT")) {
        if (check("LBRACE")) {
            // 遇到左大括号，说明缺少右括号且没有形参列表，直接报错
            error2(previousTokenLine, "j");
            judge= true;
        }
        else{
            node->addChild(parseFuncFParams());//解析函数形参列表
        }

    }

    // ')'
    if (!match("RPARENT")) {
        if(!judge)
        {
            // 缺少右小括号，报 j 类错误
            error2(peek().line, "j"); // 同步到函数体开始
        }

    }
    else{
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ")"));
    }

    // Block
    node->addChild(parseBlock());

    outputNonTerminal("FuncDef");
    return node;
}

// 主函数定义 MainFuncDef → 'int' 'main' '(' ')' Block
//解析整个主函数
std::shared_ptr<TreeNode> Parser::parseMainFuncDef() {
    auto node = std::make_shared<TreeNode>(NodeType::MAIN_FUNC_DEF, peek().line);

    match("INTTK");
    node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "int"));

    match("MAINTK");
    node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "main"));

    match("LPARENT");
    node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "("));

    // ')'
    if (!match("RPARENT")) {
        error2(peek().line, "j"); // 同步到函数体开始
    }
    else{
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ")"));
    }

    // Block
    node->addChild(parseBlock());

    outputNonTerminal("MainFuncDef");
    return node;
}

// 函数类型 FuncType → 'void' | 'int'
//解析函数返回类型，只可能是int或者void
std::shared_ptr<TreeNode> Parser::parseFuncType() {
    auto node = std::make_shared<TreeNode>(NodeType::FUNC_TYPE, peek().line);

    // 匹配 void 或 int 类型
    if (match("VOIDTK")) {
        node->value = "void";  // 存储具体类型
    } else if (match("INTTK")) {
        node->value = "int";   // 存储具体类型
    }

    outputNonTerminal("FuncType");

    return node;
}

// 函数形参表 FuncFParams → FuncFParam { ',' FuncFParam }
//形参列表只解析括号里的那一堆，不负责解析两个括号
std::shared_ptr<TreeNode> Parser::parseFuncFParams() {
    auto node = std::make_shared<TreeNode>(NodeType::FUNC_F_PARAMS, peek().line);

    node->addChild(parseFuncFParam());//单个形参

    while (match("COMMA")) {//只要有,就继续解析形参
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ","));
        node->addChild(parseFuncFParam());
    }

    outputNonTerminal("FuncFParams");

    return node;
}

// 函数形参 FuncFParam → BType Ident ['[' ']']
std::shared_ptr<TreeNode> Parser::parseFuncFParam() {
    auto node = std::make_shared<TreeNode>(NodeType::FUNC_F_PARAM, peek().line);

    // BType
    node->addChild(parseBType());

    match("IDENFR");
    // 将标识符名称作为终结符添加到语法树中
    auto identNode = std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, tokens[currentTokenIndex - 1].lexeme);
    node->addChild(identNode);

    // ['[' ']']
    if (match("LBRACK")) {
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "["));

        if (!match("RBRACK")) {
            error2(peek().line, "k");
        }
        else{
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "]"));
        }
    }

    outputNonTerminal("FuncFParam");

    return node;
}

// 语句块 Block → '{' { BlockItem } '}'
std::shared_ptr<TreeNode> Parser::parseBlock() {
    auto node = std::make_shared<TreeNode>(NodeType::BLOCK, peek().line);

    match("LBRACE");
    node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "{"));

    // { BlockItem }
    while (!check("RBRACE") && currentTokenIndex < tokens.size()) {
        node->addChild(parseBlockItem());//解析单个快项目，解析一句句具体的语句
    }

    // 修改这里：在匹配之前获取右花括号的行号
    int rbraceLine = peek().line;  // 获取右花括号的当前行号

    match("RBRACE");
    node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, rbraceLine, "}"));
    //std::cout<<rbraceLine<<std::endl;

    outputNonTerminal("Block");
    return node;
}

// 语句块项 BlockItem → Decl | Stmt
//分发任务给声明和语句
//声明就是创造了新的东西
//语句就是没有创造新的东西
std::shared_ptr<TreeNode> Parser::parseBlockItem() {
    if (check("CONSTTK") || check("INTTK") || check("STATICTK")) {
        return parseDecl();
    } else {
        return parseStmt();
    }
}

// 语句 Stmt 的各种情况
/*
    parseStmt() 是语句解析器的核心函数，负责：
    识别并解析所有类型的语句
    构建语句的语法树结构
    处理错误恢复和同步
    支持多种语句类型：
    代码块
    条件语句
    循环语句
    跳转语句
    返回语句
    输出语句
    赋值语句
    表达式语句
*/
std::shared_ptr<TreeNode> Parser::parseStmt() {
    auto node = std::make_shared<TreeNode>(NodeType::STMT, peek().line);

    if (check("LBRACE")) {//检测到大括号，就调入语句块处理
        // Block
        node->addChild(parseBlock());
    } else if (check("IFTK")) {//检测到if语句
        // 'if' '(' Cond ')' Stmt [ 'else' Stmt ]
        match("IFTK"); // if
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "if"));

        match("LPARENT");
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "("));

        node->addChild(parseCond());//parseCond用于解析条件表达式，这里就是if后面括号中的东西
        if (!match("RPARENT")) {
            // 使用前一个 token 的行号报告错误
            int errorLine = previousTokenLine;

            // 调用错误处理函数，传递错误类型和同步 token
            error2(errorLine, "j");

            // 注意：不再需要显式调用 syncTo()，因为 error() 内部已处理
        }
        else{
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ")"));
        }

        node->addChild(parseStmt());
        if (match("ELSETK")) {
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "else"));
            node->addChild(parseStmt());
        }
    } else if (check("FORTK")) {//检测到for循环
        // 'for' '(' [ForStmt] ';' [Cond] ';' [ForStmt] ')' Stmt
        match("FORTK"); // for
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "for"));

        match("LPARENT");
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "("));
        if (!check("SEMICN")) {
            node->addChild(parseForStmt());
        }

        match("SEMICN");
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ";"));

        if (!check("SEMICN")) {
            node->addChild(parseCond());//条件表达式
        }

        match("SEMICN");
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ";"));

        if (!check("RPARENT")) {
            node->addChild(parseForStmt());
        }
        if (!match("RPARENT")) {
            // 使用前一个 token 的行号报告错误
            int errorLine = previousTokenLine;

            // 调用错误处理函数，传递错误类型和同步 token
            error2(errorLine, "j");

            // 注意：不再需要显式调用 syncTo()，因为 error() 内部已处理
        }
        else{
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ")"));
        }

        node->addChild(parseStmt());
    } else if (check("BREAKTK") || check("CONTINUETK")) {//break语句或者continue语句
        // 解析 break 语句
        if (match("BREAKTK")) {
            //node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "break"));

            auto breakKeyword = std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "break");
            node->addChild(breakKeyword);

            int jumpLine = previousTokenLine; // 记录跳转语句行号

            // 检查分号
            if (!match("SEMICN")) {
                int errLine = (currentTokenIndex > 0 ?
                               tokens[currentTokenIndex - 1].line :
                               peek().line);
                error2(errLine, "i");

                //error2(jumpLine, "i");
            }
            else{
                node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ";"));
            }

            // 创建 break 语句节点
            //auto breakNode = std::make_shared<TreeNode>(NodeType::STMT, jumpLine);
            outputNonTerminal("Stmt");
            return node;
        }

        // 解析 continue 语句
        if (match("CONTINUETK")) {
            // 添加continue关键字节点
            auto continueKeyword = std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "continue");
            node->addChild(continueKeyword);

            int jumpLine = previousTokenLine; // 记录跳转语句行号

            // 检查分号
            if (!match("SEMICN")) {
                error2(jumpLine, "i");
            }
            else{
                // 可选：添加分号节点
                node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ";"));
            }

            // 创建 continue 语句节点
            //auto continueNode = std::make_shared<TreeNode>(NodeType::STMT, jumpLine);
            outputNonTerminal("Stmt");
            return node;
        }
    } else if (check("RETURNTK")) {//return语句
        // 'return' [Exp] ';'
        int returnLine = peek().line; // 记录return行号
        match("RETURNTK"); // return
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "return"));

        if (!check("SEMICN")) {
            node->addChild(parseExp());
        }
        if (!match("SEMICN")) {
            int errLine = (currentTokenIndex > 0 ?
                           tokens[currentTokenIndex - 1].line :
                           peek().line);
            error2(errLine, "i");

            //error2(returnLine, "i"); // 使用return语句的行号
            //syncTo({"SEMICN", "RBRACE"});
        }
        else{
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ";"));
        }
    } else if (check("PRINTFTK")) {//printf语句
        // 'printf' '(' StringConst { ',' Exp } ')' ';'
        int printfLine = peek().line; // 记录printf行号
        match("PRINTFTK"); // printf
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "printf"));

        match("LPARENT");
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "("));

        match("STRCON");//字符串常量
        std::string strValue = tokens[currentTokenIndex - 1].lexeme;
        auto strConstNode = std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, strValue);
        node->addChild(strConstNode);

        //只要匹配到逗号就添加变量节点
        while (match("COMMA")) {
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ","));
            node->addChild(parseExp());
        }
        if (!match("RPARENT")) {
            error2(printfLine, "j");
            //syncTo({"SEMICN"});
        }
        else{
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ")"));
        }
        if (!match("SEMICN")) {
            int errLine = (currentTokenIndex > 0 ?
                           tokens[currentTokenIndex - 1].line :
                           peek().line);
            error2(errLine, "i");

            //error2(printfLine, "i");
            // 不需要syncTo，因为已经是语句结束
        }
        else{
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ";"));
        }
    } else if (check("IDENFR")) {
        // 通用方法：检查是否构成 LVal '=' Exp 的模式
        //-------------------------------------------------------
        // 改进版：处理赋值语句 / 表达式语句
        //-------------------------------------------------------
        int stmtLine = peek().line;
        size_t lookahead = currentTokenIndex;
        bool isAssignment = false;
        bool missingRbrack = false;

        // 跳过可能的 LVal 部分
        // LVal → Ident ['[' Exp ']']
        lookahead++; // 跳过 Ident
        if (lookahead < tokens.size() && tokens[lookahead].code == "LBRACK") {
            int bracketCount = 1;
            lookahead++;
            while (lookahead < tokens.size() && bracketCount > 0) {
                std::string code = tokens[lookahead].code;

                if (code == "LBRACK") bracketCount++;
                else if (code == "RBRACK") bracketCount--;
                    // 提前终止，防止读到文件末尾
                else if (code == "ASSIGN" || code == "SEMICN" ||
                         code == "COMMA"  || code == "RBRACE" ||
                         code == "RPARENT") {
                    break;
                }

                lookahead++;
            }
            if (bracketCount > 0) missingRbrack = true;
        }

        // 检查是否存在 =
        if (lookahead < tokens.size() && tokens[lookahead].code == "ASSIGN") {
            isAssignment = true;
        }

        // 若缺 ']'，通常也是 LVal 的一部分 => 视为赋值语句
        if (missingRbrack) {
            isAssignment = true;
        }

        if (isAssignment) {
            // LVal '=' Exp ';'
            int assignLine = peek().line;
            node->addChild(parseLVal());

            if (!match("ASSIGN")) {
                // 理论上不会到这里，因为前面已经确认有ASSIGN
                syncTo({"SEMICN"});
                return node;
            }
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "="));

            node->addChild(parseExp());

            if (!match("SEMICN")) {
                int errLine = (currentTokenIndex > 0 ?
                               tokens[currentTokenIndex - 1].line :
                               peek().line);
                error2(errLine, "i");

                //error2(previousTokenLine, "i");
                // 同步到下一个语句开始
                //syncTo({"SEMICN", "RBRACE"});
            }
            else{
                node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ";"));
            }
        } else {
            // 不是赋值语句，回退到表达式语句处理
            // [Exp] ';'
            int expLine = peek().line;
            if (!check("SEMICN")) {
                node->addChild(parseExp());
            }
            if (!match("SEMICN")) {
                int errLine = (currentTokenIndex > 0 ?
                               tokens[currentTokenIndex - 1].line :
                               peek().line);
                error2(errLine, "i");

                //error2(expLine, "i");
            }
            else{
                node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ";"));
            }
        }
    }else {
        // [Exp] ';'
        int expLine = peek().line; // 记录表达式语句行号
        if (!check("SEMICN")) {
            node->addChild(parseExp());//当前不是分号，就解析表达式
        }
        if (!match("SEMICN")) {//还没有分号，就是有错
            int errLine = (currentTokenIndex > 0 ?
                           tokens[currentTokenIndex - 1].line :
                           peek().line);
            error2(errLine, "i");

            //error2(expLine, "i");
            // 不需要syncTo，因为已经是语句结束
        }
        else{
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ";"));
        }
    }

    outputNonTerminal("Stmt");//分析完成
    return node;
}

// ForStmt → LVal '=' Exp { ',' LVal '=' Exp }
//解析for循环语句中的初始化部分
std::shared_ptr<TreeNode> Parser::parseForStmt() {
    auto node = std::make_shared<TreeNode>(NodeType::FOR_STMT, peek().line);

    node->addChild(parseLVal());//解析左值

    match("ASSIGN");
    node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "="));

    node->addChild(parseExp());//解析右侧表达式

    while (match("COMMA")) {
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ","));

        node->addChild(parseLVal());//解析左值

        match("ASSIGN");
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "="));

        node->addChild(parseExp());//解析右侧表达式
    }

    outputNonTerminal("ForStmt");

    return node;
}

// 表达式相关解析函数（简化实现）
//用于解析算数表达式
//表达式 Exp → AddExp // 存在即可
std::shared_ptr<TreeNode> Parser::parseExp() {
    auto node = std::make_shared<TreeNode>(NodeType::EXP, peek().line);
    node->addChild(parseAddExp());//之所以调用加法表达式是因为所有式子都可以归于加法表达式
    outputNonTerminal("Exp");

    return node;
}

//解析条件表达式
//条件表达式 Cond → LOrExp // 存在即可
std::shared_ptr<TreeNode> Parser::parseCond() {
    // 1. 显式创建一个 COND 类型的节点
    auto node = std::make_shared<TreeNode>(NodeType::COND, peek().line);

    // 2. 解析 LOrExp，并将其作为子节点添加
    auto lorExpNode = parseLOrExp();
    node->addChild(lorExpNode);

    // 3. 输出调试信息
    outputNonTerminal("Cond");

    return node;
}
//解析左值表达式
//左值表达式 LVal → Ident ['[' Exp ']'] // 1.普通变量、常量 2.一维数组
std::shared_ptr<TreeNode> Parser::parseLVal() {
    auto node = std::make_shared<TreeNode>(NodeType::LVAL, peek().line);

    match("IDENFR");
    // 将标识符名称作为终结符添加到语法树中
    auto identNode = std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, tokens[currentTokenIndex - 1].lexeme);
    node->addChild(identNode);

    // ['[' Exp ']']
    if (match("LBRACK")) {
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "["));

        node->addChild(parseExp());
        if (!match("RBRACK")) {
            error2(peek().line, "k");
        }
        else{
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "]"));
        }
    }

    outputNonTerminal("LVal");
    return node;
}
//用于解析基本表达式
/*
    此函数用于解析基本表达式 (PrimaryExp)，根据文法规则：
    BNF
    PrimaryExp → '(' Exp ')' | LVal | Number
    它处理三种语法结构：
    括号表达式：( expression )
    左值表达式：变量/数组元素
    数值常量
 */
std::shared_ptr<TreeNode> Parser::parsePrimaryExp() {
    auto node = std::make_shared<TreeNode>(NodeType::PRIMARY_EXP, peek().line);

    if (match("LPARENT")) {//处理括号表达式
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "("));

        node->addChild(parseExp());
        if (!match("RPARENT")) {
            error2(peek().line, "j");
        }
        else{
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ")"));
        }
    } else if (check("IDENFR")) {//处理左值表达式
        node->addChild(parseLVal());
    } else {
        node->addChild(parseNumber());//处理数值常量
    }

    outputNonTerminal("PrimaryExp");
    return node;
}
//处理数值常量
std::shared_ptr<TreeNode> Parser::parseNumber() {
    // 创建 NUMBER 类型的节点
    auto node = std::make_shared<TreeNode>(NodeType::NUMBER, peek().line);
    /*
    // 匹配整数常量
    if (check("INTCON")) {
        // 将整数常量作为终结符子节点添加
        auto numberToken = advance();
        auto terminalNode = std::make_shared<TreeNode>(NodeType::TERMINAL, numberToken.line, numberToken.lexeme);
        node->addChild(terminalNode);
    } else {
        // 如果没有找到整数常量，报告错误
        error(peek().line, "i");
        // 错误恢复：跳过当前token
        if (currentTokenIndex < tokens.size()) {
            advance();
        }
    }
     */

    auto numberToken = advance();
    auto terminalNode = std::make_shared<TreeNode>(NodeType::TERMINAL, numberToken.line, numberToken.lexeme);
    node->addChild(terminalNode);

    // 输出语法成分
    outputNonTerminal("Number");
    return node;
}
//此函数用于解析一元表达式 (UnaryExp)
//基本表达式（括号表达式/变量/数字）
//函数调用
//一元运算（正号/负号/逻辑非）
std::shared_ptr<TreeNode> Parser::parseUnaryExp() {
    auto node = std::make_shared<TreeNode>(NodeType::UNARY_EXP, peek().line);

    if (check("PLUS") || check("MINU") || check("NOT")) {//检测到一元运算符
        node->addChild(parseUnaryOp());//将运算符加入子节点
        node->addChild(parseUnaryExp());//递归分析运算符右边的部分
    } else if (check("IDENFR") && currentTokenIndex + 1 < tokens.size() &&
               tokens[currentTokenIndex + 1].code == "LPARENT") {//检测函数调用
        // 记录函数调用开始的行号
        currentFunctionCallLine = peek().line;

        // 函数调用
        match("IDENFR"); // 函数名
        auto identNode = std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, tokens[currentTokenIndex - 1].lexeme);
        node->addChild(identNode);

        match("LPARENT");
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, "("));


        // 根据"一行最多一个错误"的约定进行优化判断
        if (!check("RPARENT")) {
            if (isStartOfExp()) {
                // 可能是有效参数，解析参数列表
                node->addChild(parseFuncRParams());
            } else {
                // 确定是j类错误：既不是右括号也不是有效参数开始
                error2(currentFunctionCallLine, "j");
                outputNonTerminal("UnaryExp");
                return node;  // 提前返回，避免后续处理
            }
        }

        // 检查右括号（只有在可能有效的情况下才检查）
        if (!match("RPARENT")) {
            error2(currentFunctionCallLine, "j");
        }
        else{
            node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ")"));
        }
    } else {

        node->addChild(parsePrimaryExp());
    }

    outputNonTerminal("UnaryExp");
    return node;
}

// 其他表达式解析函数（简化实现）
//解析一元运算符（只解析一个运算符）
std::shared_ptr<TreeNode> Parser::parseUnaryOp() {
    auto node = std::make_shared<TreeNode>(NodeType::UNARY_OP, peek().line);

    if (match("PLUS")) {
        // 记录正号运算符
        node->value = "+";
    } else if (match("MINU")) {
        // 记录负号运算符
        node->value = "-";
    } else if (match("NOT")) {
        // 记录逻辑非运算符
        node->value = "!";
    }

    outputNonTerminal("UnaryOp");

    return node;
}
//用来解析整个实参列表
std::shared_ptr<TreeNode> Parser::  parseFuncRParams() {
    auto node = std::make_shared<TreeNode>(NodeType::FUNC_R_PARAMS, peek().line);
    node->addChild(parseExp());
    while (match("COMMA")) {
        node->addChild(std::make_shared<TreeNode>(NodeType::TERMINAL, previousTokenLine, ","));

        node->addChild(parseExp());
    }

    outputNonTerminal("FuncRParams");

    return node;
}

// 乘除模表达式 MulExp → UnaryExp | MulExp ('*' | '/' | '%') UnaryExp
//该函数用于解析包含乘法、除法、取模运算的表达式，构建对应的抽象语法树节点，处理运算符的优先级和左结合性。
std::shared_ptr<TreeNode> Parser::parseMulExp() {
    // 创建初始的 MulExp 节点，并将第一个 UnaryExp 作为其子节点
    auto initialMulExp = std::make_shared<TreeNode>(NodeType::MUL_EXP, peek().line);
    auto firstUnaryExp = parseUnaryExp();
    initialMulExp->addChild(firstUnaryExp);

    // 将初始 MulExp 节点设为最终节点
    auto finalNode = initialMulExp;

    // 处理连续的乘除模运算 (*, /, %)
    while (check("MULT") || check("DIV") || check("MOD")) {
        // 输出当前的 MulExp
        outputNonTerminal("MulExp");

        // 创建新的 MulExp 节点
        auto newNode = std::make_shared<TreeNode>(NodeType::MUL_EXP, peek().line);

        // 将之前的表达式作为左子树
        newNode->addChild(finalNode);

        // 处理运算符
        std::shared_ptr<TreeNode> opNode;
        if (check("MULT")) {
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "*");
        } else if (check("DIV")) {
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "/");
        } else { // MOD
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "%");
        }
        advance(); // 消费运算符
        newNode->addChild(opNode);

        // 解析右边的 UnaryExp
        auto rightExpr = parseUnaryExp();
        newNode->addChild(rightExpr);

        // 更新最终节点
        finalNode = newNode;
    }

    // 输出最终的 MulExp
    outputNonTerminal("MulExp");
    return finalNode;
}

// 加减表达式 AddExp → MulExp | AddExp ('+' | '-') MulExp
//处理加减表达式
std::shared_ptr<TreeNode> Parser::parseAddExp() {
    // 创建初始的 AddExp 节点，并将第一个 MulExp 作为其子节点
    auto initialAddExp = std::make_shared<TreeNode>(NodeType::ADD_EXP, peek().line);
    auto firstMulExp = parseMulExp();
    initialAddExp->addChild(firstMulExp);

    // 将初始 AddExp 节点设为最终节点
    auto finalNode = initialAddExp;

    // 处理连续的加减运算
    while (check("PLUS") || check("MINU")) {
        // 输出当前的 AddExp
        outputNonTerminal("AddExp");

        // 创建新的 AddExp 节点
        auto newNode = std::make_shared<TreeNode>(NodeType::ADD_EXP, peek().line);

        // 将之前的表达式作为左子树
        newNode->addChild(finalNode);

        // 处理运算符
        std::shared_ptr<TreeNode> opNode;
        if (check("PLUS")) {
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "+");
        } else {
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "-");
        }
        advance(); // 消费运算符
        newNode->addChild(opNode);

        // 解析右边的 MulExp
        auto rightExpr = parseMulExp();
        newNode->addChild(rightExpr);

        // 更新最终节点
        finalNode = newNode;
    }

    // 输出最终的 AddExp
    outputNonTerminal("AddExp");
    return finalNode;
}

// 关系表达式 RelExp → AddExp | RelExp ('<' | '>' | '<=' | '>=') AddExp
//解析关系表达式
std::shared_ptr<TreeNode> Parser::parseRelExp() {
    auto initialRelExp = std::make_shared<TreeNode>(NodeType::REL_EXP, peek().line);
    auto firstAddExp = parseAddExp();
    initialRelExp->addChild(firstAddExp);

    auto finalNode = initialRelExp;

    // 处理连续的关系运算 (<, >, <=, >=)
    while (check("LSS") || check("GRE") || check("LEQ") || check("GEQ")) {
        // 输出当前的 RelExp
        outputNonTerminal("RelExp");

        // 创建新的 RelExp 节点
        auto newNode = std::make_shared<TreeNode>(NodeType::REL_EXP, peek().line);

        // 将之前的表达式作为左子树
        newNode->addChild(finalNode);

        // 处理运算符
        std::shared_ptr<TreeNode> opNode;
        if (check("LSS")) {
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "<");
        } else if (check("GRE")) {
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, ">");
        } else if (check("LEQ")) {
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "<=");
        } else { // GEQ
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, ">=");
        }
        advance(); // 消费运算符
        newNode->addChild(opNode);

        // 解析右边的 AddExp
        auto rightExpr = parseAddExp();
        newNode->addChild(rightExpr);

        // 更新最终节点
        finalNode = newNode;
    }

    // 输出最终的 RelExp
    outputNonTerminal("RelExp");
    return finalNode;
}
std::shared_ptr<TreeNode> Parser::parseEqExp() {
    auto initialEqExp = std::make_shared<TreeNode>(NodeType::EQ_EXP, peek().line);
    auto firstRelExp = parseRelExp();
    initialEqExp->addChild(firstRelExp);

    auto finalNode = initialEqExp;

    // 处理连续的 == 或 != 运算
    while (check("EQL") || check("NEQ")) {
        // 输出当前的 EqExp
        outputNonTerminal("EqExp");

        // 创建新的 EqExp 节点
        auto newNode = std::make_shared<TreeNode>(NodeType::EQ_EXP, peek().line);

        // 将之前的表达式作为左子树
        newNode->addChild(finalNode);

        // 处理运算符
        std::shared_ptr<TreeNode> opNode;
        if (check("EQL")) {
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "==");
        } else {
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "!=");
        }
        advance(); // 消费运算符
        newNode->addChild(opNode);

        // 解析右边的 RelExp
        auto rightExpr = parseRelExp();
        newNode->addChild(rightExpr);

        // 更新最终节点
        finalNode = newNode;
    }

    // 输出最终的 EqExp
    outputNonTerminal("EqExp");
    return finalNode;
}

// 逻辑与表达式 LAndExp → EqExp | LAndExp '&&' EqExp
std::shared_ptr<TreeNode> Parser::parseLAndExp() {
    auto initialLAndExp = std::make_shared<TreeNode>(NodeType::LAND_EXP, peek().line);
    auto firstEqExp = parseEqExp();
    initialLAndExp->addChild(firstEqExp);

    auto finalNode = initialLAndExp;

    // 处理连续的 && 运算
    while (check("AND") || peek().lexeme == "&") {
        // 输出当前的 LAndExp
        outputNonTerminal("LAndExp");

        // 创建新的 LAndExp 节点
        auto newNode = std::make_shared<TreeNode>(NodeType::LAND_EXP, peek().line);

        // 将之前的表达式作为左子树
        newNode->addChild(finalNode);

        // 处理运算符
        std::shared_ptr<TreeNode> opNode;
        if (check("AND")) {
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "&&");
        } else {
            // 处理错误的单个 & 符号
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "&&");
            // 这里可以记录词法错误，但根据文法说明，词法分析阶段已经处理了
        }
        advance(); // 消费运算符
        newNode->addChild(opNode);

        // 解析右边的 EqExp
        auto rightExpr = parseEqExp();
        newNode->addChild(rightExpr);

        // 更新最终节点
        finalNode = newNode;
    }

    // 输出最终的 LAndExp
    outputNonTerminal("LAndExp");
    return finalNode;
}

// 逻辑或表达式 LOrExp → LAndExp | LOrExp '||' LAndExp
std::shared_ptr<TreeNode> Parser::parseLOrExp() {
    // 1. 创建初始的 LOrExp 节点
    auto initialLOrExp = std::make_shared<TreeNode>(NodeType::LOR_EXP, peek().line);

    // 2. 解析下一级 LAndExp 并添加为子节点
    auto firstLAndExp = parseLAndExp();
    initialLOrExp->addChild(firstLAndExp);

    // 3. 将初始节点设为当前最终节点
    auto finalNode = initialLOrExp;

    // 处理连续的 || 运算
    while (check("OR") || peek().lexeme == "|") {
        // 输出当前的 LOrExp
        outputNonTerminal("LOrExp");

        // 创建新的 LOrExp 节点
        auto newNode = std::make_shared<TreeNode>(NodeType::LOR_EXP, peek().line);

        // 将之前的表达式作为左子树
        newNode->addChild(finalNode);

        // 处理运算符
        std::shared_ptr<TreeNode> opNode;
        if (check("OR")) {
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "||");
        } else {
            // 处理错误的单个 | 符号
            opNode = std::make_shared<TreeNode>(NodeType::TERMINAL, peek().line, "||");
            // 这里可以记录词法错误，但根据文法说明，词法分析阶段已经处理了
        }
        advance(); // 消费运算符
        newNode->addChild(opNode);

        // 解析右边的 LAndExp
        auto rightExpr = parseLAndExp();
        newNode->addChild(rightExpr);

        // 更新最终节点
        finalNode = newNode;
    }

    // 输出最终的 LOrExp
    outputNonTerminal("LOrExp");
    return finalNode;
}

// 常量表达式 ConstExp → AddExp
std::shared_ptr<TreeNode> Parser::parseConstExp() {
    auto node = std::make_shared<TreeNode>(NodeType::CONST_EXP, peek().line);
    node->addChild(parseAddExp());
    outputNonTerminal("ConstExp");
    return node;
}

void Parser::parse() {
    root = parseCompUnit();
}

bool Parser::hasError() const {
    return !syntaxErrors.empty() || !lexErrors.empty();
}

void Parser::writeOutput(const std::string& successFile, const std::string& errorFile)
{
    if (hasError()) {
        std::ofstream fout(errorFile);
        std::vector<std::pair<int, std::string>> allErrors;

        // 合并词法错误和语法错误
        for (const auto& e : lexErrors) {
            allErrors.push_back({e.line, e.code});
        }
        for (const auto& e : syntaxErrors) {
            allErrors.push_back({e.line, e.code});
        }

        // 按行号排序
        std::sort(allErrors.begin(), allErrors.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        for (const auto& e : allErrors) {
            fout << e.first << " " << e.second << "\n";
        }
        fout.close();
    } else {
        std::ofstream fout(successFile);
        enableOutput(fout);

        // 重新分析并输出语法成分
        // 重置分析状态
        currentTokenIndex = 0;    // 重置token索引，从头开始分析
        syntaxErrors.clear();     // 清空之前的语法错误

        // 重新执行语法分析，这次会输出到文件
        root = parseCompUnit();   // 重新构建语法树，过程中输出语法成分

        // 禁用输出并关闭文件
        disableOutput();
        fout.close();
    }
}

// [Parser.cpp]

// ... (文件顶部所有的 #include 和现有函数实现)

// ... (包括 writeOutput 函数的完整实现)


// [新增] 在文件末尾添加 printAst 函数的实现
void Parser::printAst(const std::string& filename) const {
    std::ofstream fout(filename);
    if (!fout.is_open()) {
        // 如果文件打开失败，在标准错误输出提示
        std::cerr << "Error: Could not open file " << filename << " for writing AST." << std::endl;
        return;
    }

    if (root) { //
        // 调用 TreeNode 的 print 方法，将根节点写入文件流
        root->print(fout, 0); //
    } else {
        fout << "AST root is null. Did parse() run successfully?" << std::endl;
    }

    fout.close();
}