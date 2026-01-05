#include "Lexer.h"
#include "Utils.h"
#include <fstream>
#include <algorithm>
#include <iostream>

Lexer::Lexer(const std::string &input)
        : content(input), pos(0), line(1) {
    keywords = {
            {"const","CONSTTK"}, {"int","INTTK"}, {"static","STATICTK"},
            {"break","BREAKTK"}, {"continue","CONTINUETK"}, {"if","IFTK"},
            {"else","ELSETK"}, {"for","FORTK"}, {"return","RETURNTK"},
            {"void","VOIDTK"}, {"main","MAINTK"}, {"printf","PRINTFTK"}
    };
    singleSym = {
            {'*',"MULT"},{';',"SEMICN"},{',',"COMMA"},
            {'(',"LPARENT"},{')',"RPARENT"},{'[',"LBRACK"},{']',"RBRACK"},
            {'{',"LBRACE"},{'}',"RBRACE"},{'+',"PLUS"},{'-',"MINU"},
            {'%',"MOD"},{'!',"NOT"},{'<',"LSS"},{'>',"GRE"},{'=',"ASSIGN"},{'/',"DIV"}
    };
    multiSym = {
            {"&&","AND"},{"||","OR"},{"<=","LEQ"},{">=","GEQ"},{"==","EQL"},{"!=","NEQ"}
    };
}

char Lexer::peek(size_t offset) const {
    return (pos + offset < content.size()) ? content[pos + offset] : '\0';
}

void Lexer::pushToken(const std::string &code, const std::string &lex) {
    tokens.push_back({code, lex, line});
}

void Lexer::pushError(int errLine, const std::string &errCode) {
    errors.push_back({errLine, errCode});
}

void Lexer::skipWhitespace() {
    while (pos < content.size() && isspace(static_cast<unsigned char>(content[pos]))) {
        if (content[pos] == '\n') ++line;
        ++pos;
    }
}

void Lexer::handleComment() {
    if (peek(1) == '/') {
        pos += 2;
        while (pos < content.size() && content[pos] != '\n') ++pos;
    } else if (peek(1) == '*') {
        pos += 2;
        bool closed = false;
        while (pos + 1 < content.size()) {
            if (content[pos] == '*' && content[pos + 1] == '/') {
                pos += 2; closed = true; break;
            }
            if (content[pos] == '\n') ++line;
            ++pos;
        }
        if (!closed) pushError(line, "a");
    } else {
        pushToken("DIV", "/");
        ++pos;
    }
}

void Lexer::handleString() {
    int startLine = line;
    std::string lex = "\"";
    ++pos;
    bool closed = false;

    while (pos < content.size()) {
        char ch = content[pos];
        if (ch == '\\') {
            if (pos + 1 < content.size()) {
                lex.push_back('\\');
                lex.push_back(content[pos + 1]);
                pos += 2;
            } else { pushError(startLine, "a"); break; }
        } else if (ch == '"') {
            lex.push_back('"'); ++pos; closed = true; break;
        } else {
            if (ch == '\n') ++line;
            lex.push_back(ch);
            ++pos;
        }
    }
    if (!closed) pushError(startLine, "a");
    else pushToken("STRCON", lex);
}

void Lexer::handleIdentifier() {
    int startLine = line;
    std::string lex;
    while (pos < content.size() && isIdentChar(content[pos])) {
        lex.push_back(content[pos]);
        ++pos;
    }
    if (keywords.count(lex)) pushToken(keywords[lex], lex);
    else pushToken("IDENFR", lex);
}

void Lexer::handleNumber() {
    int startLine = line;
    std::string lex;
    while (pos < content.size() && isDigitChar(content[pos])) {
        lex.push_back(content[pos]);
        ++pos;
    }
    pushToken("INTCON", lex);
}

void Lexer::handleOperator() {
    char c = content[pos];
    char n = peek(1);
    std::string two; two.push_back(c); two.push_back(n);
    if (multiSym.count(two)) {
        pushToken(multiSym[two], two);
        pos += 2;
    } else {
        if (c == '&' || c == '|') {
            pushError(line, "a");
            ++pos;
        } else if (singleSym.count(c)) {
            pushToken(singleSym[c], std::string(1, c));
            ++pos;
        } else {
            pushError(line, "a");
            ++pos;
        }
    }
}

void Lexer::handleSingleChar() {
    char c = content[pos];
    if (singleSym.count(c)) pushToken(singleSym[c], std::string(1, c));
    else pushError(line, "a");
    ++pos;
}

void Lexer::analyze() {
    while (pos < content.size()) {
        skipWhitespace();
        if (pos >= content.size()) break;
        char c = content[pos];
        if (c == '/') handleComment();
        else if (c == '"') handleString();
        else if (isIdentStart(c)) handleIdentifier();
        else if (isDigitChar(c)) handleNumber();
        else if (c == '&' || c == '|' || c == '<' || c == '>' || c == '=' || c == '!') handleOperator();
        else handleSingleChar();
    }
}

bool Lexer::hasError() const {
    return !errors.empty();
}

void Lexer::writeOutput(const std::string &successFile, const std::string &errorFile) const {
    if (hasError()) {
        std::ofstream fout(errorFile);
        std::vector<LexError> sorted = errors;
        std::sort(sorted.begin(), sorted.end(), [](auto &a, auto &b){ return a.line < b.line; });
        for (auto &e : sorted)
            fout << e.line << " " << e.code << "\n";
        fout.close();
    } else {
        std::ofstream fout(successFile);
        for (auto &t : tokens)
            fout << t.code << " " << t.lexeme << "\n";
        fout.close();
    }
}
