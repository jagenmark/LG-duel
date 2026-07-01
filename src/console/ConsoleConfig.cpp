#include "console/ConsoleConfig.hpp"

#include <fstream>
#include <sstream>

namespace lg {
namespace {

[[nodiscard]] std::string trimComment(std::string line) {
  const std::size_t comment = line.find('#');
  if (comment != std::string::npos) {
    line.erase(comment);
  }
  return line;
}

[[nodiscard]] bool hasText(std::string_view line) {
  return line.find_first_not_of(" \t\r\n") != std::string_view::npos;
}

[[nodiscard]] std::string lineError(int lineNumber, const std::string& message) {
  return "line " + std::to_string(lineNumber) + ": " + message;
}

} // namespace

ConsoleConfigResult executeConsoleConfigText(
  ConsoleSystem& console,
  std::string_view text
) {
  ConsoleConfigResult result;
  std::istringstream input{std::string(text)};
  std::string line;
  int lineNumber = 0;
  while (std::getline(input, line)) {
    ++lineNumber;
    line = trimComment(std::move(line));
    if (!hasText(line)) {
      continue;
    }
    const std::string output = console.execute(line);
    if (
      output.starts_with("unknown ") ||
      output.starts_with("usage: ") ||
      output.starts_with("invalid ") ||
      output.starts_with("value out of range") ||
      output.ends_with(" is read-only")
    ) {
      result.ok = false;
      result.errors.push_back(lineError(lineNumber, output));
    }
  }
  return result;
}

ConsoleConfigResult executeConsoleConfigFile(
  ConsoleSystem& console,
  const std::string& path
) {
  std::ifstream file(path);
  if (!file) {
    return {false, {"could not open console config file '" + path + "'"}};
  }

  std::ostringstream text;
  text << file.rdbuf();
  ConsoleConfigResult result = executeConsoleConfigText(console, text.str());
  for (std::string& error : result.errors) {
    error = path + ": " + error;
  }
  return result;
}

} // namespace lg
