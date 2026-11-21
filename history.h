#ifndef HISTORY_H
#define HISTORY_H

#include <string>
#include <deque>

void load_history();
void save_history();
void add_to_history(const std::string& command);
std::string get_input_with_history();

#endif