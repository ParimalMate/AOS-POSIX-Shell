#ifndef BUILTINS_H
#define BUILTINS_H

#include <vector>
#include <string>

void handle_cd(std::vector<std::string> args);
void handle_echo(std::vector<std::string> args);
void handle_pwd();
void handle_ls(std::vector<std::string> args);
void handle_pinfo(std::vector<std::string> args);
void handle_search(std::vector<std::string> args);
void handle_history(std::vector<std::string> args);

#endif