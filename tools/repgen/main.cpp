// repgen - converts a .rep interface definition into a C++ header.
//
// usage: repgen <file.rep> -o <output.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "generator.h"
#include "parser.h"

namespace {

std::string baseName(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos)
        name = name.substr(0, dot);
    return name;
}

int usage()
{
    std::cerr << "usage: repgen <input.rep> [-o <output.h>] [--print-signature]\n";
    return 2;
}

}  // namespace

int main(int argc, char **argv)
{
    std::string input;
    std::string output;
    bool printSignature = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output = argv[++i];
        } else if (arg == "--print-signature") {
            printSignature = true;
        } else if (arg == "-h" || arg == "--help") {
            return usage();
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "repgen: unknown option " << arg << "\n";
            return usage();
        } else {
            input = arg;
        }
    }

    if (input.empty())
        return usage();

    std::ifstream file(input);
    if (!file) {
        std::cerr << "repgen: cannot open " << input << "\n";
        return 1;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    rep::File parsed;
    parsed.name = baseName(input);
    rep::Parser parser;
    std::string error;
    if (!parser.parse(text, parsed, error)) {
        std::cerr << "repgen: " << input << ": " << error << "\n";
        return 1;
    }

    if (printSignature) {
        std::cout << rep::computeSignature(parsed) << "\n";
        return 0;
    }

    std::string generated;
    if (!rep::generate(parsed, parsed.name + ".rep", generated, error)) {
        std::cerr << "repgen: " << input << ": " << error << "\n";
        return 1;
    }

    if (output.empty())
        output = "rep_" + parsed.name + ".h";

    std::ofstream out(output);
    if (!out) {
        std::cerr << "repgen: cannot write " << output << "\n";
        return 1;
    }
    out << generated;
    return 0;
}
