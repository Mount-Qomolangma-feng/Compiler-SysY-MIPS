#include "Utils.h"

bool isIdentStart(char c) {
    return (c == '_' || std::isalpha(static_cast<unsigned char>(c)));
}

bool isIdentChar(char c) {
    return (c == '_' || std::isalnum(static_cast<unsigned char>(c)));
}

bool isDigitChar(char c) {
    return std::isdigit(static_cast<unsigned char>(c));
}
