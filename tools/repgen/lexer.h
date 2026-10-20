// repgen - tokenizer for the .rep interface description language.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rep {

enum class TokenKind { End, Identifier, Number, String, Punct };

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    int line = 0;

    bool is(const char *value) const { return text == value; }
    bool isPunct(char c) const { return kind == TokenKind::Punct && text.size() == 1 && text[0] == c; }
};

class Lexer {
public:
    explicit Lexer(std::string_view text);

    bool ok() const { return error_.empty(); }
    const std::string &error() const { return error_; }
    const std::vector<Token> &tokens() const { return tokens_; }

private:
    void tokenize();

    std::string_view text_;
    std::vector<Token> tokens_;
    std::string error_;
    size_t pos_ = 0;
    int line_ = 1;
};

}  // namespace rep
