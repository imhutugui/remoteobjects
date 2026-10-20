#include "lexer.h"

#include <cctype>

namespace rep {

Lexer::Lexer(std::string_view text) : text_(text)
{
    tokenize();
}

void Lexer::tokenize()
{
    while (pos_ < text_.size() && error_.empty()) {
        const char c = text_[pos_];

        if (c == '\n') {
            ++line_;
            ++pos_;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++pos_;
            continue;
        }

        // Comments.
        if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
            while (pos_ < text_.size() && text_[pos_] != '\n')
                ++pos_;
            continue;
        }
        if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
            pos_ += 2;
            while (pos_ + 1 < text_.size() && !(text_[pos_] == '*' && text_[pos_ + 1] == '/')) {
                if (text_[pos_] == '\n')
                    ++line_;
                ++pos_;
            }
            if (pos_ + 1 >= text_.size()) {
                error_ = "unterminated block comment";
                return;
            }
            pos_ += 2;
            continue;
        }

        // Preprocessor directives (#include, #pragma ...) are ignored.
        if (c == '#') {
            while (pos_ < text_.size() && text_[pos_] != '\n')
                ++pos_;
            continue;
        }

        // String literals are kept verbatim; only string default values use them.
        if (c == '"') {
            const size_t start = pos_++;
            while (pos_ < text_.size() && text_[pos_] != '"') {
                if (text_[pos_] == '\\')
                    ++pos_;
                ++pos_;
            }
            if (pos_ >= text_.size()) {
                error_ = "unterminated string literal";
                return;
            }
            ++pos_;
            tokens_.push_back({TokenKind::String, std::string(text_.substr(start, pos_ - start)),
                               line_});
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const size_t start = pos_;
            while (pos_ < text_.size()
                   && (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_'))
                ++pos_;
            tokens_.push_back(
                {TokenKind::Identifier, std::string(text_.substr(start, pos_ - start)), line_});
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
            const size_t start = pos_;
            if (c == '-')
                ++pos_;
            while (pos_ < text_.size()
                   && (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.'
                       || text_[pos_] == 'x' || text_[pos_] == 'X'))
                ++pos_;
            tokens_.push_back(
                {TokenKind::Number, std::string(text_.substr(start, pos_ - start)), line_});
            continue;
        }

        if (c == ':' && pos_ + 1 < text_.size() && text_[pos_ + 1] == ':') {
            pos_ += 2;
            tokens_.push_back({TokenKind::Punct, "::", line_});
            continue;
        }

        if (std::string_view("{}()[],;=<>:&*+").find(c) != std::string_view::npos) {
            ++pos_;
            tokens_.push_back({TokenKind::Punct, std::string(1, c), line_});
            continue;
        }

        error_ = "unexpected character '" + std::string(1, c) + "' on line "
                 + std::to_string(line_);
        return;
    }

    tokens_.push_back({TokenKind::End, "", line_});
}

}  // namespace rep
