#ifndef UTILS_H
#define UTILS_H

#include <vector>
#include <string>
#include <sys/types.h>

std::vector<std::string> splitCommands(std::string input);
std::vector<std::string> splitArgs(const std::string& s);
void trim(std::string &s);
std::string getPrompt();
std::string getDisplayPath();
std::string abbreviateHome(const std::string &path);
std::string getBaseName(std::string path);
std::string getFilePermissions(mode_t mode);
std::string formatFileSize(off_t size);
void printLongListing(const std::string &dir, const std::vector<std::string> &names, bool showTotal);
bool search_recursive(const std::string &current_dir, const std::string &target);
std::string get_exe_path(pid_t pid);
std::string get_process_status(pid_t pid);
std::string get_virtual_memory(pid_t pid);

#endif
