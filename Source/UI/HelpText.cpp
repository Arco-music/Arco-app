#include "HelpText.h"
namespace arco {
const std::vector<HelpSection>& helpSections() {
    static const std::vector<HelpSection> s = {
        { "Start here", "# ARCO\nPlaceholder." },
    };
    return s;
}
} // namespace arco
