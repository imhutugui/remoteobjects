// repgen - recursive descent parser for .rep files.
#pragma once

#include <string>
#include <string_view>

#include "ast.h"
#include "lexer.h"

namespace rep {

class Parser {
public:
    bool parse(std::string_view text, File &file, std::string &error);
    const std::string &error() const { return error_; }

private:
    bool fail(const std::string &message, int line);
    const Token &peek(size_t ahead = 0) const;
    bool accept(const char *text);
    bool acceptPunct(char c);
    void advance();

    bool parseTopLevel(File &file);
    bool parseClass(ClassDecl &out);
    bool parsePod(PodDecl &out);
    bool parseEnum(EnumDecl &out);
    bool parseProperty(Property &out);
    bool parseSignal(SignalDecl &out);
    bool parseSlot(SlotDecl &out);
    bool parseArgumentList(std::vector<Argument> &out);
    bool skipBalanced(const char *what);

    std::vector<Token> tokens_;
    size_t index_ = 0;
    std::string error_;
};

// Rebuilds a normalized type string from tokens ("QList < int >" -> "QList<int>").
std::string normalizeType(const std::vector<Token> &tokens);

}  // namespace rep
