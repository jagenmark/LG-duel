#pragma once

#include <string>
#include <string_view>

namespace lg {

void appendConsolePasteText(std::string& input, std::string_view text);
[[nodiscard]] std::string consoleInputClipboardText(std::string_view input);

} // namespace lg
