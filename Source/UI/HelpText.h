#pragma once
// ARCO's built-in guide (HELP). Simple markup: "# " title, "## " heading, "- " bullet, blank line = space.
#include <vector>

namespace arco {
struct HelpSection { const char* title; const char* body; };
const std::vector<HelpSection>& helpSections();
} // namespace arco
