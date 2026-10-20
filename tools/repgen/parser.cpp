#include "parser.h"

#include <cctype>

#include "lexer.h"

namespace rep {

namespace {

bool isModifier(const Token &token, PropertyModifier &modifier)
{
    if (token.text == "CONSTANT") {
        modifier = PropertyModifier::Constant;
        return true;
    }
    if (token.text == "READONLY") {
        modifier = PropertyModifier::ReadOnly;
        return true;
    }
    if (token.text == "READWRITE") {
        modifier = PropertyModifier::ReadWrite;
        return true;
    }
    if (token.text == "READPUSH") {
        modifier = PropertyModifier::ReadPush;
        return true;
    }
    if (token.text == "SOURCEONLYSETTER") {
        modifier = PropertyModifier::SourceOnlySetter;
        return true;
    }
    return false;
}

// Splits tokens on top level commas (ignores commas inside <> or ()).
std::vector<std::vector<Token>> splitArguments(const std::vector<Token> &tokens)
{
    std::vector<std::vector<Token>> out;
    std::vector<Token> current;
    int depth = 0;
    for (const auto &token : tokens) {
        if (token.isPunct('<') || token.isPunct('('))
            ++depth;
        else if (token.isPunct('>') || token.isPunct(')'))
            --depth;
        if (depth == 0 && token.isPunct(',')) {
            out.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(token);
    }
    if (!current.empty())
        out.push_back(current);
    return out;
}

int findOpenParen(const std::vector<Token> &tokens)
{
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].isPunct('('))
            return static_cast<int>(i);
    }
    return -1;
}

std::vector<Token> insideParens(const std::vector<Token> &tokens, int openIndex)
{
    std::vector<Token> out;
    int depth = 0;
    for (size_t i = static_cast<size_t>(openIndex); i < tokens.size(); ++i) {
        if (tokens[i].isPunct('(')) {
            ++depth;
            if (depth == 1)
                continue;
        } else if (tokens[i].isPunct(')')) {
            --depth;
            if (depth == 0)
                break;
        }
        out.push_back(tokens[i]);
    }
    return out;
}

void splitArgument(const std::vector<Token> &tokens, Argument &argument)
{
    if (tokens.size() >= 2 && tokens.back().kind == TokenKind::Identifier) {
        argument.name = tokens.back().text;
        argument.type.name = normalizeType(std::vector<Token>(tokens.begin(), tokens.end() - 1));
    } else {
        argument.type.name = normalizeType(tokens);
    }
}

}  // namespace

std::string normalizeType(const std::vector<Token> &tokens)
{
    std::string out;
    for (const auto &token : tokens) {
        if (token.text == "const")
            continue;
        const bool tight = token.isPunct('<') || token.isPunct('>') || token.text == "::"
                           || token.isPunct(',');
        if (!tight && !out.empty() && out.back() != '<' && out.back() != ','
            && out.back() != ':')
            out += ' ';
        out += token.text;
    }
    return out;
}

bool Parser::fail(const std::string &message, int line)
{
    error_ = "line " + std::to_string(line) + ": " + message;
    return false;
}

const Token &Parser::peek(size_t ahead) const
{
    const size_t index = index_ + ahead;
    if (index >= tokens_.size())
        return tokens_.back();
    return tokens_[index];
}

void Parser::advance()
{
    if (index_ < tokens_.size() - 1)
        ++index_;
}

bool Parser::accept(const char *text)
{
    if (peek().text == text) {
        advance();
        return true;
    }
    return false;
}

bool Parser::acceptPunct(char c)
{
    if (peek().isPunct(c)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::skipBalanced(const char *what)
{
    advance();  // keyword
    if (!acceptPunct('('))
        return fail(std::string("expected '(' after ") + what, peek().line);
    int depth = 1;
    while (peek().kind != TokenKind::End && depth > 0) {
        if (peek().isPunct('('))
            ++depth;
        else if (peek().isPunct(')'))
            --depth;
        advance();
    }
    acceptPunct(';');
    return true;
}

bool Parser::parse(std::string_view text, File &file, std::string &error)
{
    Lexer lexer(text);
    if (!lexer.ok()) {
        error = lexer.error();
        return false;
    }
    tokens_ = lexer.tokens();
    index_ = 0;
    error_.clear();

    if (!parseTopLevel(file)) {
        error = error_;
        return false;
    }
    return true;
}

bool Parser::parseTopLevel(File &file)
{
    while (peek().kind != TokenKind::End) {
        const Token &token = peek();
        if (token.is("class")) {
            ClassDecl cls;
            if (!parseClass(cls))
                return false;
            file.classes.push_back(std::move(cls));
        } else if (token.is("POD")) {
            PodDecl pod;
            if (!parsePod(pod))
                return false;
            file.pods.push_back(std::move(pod));
        } else if (token.is("ENUM")) {
            EnumDecl decl;
            if (!parseEnum(decl))
                return false;
            file.enums.push_back(std::move(decl));
        } else if (token.is("USE_ENUM") || token.is("FLAG")) {
            if (!skipBalanced(token.text.c_str()))
                return false;
        } else if (token.is("MODEL")) {
            return fail("MODEL declarations are not supported by repgen", token.line);
        } else if (token.is("CLASS")) {
            return fail("CLASS(child) child replication is not supported by repgen", token.line);
        } else if (token.isPunct(';')) {
            advance();
        } else {
            return fail("unexpected token '" + token.text + "'", token.line);
        }
    }
    return true;
}

bool Parser::parseClass(ClassDecl &out)
{
    advance();  // class
    out.line = peek().line;

    if (peek().kind != TokenKind::Identifier)
        return fail("expected class name", peek().line);
    if (peek(1).kind == TokenKind::Identifier) {
        out.attribute = peek().text;
        advance();
    }
    out.name = peek().text;
    advance();

    if (!acceptPunct('{'))
        return fail("expected '{' after class " + out.name, peek().line);

    while (true) {
        const Token &token = peek();
        if (token.kind == TokenKind::End)
            return fail("unterminated class " + out.name, token.line);
        if (token.isPunct('}')) {
            advance();
            break;
        }
        if (token.isPunct(';')) {
            advance();
            continue;
        }
        if (token.is("PROP")) {
            advance();
            Property property;
            if (!parseProperty(property))
                return false;
            out.properties.push_back(std::move(property));
        } else if (token.is("SIGNAL")) {
            advance();
            SignalDecl signal;
            if (!parseSignal(signal))
                return false;
            out.signals.push_back(std::move(signal));
        } else if (token.is("SLOT")) {
            advance();
            SlotDecl slot;
            if (!parseSlot(slot))
                return false;
            out.slots.push_back(std::move(slot));
        } else if (token.is("ENUM")) {
            EnumDecl decl;
            if (!parseEnum(decl))
                return false;
            out.enums.push_back(std::move(decl));
        } else if (token.is("CLASS")) {
            return fail("child replication (CLASS) is not supported", token.line);
        } else if (token.is("MODEL")) {
            return fail("MODEL is not supported", token.line);
        } else {
            return fail("unexpected token '" + token.text + "' in class " + out.name, token.line);
        }
    }
    acceptPunct(';');
    return true;
}

bool Parser::parseProperty(Property &out)
{
    if (!acceptPunct('('))
        return fail("expected '(' after PROP", peek().line);

    std::vector<Token> inner;
    int depth = 1;
    while (peek().kind != TokenKind::End) {
        if (peek().isPunct('('))
            ++depth;
        else if (peek().isPunct(')')) {
            --depth;
            if (depth == 0) {
                advance();
                break;
            }
        }
        inner.push_back(peek());
        advance();
    }
    if (inner.empty())
        return fail("empty PROP declaration", peek().line);

    out.line = peek().line;

    // Trailing modifier.
    PropertyModifier modifier = PropertyModifier::ReadWrite;
    if (isModifier(inner.back(), modifier)) {
        out.modifier = modifier;
        inner.pop_back();
    }

    // Optional default value.
    for (size_t i = 0; i < inner.size(); ++i) {
        if (inner[i].isPunct('=')) {
            std::string value;
            for (size_t j = i + 1; j < inner.size(); ++j)
                value += inner[j].text;
            out.defaultValue = value;
            inner.resize(i);
            break;
        }
    }

    if (inner.size() < 2 || inner.back().kind != TokenKind::Identifier)
        return fail("PROP needs a type and a name", peek().line);
    out.name = inner.back().text;
    out.type.name = normalizeType(std::vector<Token>(inner.begin(), inner.end() - 1));
    if (out.type.name.empty())
        return fail("PROP '" + out.name + "' has no type", peek().line);

    acceptPunct(';');
    return true;
}

bool Parser::parseSignal(SignalDecl &out)
{
    if (!acceptPunct('('))
        return fail("expected '(' after SIGNAL", peek().line);

    std::vector<Token> inner;
    int depth = 1;
    while (peek().kind != TokenKind::End) {
        if (peek().isPunct('('))
            ++depth;
        else if (peek().isPunct(')')) {
            --depth;
            if (depth == 0) {
                advance();
                break;
            }
        }
        inner.push_back(peek());
        advance();
    }

    const int open = findOpenParen(inner);
    if (open <= 0)
        return fail("SIGNAL must look like SIGNAL(name(args))", peek().line);
    out.name = inner[static_cast<size_t>(open - 1)].text;
    out.line = peek().line;

    const auto args = insideParens(inner, open);
    for (const auto &group : splitArguments(args)) {
        Argument argument;
        splitArgument(group, argument);
        if (argument.type.name.empty())
            return fail("SIGNAL " + out.name + " has an argument without a type", out.line);
        out.arguments.push_back(std::move(argument));
    }

    acceptPunct(';');
    return true;
}

bool Parser::parseSlot(SlotDecl &out)
{
    if (!acceptPunct('('))
        return fail("expected '(' after SLOT", peek().line);

    std::vector<Token> inner;
    int depth = 1;
    while (peek().kind != TokenKind::End) {
        if (peek().isPunct('('))
            ++depth;
        else if (peek().isPunct(')')) {
            --depth;
            if (depth == 0) {
                advance();
                break;
            }
        }
        inner.push_back(peek());
        advance();
    }

    const int open = findOpenParen(inner);
    if (open <= 0)
        return fail("SLOT must look like SLOT(returnType name(args))", peek().line);

    out.name = inner[static_cast<size_t>(open - 1)].text;
    out.line = peek().line;
    out.returnType.name = normalizeType(std::vector<Token>(inner.begin(), inner.begin() + open - 1));
    if (out.returnType.name.empty())
        return fail("SLOT " + out.name + " has no return type", out.line);

    const auto args = insideParens(inner, open);
    for (const auto &group : splitArguments(args)) {
        Argument argument;
        splitArgument(group, argument);
        if (argument.type.name.empty())
            return fail("SLOT " + out.name + " has an argument without a type", out.line);
        out.arguments.push_back(std::move(argument));
    }

    acceptPunct(';');
    return true;
}

bool Parser::parseEnum(EnumDecl &out)
{
    advance();  // ENUM
    out.line = peek().line;
    accept("class");

    if (peek().kind != TokenKind::Identifier)
        return fail("expected enum name", peek().line);
    out.name = peek().text;
    advance();

    if (acceptPunct(':')) {
        std::vector<Token> typeTokens;
        while (peek().kind != TokenKind::End && !peek().isPunct('{')) {
            typeTokens.push_back(peek());
            advance();
        }
        out.underlyingType = normalizeType(typeTokens);
    }

    if (!acceptPunct('{'))
        return fail("expected '{' after enum " + out.name, peek().line);

    while (peek().kind != TokenKind::End && !peek().isPunct('}')) {
        if (acceptPunct(',')) {
            continue;
        }
        if (peek().kind != TokenKind::Identifier)
            return fail("expected enum value in " + out.name, peek().line);
        std::string symbol = peek().text;
        advance();
        std::string value;
        if (acceptPunct('=')) {
            value = peek().text;
            advance();
        }
        out.values.emplace_back(std::move(symbol), std::move(value));
    }

    if (!acceptPunct('}'))
        return fail("unterminated enum " + out.name, peek().line);
    acceptPunct(';');
    return true;
}

bool Parser::parsePod(PodDecl &out)
{
    advance();  // POD
    out.line = peek().line;

    if (peek().kind != TokenKind::Identifier)
        return fail("expected POD name", peek().line);
    if (peek(1).kind == TokenKind::Identifier)
        advance();  // attribute
    out.name = peek().text;
    advance();

    if (!acceptPunct('('))
        return fail("expected '(' after POD " + out.name, peek().line);

    std::vector<Token> inner;
    int depth = 1;
    while (peek().kind != TokenKind::End) {
        if (peek().isPunct('('))
            ++depth;
        else if (peek().isPunct(')')) {
            --depth;
            if (depth == 0) {
                advance();
                break;
            }
        }
        inner.push_back(peek());
        advance();
    }

    for (const auto &group : splitArguments(inner)) {
        Argument field;
        splitArgument(group, field);
        if (field.type.name.empty() || field.name.empty())
            return fail("POD " + out.name + " fields need a type and a name", out.line);
        out.fields.push_back(std::move(field));
    }

    acceptPunct(';');
    return true;
}

}  // namespace rep
