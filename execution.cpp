#include "execution.h"
#include "shell.h"
#include "builtins.h"
#include "redirection.h"
#include "utils.h"
#include <iostream>
#include <vector>
#include <sys/wait.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <sstream>
#include <string>

using namespace std;

void handle_command_error(const string &command, int errno_val) {
    if (errno_val == ENOENT) {
        cerr << command << ": command not found" << endl;
    } else if (errno_val == EACCES) {
        cerr << command << ": Permission denied" << endl;
    } else {
        cerr << command << ": " << strerror(errno_val) << endl;
    }
}

static void block_sigchld(sigset_t &old_mask) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &old_mask);
}

static void restore_signal_mask(const sigset_t &old_mask) {
    sigprocmask(SIG_SETMASK, &old_mask, nullptr);
}

// Runs in a freshly forked child: places it in the job's process group, gives
// a foreground job the terminal and restores default signal behaviour.
static void prepare_child(pid_t pgid, bool background) {
    setpgid(0, pgid);
    if (!background && isatty(STDIN_FILENO)) {
        tcsetpgrp(STDIN_FILENO, getpgrp());
    }
    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);

    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, nullptr);
}

static void give_terminal_to(pid_t pgid) {
    if (isatty(STDIN_FILENO)) tcsetpgrp(STDIN_FILENO, pgid);
}

static void take_back_terminal() {
    if (isatty(STDIN_FILENO)) {
        tcsetpgrp(STDIN_FILENO, getpgrp());
        if (shell_terminal_modes_saved) {
            tcsetattr(STDIN_FILENO, TCSADRAIN, &shell_terminal_modes);
        }
    }
}

static int wait_for_child(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, WUNTRACED) == -1) {
        if (errno != EINTR) return -1;
    }
    return status;
}

void execute_command(vector<string> args, bool background) {
    sigset_t old_mask;
    block_sigchld(old_mask);
    cout.flush();

    pid_t pid = fork();

    if (pid == 0) {
        prepare_child(0, background);

        vector<char*> argv;
        for (string& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());

        handle_command_error(args[0], errno);
        _exit(127);
    } else if (pid > 0) {
        setpgid(pid, pid);
        background_processes.erase(pid);

        if (!background) {
            foreground_pgid = pid;
            give_terminal_to(pid);
            is_foreground_running = true;

            int status = wait_for_child(pid);

            take_back_terminal();
            is_foreground_running = false;
            foreground_pgid = -1;

            if (status != -1 && WIFSTOPPED(status)) {
                cout << "\nProcess " << pid << " stopped" << endl;
                background_processes[pid] = args[0];
            } else if (status != -1 && WIFSIGNALED(status) && WTERMSIG(status) == SIGINT) {
                cout << endl;
            }
        } else {
            cout << "[" << pid << "]" << endl;
            background_processes[pid] = args[0];
        }
    } else {
        cerr << "Error: Unable to create process for '" << args[0] << "'" << endl;
        cerr << "Reason: " << strerror(errno) << endl;
    }

    restore_signal_mask(old_mask);
}

void execute_pipeline(vector<vector<string>> commands, bool background) {
    int num_commands = commands.size();
    if (num_commands == 0) return;

    vector<int> input_fds, output_fds;
    vector<bool> has_input_redirs, has_output_redirs;

    process_redirection_in_pipeline(commands, input_fds, output_fds, has_input_redirs, has_output_redirs);
    if (input_fds.empty()) return;

    for (int i = 0; i < num_commands; i++) {
        if (commands[i].empty()) {
            cerr << "Syntax error: missing command in pipeline" << endl;
            for (int j = 0; j < num_commands; j++) {
                if (input_fds[j] != -1) close(input_fds[j]);
                if (output_fds[j] != -1) close(output_fds[j]);
            }
            return;
        }
    }

    vector<vector<int>> pipes(num_commands - 1, vector<int>(2));
    for (int i = 0; i < num_commands - 1; i++) {
        if (pipe(pipes[i].data()) == -1) {
            perror("pipe");
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            for (int j = 0; j < num_commands; j++) {
                if (input_fds[j] != -1) close(input_fds[j]);
                if (output_fds[j] != -1) close(output_fds[j]);
            }
            return;
        }
    }

    sigset_t old_mask;
    block_sigchld(old_mask);
    cout.flush();

    vector<pid_t> pids;
    pid_t pipeline_pgid = 0;
    for (int i = 0; i < num_commands; i++) {
        pid_t pid = fork();

        if (pid == 0) {
            prepare_child(pipeline_pgid, background);

            if (i == 0 && has_input_redirs[i] && input_fds[i] != -1) {
                dup2(input_fds[i], STDIN_FILENO);
            } else if (i > 0) {
                dup2(pipes[i-1][0], STDIN_FILENO);
            }

            if (i == num_commands - 1 && has_output_redirs[i] && output_fds[i] != -1) {
                dup2(output_fds[i], STDOUT_FILENO);
            } else if (i < num_commands - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            for (int j = 0; j < num_commands - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            for (int j = 0; j < num_commands; j++) {
                if (input_fds[j] != -1) close(input_fds[j]);
                if (output_fds[j] != -1) close(output_fds[j]);
            }

            vector<string> args = commands[i];
            string command_name = args[0];

            if (command_name == "cd") {
                handle_cd(args);
                exit(EXIT_SUCCESS);
            } else if (command_name == "echo") {
                handle_echo(args);
                exit(EXIT_SUCCESS);
            } else if (command_name == "pwd") {
                handle_pwd();
                exit(EXIT_SUCCESS);
            } else if (command_name == "ls") {
                handle_ls(args);
                exit(EXIT_SUCCESS);
            } else if (command_name == "pinfo") {
                handle_pinfo(args);
                exit(EXIT_SUCCESS);
            } else if (command_name == "search") {
                handle_search(args);
                exit(EXIT_SUCCESS);
            } else if (command_name == "history") {
                handle_history(args);
                exit(EXIT_SUCCESS);
            } else {
                vector<char*> argv;
                for (string& arg : args) {
                    argv.push_back(const_cast<char*>(arg.c_str()));
                }
                argv.push_back(nullptr);

                execvp(argv[0], argv.data());
                handle_command_error(args[0], errno);
                _exit(127);
            }
        } else if (pid > 0) {
            pids.push_back(pid);
            if (i == 0) pipeline_pgid = pid;
            setpgid(pid, pipeline_pgid);
            background_processes.erase(pid);
        } else {
            cerr << "Error: Unable to create process for pipeline command" << endl;
            cerr << "Reason: " << strerror(errno) << endl;
            break;
        }
    }

    for (int i = 0; i < num_commands - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (int i = 0; i < num_commands; i++) {
        if (input_fds[i] != -1) close(input_fds[i]);
        if (output_fds[i] != -1) close(output_fds[i]);
    }

    if (pids.empty()) {
        restore_signal_mask(old_mask);
        return;
    }

    if (!background) {
        foreground_pgid = pipeline_pgid;
        give_terminal_to(pipeline_pgid);
        is_foreground_running = true;

        bool interrupted = false;
        for (size_t i = 0; i < pids.size(); i++) {
            int status = wait_for_child(pids[i]);
            if (status == -1) continue;

            if (WIFSTOPPED(status)) {
                cout << "\nProcess " << pids[i] << " stopped" << endl;
                background_processes[pids[i]] = commands[i][0];
            } else if (WIFSIGNALED(status) && WTERMSIG(status) == SIGINT) {
                interrupted = true;
            }
        }

        take_back_terminal();
        is_foreground_running = false;
        foreground_pgid = -1;
        if (interrupted) cout << endl;
    } else {
        for (size_t i = 0; i < pids.size(); i++) {
            cout << "[" << pids[i] << "]" << endl;
            background_processes[pids[i]] = commands[i][0];
        }
    }

    restore_signal_mask(old_mask);
}

vector<vector<string>> parse_pipeline(const string &input) {
    vector<vector<string>> commands;
    stringstream ss(input);
    string token;

    while (getline(ss, token, '|')) {
        trim(token);
        if (!token.empty()) {
            commands.push_back(splitArgs(token));
        }
    }

    return commands;
}
