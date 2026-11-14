#pragma once

// The F1 panel's languages. The panel's texts are written in Spanish (the source language) and Tr() gives each one in the
// language chosen in the panel: 0 Spanish, 1 English (built in), then every lang/<name>.txt beside the exe (lines
// "Spanish text = translation", # for a comment; lang/template.txt is written there for a translator to copy). A text
// with no translation shows as written.
namespace lang {

const char* Tr(const char* source);  // stable for the program's run
int Count();
const char* Name(int index);

}  // namespace lang

using lang::Tr;
