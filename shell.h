#ifndef SHELL_H
#define SHELL_H
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <deque>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <termios.h>

extern std::string home_directory;
extern std::string previous_directory;
extern std::map<pid_t, std::string> background_processes;
extern pid_t foreground_pgid;
extern bool is_foreground_running;
extern std::deque<std::string> history_list;
extern const int MAX_HISTORY;
extern const int DISPLAY_HISTORY;
extern std::string history_file;
extern int history_index;
extern std::string current_input;
extern volatile sig_atomic_t prompt_interrupted;
extern struct termios shell_terminal_modes;
extern bool shell_terminal_modes_saved;

void setup_signal_handlers();
void cleanup_background_processes();
bool background_jobs_finished();
void handle_command_error(const std::string &command, int errno_val);
bool is_builtin_command(const std::string &command);

#endif
