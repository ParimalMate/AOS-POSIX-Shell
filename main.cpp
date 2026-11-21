#include "shell.h"
#include "builtins.h"
#include "execution.h"
#include "history.h"
#include "utils.h"
#include "signals.h"
#include "redirection.h"
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <clocale>
#include <termios.h>

using namespace std;

string home_directory;
string previous_directory;
map<pid_t, string> background_processes;
pid_t foreground_pgid = -1;
bool is_foreground_running = false;
deque<string> history_list;
const int MAX_HISTORY = 20;
const int DISPLAY_HISTORY = 10;
string history_file;
int history_index = -1;
string current_input;
struct termios shell_terminal_modes;
bool shell_terminal_modes_saved = false;

int main(){
    setlocale(LC_COLLATE, "");
    setup_signal_handlers();
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &shell_terminal_modes) == 0) {
        shell_terminal_modes_saved = true;
    }
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        perror("getcwd");
        return 1;
    }
    home_directory = cwd;
    history_file = home_directory + "/.myshell_history";
    load_history();
    
    while(true){
        string input = get_input_with_history();
        if (input == "\x04") {
            cout << endl;
            if (is_foreground_running && foreground_pgid != -1) 
                kill(-foreground_pgid, SIGTERM);
            break;
        }
        trim(input);
        if (input.empty()) continue;
        
        add_to_history(input);
        
        bool should_exit = false;
        vector<string> commands = splitCommands(input);
        for (string cmd : commands) {
            bool background = false;
            trim(cmd);
            if (cmd.empty()) continue;
            
            size_t amp_pos = cmd.find_last_of('&');
            if (amp_pos != string::npos) {
                string after_amp = cmd.substr(amp_pos + 1);
                trim(after_amp);
                if (after_amp.empty()) {
                    background = true;
                    cmd = cmd.substr(0, amp_pos);
                    trim(cmd);
                }
            }
            if (cmd.empty()) continue;
            
            if (cmd.find('|') != string::npos) {
                vector<vector<string>> pipeline_commands = parse_pipeline(cmd);
                if (!pipeline_commands.empty()) execute_pipeline(pipeline_commands, background);
            } else {
                vector<string> args = splitArgs(cmd);
                if (args.empty()) continue;
                if (args[0] == "exit") {
                    should_exit = true;
                    break;
                }
                
                int input_fd = -1, output_fd = -1;
                bool has_input_redir = false, has_output_redir = false;
                if (!handle_redirection(args, input_fd, output_fd, has_input_redir, has_output_redir)) continue;
                if (args.empty()) {
                    if (input_fd != -1) close(input_fd);
                    if (output_fd != -1) close(output_fd);
                    cerr << "Syntax error: missing command" << endl;
                    continue;
                }
                
                int saved_stdin = dup(STDIN_FILENO), saved_stdout = dup(STDOUT_FILENO);
                if (has_input_redir && input_fd != -1) { dup2(input_fd, STDIN_FILENO); close(input_fd); }
                if (has_output_redir && output_fd != -1) { dup2(output_fd, STDOUT_FILENO); close(output_fd); }
                
                string command_name = args[0];
                if (command_name == "cd") handle_cd(args);
                else if (command_name == "echo") handle_echo(args);
                else if (command_name == "pwd") handle_pwd();
                else if (command_name == "ls") handle_ls(args);
                else if (command_name == "pinfo") handle_pinfo(args);
                else if (command_name == "search") handle_search(args);
                else if (command_name == "history") handle_history(args);
                else {
                    if (background && is_builtin_command(command_name)) {
                        cout << "Warning: Built-in commands cannot run in background. Running in foreground." << endl;
                        background = false;
                    }
                    execute_command(args, background);
                }
                restore_stdin_stdout(saved_stdin, saved_stdout);
            }
        }
        cleanup_background_processes();
        if (should_exit) break;
    }
    
    save_history();
    return 0;
}