#ifndef EXECUTION_H
#define EXECUTION_H

#include <vector>
#include <string>

void execute_command(std::vector<std::string> args, bool background);
void execute_pipeline(std::vector<std::vector<std::string>> commands, bool background);
std::vector<std::vector<std::string>> parse_pipeline(const std::string &input);

#endif