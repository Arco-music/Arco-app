#pragma once
// ARCO 2.4: PATCHER - ready-made patches to open, copy and change; the lessons of the guide.
#include "Patcher.h"

namespace arco { namespace px {

struct Example { juce::String name, shelf, notes; PatchDoc doc; };
const std::vector<Example>& examples();
// a patch written as lines: "obj id x y text" / "msg id x y text" / "com id x y text" / "- a outlet b inlet"
PatchDoc patchFromText(const juce::String& name, const juce::String& text);

struct Lesson { juce::String title, text; juce::String example; };   // example: a name in examples()
const std::vector<Lesson>& lessons();

}} // namespace arco::px
