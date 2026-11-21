#ifndef AUTOCOMPLETE_H
#define AUTOCOMPLETE_H

#include <string>

// Called when TAB is pressed. Completes the word at the end of `input`
// (a command name in command position, otherwise a file/directory name),
// updating both `input` and what is shown on the terminal.
void handle_tab_completion(std::string &input);

#endif
