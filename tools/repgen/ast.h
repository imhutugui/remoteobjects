// repgen - abstract syntax tree for .rep files.
#pragma once

#include <string>
#include <vector>

namespace rep {

struct TypeRef {
    std::string name;  // normalized, e.g. "QList<int>", "const int"
};

struct Argument {
    TypeRef type;
    std::string name;
};

enum class PropertyModifier { ReadWrite, ReadOnly, Constant, ReadPush, SourceOnlySetter };

struct Property {
    TypeRef type;
    std::string name;
    PropertyModifier modifier = PropertyModifier::ReadWrite;
    std::string defaultValue;  // raw literal, empty when absent
    int line = 0;
};

struct SignalDecl {
    std::string name;
    std::vector<Argument> arguments;
    int line = 0;
};

struct SlotDecl {
    TypeRef returnType;
    std::string name;
    std::vector<Argument> arguments;
    int line = 0;
};

struct EnumDecl {
    std::string name;
    std::string underlyingType;  // e.g. "int"
    std::vector<std::pair<std::string, std::string>> values;  // symbol, value literal
    int line = 0;
};

struct PodDecl {
    std::string name;
    std::vector<Argument> fields;
    int line = 0;
};

struct ClassDecl {
    std::string name;
    std::string attribute;
    std::vector<EnumDecl> enums;
    std::vector<Property> properties;
    std::vector<SignalDecl> signals;
    std::vector<SlotDecl> slots;
    int line = 0;
};

struct File {
    std::string name;
    std::vector<EnumDecl> enums;
    std::vector<PodDecl> pods;
    std::vector<ClassDecl> classes;
};

}  // namespace rep
