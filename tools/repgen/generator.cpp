#include "generator.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace rep {

namespace {

class TypeMapper {
public:
    explicit TypeMapper(const File &file)
    {
        for (const auto &e : file.enums)
            topLevelEnums_.insert(e.name);
        for (const auto &p : file.pods)
            pods_.insert(p.name);
        for (const auto &c : file.classes) {
            for (const auto &e : c.enums)
                classEnums_.insert(e.name);
        }
    }

    // Maps a .rep type to a C++ type. `className` is empty when the type is
    // used outside any generated class (POD fields, global enums).
    bool map(const std::string &repType, const std::string &className, std::string &out,
             std::string &error) const
    {
        const std::string type = normalize(repType);
        if (type.empty()) {
            error = "empty type";
            return false;
        }

        // Containers.
        const size_t open = type.find('<');
        if (open != std::string::npos && type.back() == '>') {
            const std::string outer = shortName(type.substr(0, open));
            const std::string inner = type.substr(open + 1, type.size() - open - 2);
            if (outer == "QList" || outer == "QVector" || outer == "QLinkedList" || outer == "vector") {
                std::string item;
                if (!map(inner, className, item, error))
                    return false;
                out = "std::vector<" + item + ">";
                return true;
            }
            if (outer == "QMap" || outer == "QHash" || outer == "map") {
                const auto parts = splitTopLevel(inner);
                if (parts.size() != 2) {
                    error = "expected two template arguments in '" + repType + "'";
                    return false;
                }
                std::string key;
                std::string value;
                if (!map(parts[0], className, key, error) || !map(parts[1], className, value, error))
                    return false;
                out = "std::map<" + key + ", " + value + ">";
                return true;
            }
            error = "unsupported container '" + repType + "'";
            return false;
        }

        const std::string name = shortName(type);

        // Scalars.
        static const std::map<std::string, std::string> scalars = {
            {"bool", "bool"},
            {"int", "int32_t"},
            {"qint32", "int32_t"},
            {"int32_t", "int32_t"},
            {"signed", "int32_t"},
            {"short", "int32_t"},
            {"qint16", "int32_t"},
            {"uint", "uint32_t"},
            {"quint32", "uint32_t"},
            {"uint32_t", "uint32_t"},
            {"ushort", "uint32_t"},
            {"quint16", "uint32_t"},
            {"unsignedint", "uint32_t"},
            {"unsigned", "uint32_t"},
            {"longlong", "int64_t"},
            {"qlonglong", "int64_t"},
            {"qint64", "int64_t"},
            {"int64_t", "int64_t"},
            {"long", "int64_t"},
            {"ulonglong", "uint64_t"},
            {"qulonglong", "uint64_t"},
            {"quint64", "uint64_t"},
            {"uint64_t", "uint64_t"},
            {"unsignedlonglong", "uint64_t"},
            {"float", "float"},
            {"double", "double"},
            {"qreal", "double"},
            {"QString", "std::string"},
            {"QStringView", "std::string"},
            {"string", "std::string"},
            {"QByteArray", "std::string"},
            {"QVariant", "ro::Value"},
            {"void", "void"},
        };
        auto scalar = scalars.find(compact(type));
        if (scalar != scalars.end()) {
            out = scalar->second;
            return true;
        }

        if (topLevelEnums_.count(name)) {
            out = "::" + name;
            return true;
        }
        if (classEnums_.count(name)) {
            if (className.empty()) {
                error = "enum '" + name + "' is declared inside a class and cannot be used here";
                return false;
            }
            out = name;
            return true;
        }
        if (pods_.count(name)) {
            out = "::" + name;
            return true;
        }

        error = "unsupported type '" + repType + "'";
        return false;
    }

    bool isEnumType(const std::string &repType) const
    {
        const std::string name = shortName(normalize(repType));
        return topLevelEnums_.count(name) > 0 || classEnums_.count(name) > 0;
    }

private:
    static std::string compact(const std::string &type)
    {
        std::string out;
        for (char c : type) {
            if (c != ' ')
                out += c;
        }
        return out;
    }

    static std::string normalize(const std::string &type)
    {
        std::string out;
        std::istringstream stream(type);
        std::string word;
        while (stream >> word) {
            if (word == "const")
                continue;
            if (!out.empty())
                out += ' ';
            out += word;
        }
        // Drop references and pointers.
        while (!out.empty() && (out.back() == '&' || out.back() == '*'))
            out.pop_back();
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
        return out;
    }

    static std::string shortName(const std::string &type)
    {
        const size_t pos = type.rfind("::");
        return pos == std::string::npos ? type : type.substr(pos + 2);
    }

    static std::vector<std::string> splitTopLevel(const std::string &text)
    {
        std::vector<std::string> parts;
        std::string current;
        int depth = 0;
        for (char c : text) {
            if (c == '<')
                ++depth;
            else if (c == '>')
                --depth;
            if (c == ',' && depth == 0) {
                parts.push_back(current);
                current.clear();
                continue;
            }
            current += c;
        }
        if (!current.empty())
            parts.push_back(current);
        return parts;
    }

    std::set<std::string> topLevelEnums_;
    std::set<std::string> classEnums_;
    std::set<std::string> pods_;
};

std::string capitalize(const std::string &name)
{
    if (name.empty())
        return name;
    std::string out = name;
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

std::string zeroValueFor(const std::string &cppType)
{
    if (cppType == "bool")
        return "false";
    if (cppType == "float" || cppType == "double")
        return "0";
    if (cppType == "std::string")
        return "std::string()";
    return "{}";
}

std::string defaultValueFor(const Property &property, const std::string &cppType,
                            const TypeMapper &mapper, const std::string &className)
{
    const std::string &literal = property.defaultValue;
    if (literal.empty())
        return zeroValueFor(cppType);
    if (literal == "true" || literal == "false")
        return literal;

    std::string mapped;
    std::string ignored;
    const bool isEnum = mapper.map(property.type.name, className, mapped, ignored)
                        && mapper.isEnumType(property.type.name);
    const bool looksNumeric = !literal.empty()
                              && (std::isdigit(static_cast<unsigned char>(literal[0]))
                                  || literal[0] == '-' || literal[0] == '.');

    if (isEnum && !looksNumeric && literal.front() != '"') {
        std::string typeName = mapped;
        if (typeName.rfind("::", 0) == 0)
            typeName = typeName.substr(2);
        return typeName + "::" + literal;
    }
    if (cppType == "std::string")
        return literal;  // already quoted in the .rep file
    return literal;
}

std::string cppParameterList(const std::vector<Argument> &arguments, const TypeMapper &mapper,
                             const std::string &className, std::string &error)
{
    std::string out;
    for (size_t i = 0; i < arguments.size(); ++i) {
        std::string cpp;
        if (!mapper.map(arguments[i].type.name, className, cpp, error))
            return {};
        if (i)
            out += ", ";
        out += cpp;
        if (!arguments[i].name.empty())
            out += " " + arguments[i].name;
    }
    return out;
}

std::string cppArgumentNames(const std::vector<Argument> &arguments)
{
    std::string out;
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i)
            out += ", ";
        out += arguments[i].name.empty() ? ("arg" + std::to_string(i)) : arguments[i].name;
    }
    return out;
}

bool cppArgumentTypes(const std::vector<Argument> &arguments, const TypeMapper &mapper,
                      const std::string &className, std::string &out, std::string &error)
{
    out.clear();
    for (size_t i = 0; i < arguments.size(); ++i) {
        std::string cpp;
        if (!mapper.map(arguments[i].type.name, className, cpp, error))
            return false;
        if (i)
            out += ", ";
        out += cpp;
    }
    return true;
}

struct Emitter {
    std::ostringstream out;

    void line(const std::string &text = std::string()) { out << text << '\n'; }
};

bool emitEnums(Emitter &e, const std::vector<EnumDecl> &enums, const std::string &indent,
               std::string &error)
{
    for (const auto &decl : enums) {
        std::string underlying = "int32_t";
        if (!decl.underlyingType.empty()) {
            underlying = decl.underlyingType;
            if (underlying == "int" || underlying == "qint32")
                underlying = "int32_t";
            else if (underlying == "uint" || underlying == "quint32")
                underlying = "uint32_t";
        }
        e.line(indent + "enum class " + decl.name + " : " + underlying + " {");
        long nextValue = 0;
        for (size_t i = 0; i < decl.values.size(); ++i) {
            const auto &value = decl.values[i];
            std::string literal;
            if (!value.second.empty()) {
                literal = " = " + value.second;
                nextValue = std::strtol(value.second.c_str(), nullptr, 0) + 1;
            } else {
                literal = " = " + std::to_string(nextValue++);
            }
            e.line(indent + "    " + value.first + literal
                   + (i + 1 == decl.values.size() ? "" : ","));
        }
        e.line(indent + "};");
        if (indent.empty()) {
            // Keeps .rep snippets such as `PodT(QList<TestEnum::Test>)` compiling.
            e.line(indent + "struct " + decl.name + "Enum { using " + decl.name + " = ::" + decl.name
                   + "; };");
        }
        e.line();
    }
    return true;
}

bool emitPods(Emitter &e, const File &file, const TypeMapper &mapper, std::string &error)
{
    for (const auto &pod : file.pods) {
        std::vector<std::string> types;
        for (const auto &field : pod.fields) {
            std::string cpp;
            if (!mapper.map(field.type.name, {}, cpp, error)) {
                error = "POD " + pod.name + ": " + error;
                return false;
            }
            types.push_back(cpp);
        }

        e.line("struct " + pod.name + " {");
        for (size_t i = 0; i < pod.fields.size(); ++i)
            e.line("    " + types[i] + " " + pod.fields[i].name + "{};");
        e.line();
        e.line("    ro::Value toValue() const;");
        e.line("    static " + pod.name + " fromValue(const ro::Value &value);");
        e.line("};");
        e.line();

        e.line("inline ro::Value " + pod.name + "::toValue() const");
        e.line("{");
        std::string fields;
        for (size_t i = 0; i < pod.fields.size(); ++i) {
            if (i)
                fields += ", ";
            fields += "ro::toValue(" + pod.fields[i].name + ")";
        }
        e.line("    return ro::Value::structure(\"" + pod.name + "\", {" + fields + "});");
        e.line("}");
        e.line();

        e.line("inline " + pod.name + " " + pod.name + "::fromValue(const ro::Value &value)");
        e.line("{");
        e.line("    " + pod.name + " out;");
        e.line("    const auto &fields = value.fields();");
        for (size_t i = 0; i < pod.fields.size(); ++i) {
            e.line("    if (fields.size() > " + std::to_string(i) + ")");
            e.line("        out." + pod.fields[i].name + " = ro::fromValue<" + types[i]
                   + ">(fields[" + std::to_string(i) + "]);");
        }
        e.line("    return out;");
        e.line("}");
        e.line();
    }
    return true;
}

bool emitSource(Emitter &e, const ClassDecl &cls, const TypeMapper &mapper, std::string &error)
{
    const std::string className = cls.name + "SimpleSource";

    e.line("// " + cls.name + ": properties are pushed to replicas on every set;");
    e.line("// slots are pure virtual and executed on the IO thread.");
    e.line("class " + className + " : public ro::SourceBase {");
    e.line("public:");
    e.line("    static constexpr const char *kTypeName = \"" + cls.name + "\";");
    e.line("    static constexpr const char *kSignature = \"@@SIGNATURE@@\";");
    e.line();

    if (!cls.enums.empty()) {
        if (!emitEnums(e, cls.enums, "    ", error))
            return false;
    }

    // Constructor.
    e.line("    " + className + "() : ro::SourceBase(kTypeName, kSignature)");
    e.line("    {");
    std::string propertyNames;
    for (size_t i = 0; i < cls.properties.size(); ++i) {
        if (i)
            propertyNames += ", ";
        propertyNames += "\"" + cls.properties[i].name + "\"";
    }
    if (!cls.properties.empty())
        e.line("        setPropertyNames({" + propertyNames + "});");
    for (size_t i = 0; i < cls.properties.size(); ++i)
        e.line("        " + cls.properties[i].name + "Changed.attachProperty(this, "
               + std::to_string(i) + ");");
    for (size_t i = 0; i < cls.signals.size(); ++i)
        e.line("        " + cls.signals[i].name + ".attachSignal(this, " + std::to_string(i)
               + ");");
    e.line("    }");
    e.line();

    // Properties.
    for (const auto &property : cls.properties) {
        std::string cpp;
        if (!mapper.map(property.type.name, cls.name, cpp, error)) {
            error = cls.name + "." + property.name + ": " + error;
            return false;
        }
        const std::string defaultInit =
            defaultValueFor(property, cpp, mapper, cls.name);
        e.line("    " + cpp + " " + property.name + "() const");
        e.line("    {");
        e.line("        std::lock_guard<std::mutex> lock(mutex_);");
        e.line("        return " + property.name + "_;");
        e.line("    }");
        e.line();
        if (property.modifier != PropertyModifier::ReadOnly
            && property.modifier != PropertyModifier::Constant) {
            e.line("    void set" + capitalize(property.name) + "(" + cpp + " value)");
            e.line("    {");
            e.line("        {");
            e.line("            std::lock_guard<std::mutex> lock(mutex_);");
            e.line("            " + property.name + "_ = value;");
            e.line("        }");
            e.line("        " + property.name + "Changed.emit(value);");
            e.line("    }");
            e.line();
            e.line("    // Kept for QtRO-style sources that emit the property signal directly.");
            e.line("    void emit" + capitalize(property.name) + "Changed(" + cpp + " value)");
            e.line("    {");
            e.line("        set" + capitalize(property.name) + "(value);");
            e.line("    }");
            e.line();
        }
        e.line("    ro::Signal<" + cpp + "> " + property.name + "Changed;");
        e.line();
    }

    // Signals.
    for (const auto &signal : cls.signals) {
        std::string types;
        if (!cppArgumentTypes(signal.arguments, mapper, cls.name, types, error)) {
            error = cls.name + "." + signal.name + ": " + error;
            return false;
        }
        e.line("    ro::Signal<" + types + "> " + signal.name + ";");
        e.line("    void emit" + capitalize(signal.name) + "("
               + cppParameterList(signal.arguments, mapper, cls.name, error) + ")");
        e.line("    {");
        e.line("        " + signal.name + ".emit(" + cppArgumentNames(signal.arguments) + ");");
        e.line("    }");
        e.line();
    }

    // Slots.
    for (const auto &slot : cls.slots) {
        std::string returnType;
        if (!mapper.map(slot.returnType.name, cls.name, returnType, error)) {
            error = cls.name + "." + slot.name + ": " + error;
            return false;
        }
        e.line("    virtual " + returnType + " " + slot.name + "("
               + cppParameterList(slot.arguments, mapper, cls.name, error) + ") = 0;");
    }
    if (!cls.slots.empty())
        e.line();

    // Property snapshot.
    e.line("    ro::Value::List propertyValues() const override");
    e.line("    {");
    e.line("        std::lock_guard<std::mutex> lock(mutex_);");
    std::string values;
    for (size_t i = 0; i < cls.properties.size(); ++i) {
        if (i)
            values += ", ";
        values += "ro::toValue(" + cls.properties[i].name + "_)";
    }
    e.line("        return {" + values + "};");
    e.line("    }");
    e.line();

    // Dispatch.
    e.line("    ro::Value invokeMethod(uint16_t index, const ro::Value::List &args) override");
    e.line("    {");
    if (!cls.slots.empty()) {
        e.line("        switch (index) {");
        for (size_t i = 0; i < cls.slots.size(); ++i) {
            const auto &slot = cls.slots[i];
            std::string returnType;
            mapper.map(slot.returnType.name, cls.name, returnType, error);
            e.line("        case " + std::to_string(i) + ": {");
            if (!slot.arguments.empty())
                e.line("            if (args.size() < " + std::to_string(slot.arguments.size())
                       + ") return ro::Value();");
            std::string call = slot.name + "(";
            for (size_t a = 0; a < slot.arguments.size(); ++a) {
                std::string cpp;
                mapper.map(slot.arguments[a].type.name, cls.name, cpp, error);
                if (a)
                    call += ", ";
                call += "ro::fromValue<" + cpp + ">(args[" + std::to_string(a) + "])";
            }
            call += ")";
            if (returnType == "void")
                e.line("            " + call + ";");
            else
                e.line("            return ro::toValue(" + call + ");");
            e.line("            return ro::Value();");
            e.line("        }");
        }
        e.line("        default: return ro::Value();");
        e.line("        }");
    } else {
        e.line("        (void)index;");
        e.line("        (void)args;");
        e.line("        return ro::Value();");
    }
    e.line("    }");
    e.line();

    e.line("private:");
    e.line("    mutable std::mutex mutex_;");
    for (const auto &property : cls.properties) {
        std::string cpp;
        mapper.map(property.type.name, cls.name, cpp, error);
        e.line("    " + cpp + " " + property.name + "_ = "
               + defaultValueFor(property, cpp, mapper, cls.name) + ";");
    }
    e.line("};");
    e.line();
    return true;
}

bool emitReplica(Emitter &e, const ClassDecl &cls, const TypeMapper &mapper, std::string &error)
{
    const std::string className = cls.name + "Replica";

    e.line("class " + className + " : public ro::Replica {");
    e.line("public:");
    e.line("    static constexpr const char *kTypeName = \"" + cls.name + "\";");
    e.line("    static constexpr const char *kSignature = \"@@SIGNATURE@@\";");
    e.line();

    if (!cls.enums.empty()) {
        if (!emitEnums(e, cls.enums, "    ", error))
            return false;
    }

    e.line("    explicit " + className + "(ro::Node *node, std::string name = kTypeName)");
    e.line("        : ro::Replica(node, std::move(name), kTypeName, kSignature)");
    e.line("    {");
    for (const auto &property : cls.properties) {
        std::string cpp;
        if (!mapper.map(property.type.name, cls.name, cpp, error)) {
            error = cls.name + "." + property.name + ": " + error;
            return false;
        }
        e.line("        registerProperty(\"" + property.name + "\", [this](const ro::Value &value) {");
        e.line("            const auto converted = ro::fromValue<" + cpp + ">(value);");
        e.line("            {");
        e.line("                std::lock_guard<std::mutex> lock(mutex_);");
        e.line("                " + property.name + "_ = converted;");
        e.line("            }");
        e.line("            " + property.name + "Changed.emit(converted);");
        e.line("        });");
    }
    for (const auto &signal : cls.signals) {
        e.line("        registerSignal([this](const ro::Value::List &args) {");
        if (!signal.arguments.empty())
            e.line("            if (args.size() < " + std::to_string(signal.arguments.size())
                   + ") return;");
        std::string args;
        for (size_t a = 0; a < signal.arguments.size(); ++a) {
            std::string cpp;
            if (!mapper.map(signal.arguments[a].type.name, cls.name, cpp, error)) {
                error = cls.name + "." + signal.name + ": " + error;
                return false;
            }
            if (a)
                args += ", ";
            args += "ro::fromValue<" + cpp + ">(args[" + std::to_string(a) + "])";
        }
        e.line("            " + signal.name + ".emit(" + args + ");");
        e.line("        });");
    }
    e.line("    }");
    e.line();

    for (const auto &property : cls.properties) {
        std::string cpp;
        mapper.map(property.type.name, cls.name, cpp, error);
        e.line("    " + cpp + " " + property.name + "() const");
        e.line("    {");
        e.line("        std::lock_guard<std::mutex> lock(mutex_);");
        e.line("        return " + property.name + "_;");
        e.line("    }");
        e.line("    ro::Signal<" + cpp + "> " + property.name + "Changed;");
        e.line();
    }

    for (const auto &signal : cls.signals) {
        std::string types;
        cppArgumentTypes(signal.arguments, mapper, cls.name, types, error);
        e.line("    ro::Signal<" + types + "> " + signal.name + ";");
        e.line();
    }

    for (size_t i = 0; i < cls.slots.size(); ++i) {
        const auto &slot = cls.slots[i];
        std::string returnType;
        if (!mapper.map(slot.returnType.name, cls.name, returnType, error)) {
            error = cls.name + "." + slot.name + ": " + error;
            return false;
        }
        e.line("    ro::PendingCall " + slot.name + "("
               + cppParameterList(slot.arguments, mapper, cls.name, error) + ")");
        e.line("    {");
        std::string args;
        for (size_t a = 0; a < slot.arguments.size(); ++a) {
            if (a)
                args += ", ";
            args += "ro::toValue("
                    + (slot.arguments[a].name.empty() ? "arg" + std::to_string(a)
                                                      : slot.arguments[a].name)
                    + ")";
        }
        e.line("        return node()->invoke(*this, " + std::to_string(i) + ", {" + args + "});");
        e.line("    }");
        e.line();
    }

    e.line("private:");
    e.line("    mutable std::mutex mutex_;");
    for (const auto &property : cls.properties) {
        std::string cpp;
        mapper.map(property.type.name, cls.name, cpp, error);
        e.line("    " + cpp + " " + property.name + "_ = "
               + defaultValueFor(property, cpp, mapper, cls.name) + ";");
    }
    e.line("};");
    e.line();
    return true;
}

}  // namespace

std::string computeSignature(const File &file)
{
    std::string canonical;
    for (const auto &e : file.enums) {
        canonical += "E" + e.name + "{";
        for (const auto &v : e.values)
            canonical += v.first + "=" + v.second + ",";
        canonical += "}";
    }
    for (const auto &p : file.pods) {
        canonical += "P" + p.name + "(";
        for (const auto &f : p.fields)
            canonical += f.type.name + " " + f.name + ",";
        canonical += ")";
    }
    for (const auto &c : file.classes) {
        canonical += "C" + c.name + "{";
        for (const auto &e : c.enums) {
            canonical += "E" + e.name + "{";
            for (const auto &v : e.values)
                canonical += v.first + "=" + v.second + ",";
            canonical += "}";
        }
        for (const auto &p : c.properties)
            canonical += "p" + p.type.name + " " + p.name + ";";
        for (const auto &s : c.signals) {
            canonical += "g" + s.name + "(";
            for (const auto &a : s.arguments)
                canonical += a.type.name + ",";
            canonical += ");";
        }
        for (const auto &s : c.slots) {
            canonical += "s" + s.returnType.name + " " + s.name + "(";
            for (const auto &a : s.arguments)
                canonical += a.type.name + ",";
            canonical += ");";
        }
        canonical += "}";
    }

    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : canonical) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

bool generate(const File &file, const std::string &sourceName, std::string &output,
              std::string &error)
{
    TypeMapper mapper(file);
    const std::string signature = computeSignature(file);

    Emitter e;
    e.line("// Generated by repgen from " + sourceName + ". Do not edit.");
    e.line("#pragma once");
    e.line();
    e.line("#include <cstdint>");
    e.line("#include <map>");
    e.line("#include <mutex>");
    e.line("#include <string>");
    e.line("#include <utility>");
    e.line("#include <vector>");
    e.line();
    e.line("#include \"ro/node.h\"");
    e.line("#include \"ro/pendingcall.h\"");
    e.line("#include \"ro/replica.h\"");
    e.line("#include \"ro/signal.h\"");
    e.line("#include \"ro/source.h\"");
    e.line("#include \"ro/value.h\"");
    e.line();

    if (!file.enums.empty()) {
        e.line("// ---- enums ----------------------------------------------------");
        if (!emitEnums(e, file.enums, "", error))
            return false;
    }
    if (!file.pods.empty()) {
        e.line("// ---- PODs -----------------------------------------------------");
        if (!emitPods(e, file, mapper, error))
            return false;
    }
    for (const auto &cls : file.classes) {
        e.line("// ---- " + cls.name + " --------------------------------------------");
        if (!emitSource(e, cls, mapper, error))
            return false;
        if (!emitReplica(e, cls, mapper, error))
            return false;
    }

    output = e.out.str();
    // Patch signatures after the fact: the signature must be identical in both
    // classes, and generating it up front keeps emit* functions simple.
    size_t pos = 0;
    while ((pos = output.find("@@SIGNATURE@@", pos)) != std::string::npos) {
        output.replace(pos, std::strlen("@@SIGNATURE@@"), signature);
        pos += signature.size();
    }
    return true;
}

}  // namespace rep
