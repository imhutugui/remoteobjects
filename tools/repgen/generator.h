// repgen - C++ header generation from a parsed .rep file.
#pragma once

#include <string>

#include "ast.h"

namespace rep {

// Signature used to reject replicas whose interface differs from the source.
std::string computeSignature(const File &file);

// Renders a self contained header with `<Class>SimpleSource` and
// `<Class>Replica` types plus the POD/enum declarations they use.
bool generate(const File &file, const std::string &sourceName, std::string &output,
              std::string &error);

}  // namespace rep
