cmake_minimum_required(VERSION 3.16)
project(Compiler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置 include 目录
include_directories(include)

# 收集源码文件
file(GLOB SOURCES src/*.cpp)

# 生成可执行文件 Compiler
add_executable(Compiler ${SOURCES}
        include/Parser.h
        src/Parser.cpp
        include/TreeNode.h
        include/Parser.h
        src/Parser.cpp
        include/SymbolTable.h
        include/SemanticAnalyzer.h
        src/SemanticAnalyzer.cpp
        include/IR.h
        include/IRGenerator.h
        src/IRGenerator.cpp
        include/IR.h
        include/IRGenerator.h
        src/IRGenerator.cpp
        include/MIPS.h
        include/MIPSGenerator.h
        src/MIPSGenerator.cpp
        include/OptimizerStructs.h
        include/Optimizer.h
        src/Optimizer.cpp)
