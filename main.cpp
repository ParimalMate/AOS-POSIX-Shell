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
#include <cstdlib>
#include <pwd.h>
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

// Runs one command (a pipeline or a single command, possibly with
// redirections). Returns true if the shell should exit.
static bool run_command(string cmd, bool background) {
    if (cmd.find('|') != string::npos) {
        vector<vector<string>> pipeline_commands = parse_pipeline(cmd);
        if (!pipeline_commands.empty()) execute_pipeline(pipeline_commands, background);
        return false;
    }

    vector<string> args = splitArgs(cmd);
    if (args.empty()) return false;
    if (args[0] == "exit") return true;

    int input_fd = -1, output_fd = -1;
    bool has_input_redir = false, has_output_redir = false;
    if (!handle_redirection(args, input_fd, output_fd, has_input_redir, has_output_redir)) return false;
    if (args.empty()) {
        if (input_fd != -1) close(input_fd);
        if (output_fd != -1) close(output_fd);
        cerr << "Syntax error: missing command" << endl;
        return false;
    }

    int saved_stdin = dup(STDIN_FILENO), saved_stdout = dup(STDOUT_FILENO);
    if (has_input_redir && input_fd != -1) { dup2(input_fd, STDIN_FILENO); close(input_fd); }
    if (has_output_redir && output_fd != -1) { dup2(output_fd, STDOUT_FILENO); close(output_fd); }

    // Built-ins always run in the shell itself (in the foreground)
    string command_name = args[0];
    if (command_name == "cd") handle_cd(args);
    else if (command_name == "echo") handle_echo(args);
    else if (command_name == "pwd") handle_pwd();
    else if (command_name == "ls") handle_ls(args);
    else if (command_name == "pinfo") handle_pinfo(args);
    else if (command_name == "search") handle_search(args);
    else if (command_name == "history") handle_history(args);
    else execute_command(args, background);

    restore_stdin_stdout(saved_stdin, saved_stdout);
    return false;
}

// Splits one ';'-separated command at '&': every part followed by '&' runs
// in the background, the final part (if any) in the foreground.
static vector<pair<string, bool>> split_background_jobs(const string &cmd) {
    vector<pair<string, bool>> jobs;
    size_t start = 0;
    while (true) {
        size_t amp = cmd.find('&', start);
        string part = cmd.substr(start, amp == string::npos ? string::npos : amp - start);
        trim(part);
        if (!part.empty()) jobs.push_back(make_pair(part, amp != string::npos));
        if (amp == string::npos) break;
        start = amp + 1;
    }
    return jobs;
}

static string history_file_path() {
    // Kept in the user's home directory so history is shared by all sessions,
    // wherever the shell is started from
    string user_home;
    struct passwd *pw = getpwuid(getuid());
    if (getenv("HOME") != nullptr) user_home = getenv("HOME");
    else if (pw != nullptr && pw->pw_dir != nullptr) user_home = pw->pw_dir;
    if (user_home.empty() || access(user_home.c_str(), W_OK) != 0) user_home = home_directory;
    return user_home + "/.myshell_history";
}

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
    history_file = history_file_path();
    load_history();
    
    bool should_exit = false;
    while (!should_exit) {
        string input = get_input_with_history();
        if (input == "\x04") {
            cout << endl;
            break;
        }
        trim(input);
        if (input.empty()) continue;
        
        add_to_history(input);
        
        vector<string> commands = splitCommands(input);
        for (size_t i = 0; i < commands.size() && !should_exit; i++) {
            vector<pair<string, bool>> jobs = split_background_jobs(commands[i]);
            for (size_t j = 0; j < jobs.size() && !should_exit; j++) {
                should_exit = run_command(jobs[j].first, jobs[j].second);
            }
        }
        cleanup_background_processes();
    }
    
    save_history();
    return 0;
}
