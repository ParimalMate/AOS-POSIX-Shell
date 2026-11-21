#include "builtins.h"
#include "shell.h"
#include "utils.h"
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <iomanip>
#include <limits.h>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <algorithm>

using namespace std;

static bool collateLess(const string &a, const string &b) {
    return strcoll(a.c_str(), b.c_str()) < 0;
}

bool is_builtin_command(const string &command) {
    static const char *builtins[] = {"cd", "echo", "pwd", "ls", "pinfo", "search", "history", "exit"};
    for (const char *builtin : builtins) {
        if (command == builtin) return true;
    }
    return false;
}

void handle_cd(vector<string> args) {
    if (args.size() > 2) { 
        cout << "Invalid arguments" << endl; 
        return; 
    }
    
    string target;
    if (args.size() == 1) {
        target = home_directory;
    } else {
        target = args[1];
    }
    
    if (target == "-") {
        if (previous_directory.empty()) { 
            cout << "No previous directory" << endl; 
            return; 
        } else {
            target = previous_directory;
        }
    } else if (target == "~") {
        target = home_directory;
    } else if (target.compare(0, 2, "~/") == 0) {
        target = home_directory + target.substr(1);
    } else if (target == ".") {
        return;
    } else if (target == "..") {
        char cwd[PATH_MAX];
        getcwd(cwd, sizeof(cwd));
        string current_dir = cwd;
        size_t last_slash = current_dir.find_last_of('/');
        if (last_slash != string::npos) {
            target = (last_slash == 0) ? "/" : current_dir.substr(0, last_slash);
        }
    }
    
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));
    string current_dir = cwd;
    int ret = chdir(target.c_str());
    if (ret != 0) {
        perror("cd");
    } else {
        if (args.size() == 2 && args[1] == "-") cout << target << endl;
        previous_directory = current_dir;
    }
}

void handle_echo(vector<string> args) {
    for (size_t i = 1; i < args.size(); i++) {
        if (i > 1) cout << " ";
        cout << args[i];
    }
    cout << endl;
}

void handle_pwd() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        cout << cwd << endl;
    } else {
        perror("pwd");
    }
}

void handle_ls(vector<string> args) {
    bool show_all = false, long_format = false, invalid_flag = false;
    vector<string> paths;
    string invalid_flag_name;
    
    for (size_t i = 1; i < args.size(); i++) {
        if (args[i][0] == '-') {
            if (args[i].size() == 1) { 
                paths.push_back(args[i]); 
                continue; 
            }
            
            string flag = args[i].substr(1);
            for (char c : flag) {
                if (c == 'a') {
                    show_all = true;
                } else if (c == 'l') {
                    long_format = true;
                } else {
                    invalid_flag = true;
                    invalid_flag_name = string(1, c);
                    break;
                }
            }
            if (invalid_flag) break;
        } else {
            paths.push_back(args[i]);
        }
    }
    
    if (invalid_flag) {
        cout << "ls: invalid option -- '" << invalid_flag_name << "'" << endl;
        cout << "Try 'ls -a -l' for more information." << endl;
        return;
    }
    
    size_t operand_count = paths.size();
    if (paths.empty()) {
        paths.push_back(".");
    }

    // Like GNU ls: report bad operands first, then list file operands
    // together, then each directory.
    vector<string> files, dirs;
    for (const string &path : paths) {
        string actual_path = path;
        if (path == "~") {
            actual_path = home_directory;
        } else if (path.compare(0, 2, "~/") == 0) {
            actual_path = home_directory + path.substr(1);
        } else if (path == "-") {
            if (previous_directory.empty()) {
                cout << "ls: cannot access '-': No previous directory" << endl;
                continue;
            } else {
                actual_path = previous_directory;
            }
        }

        struct stat stat_buf;
        // With -l a symbolic link operand is listed itself, not followed
        int rc = long_format ? lstat(actual_path.c_str(), &stat_buf) : stat(actual_path.c_str(), &stat_buf);
        if (rc != 0 && !long_format) {
            // A dangling symbolic link is still listed by name
            rc = lstat(actual_path.c_str(), &stat_buf);
        }
        if (rc != 0) {
            perror(("ls: cannot access '" + actual_path + "'").c_str());
            continue;
        }
        if (S_ISDIR(stat_buf.st_mode)) dirs.push_back(actual_path);
        else files.push_back(actual_path);
    }

    sort(files.begin(), files.end(), collateLess);
    sort(dirs.begin(), dirs.end(), collateLess);

    if (!files.empty()) {
        if (long_format) {
            printLongListing("", files, false, dirs);
        } else {
            for (const string &file : files) cout << file << endl;
        }
    }

    for (size_t d = 0; d < dirs.size(); d++) {
        const string &actual_path = dirs[d];
        DIR *dir = opendir(actual_path.c_str());
        if (dir == nullptr) {
            perror(("ls: cannot open directory '" + actual_path + "'").c_str());
            continue;
        }

        if (!files.empty() || d > 0) cout << endl;
        if (operand_count > 1) {
            cout << actual_path << ":" << endl;
        }

        vector<string> entries;
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            string name = entry->d_name;
            if (!show_all && name[0] == '.') continue;
            entries.push_back(name);
        }
        closedir(dir);

        sort(entries.begin(), entries.end(), collateLess);
        if (long_format) {
            printLongListing(actual_path, entries, true);
        } else {
            for (const string &name : entries) cout << name << endl;
        }
    }
}

void handle_pinfo(vector<string> args) {
    pid_t pid;
    if (args.size() == 1) {
        pid = getpid();
    } else if (args.size() == 2) {
        try { 
            pid = stoi(args[1]); 
        } catch (const exception& e) { 
            cerr << "pinfo: invalid PID '" << args[1] << "'" << endl; 
            return; 
        }
    } else { 
        cerr << "pinfo: too many arguments" << endl; 
        return; 
    }
    
    string status_path = "/proc/" + to_string(pid) + "/status";
    FILE* status_file = fopen(status_path.c_str(), "r");
    if (status_file == nullptr) { 
        cerr << "pinfo: process with PID " << pid << " does not exist" << endl; 
        return; 
    }
    fclose(status_file);
    
    string status = get_process_status(pid);
    string memory = get_virtual_memory(pid);
    string exe_path = get_exe_path(pid);
    
    exe_path = abbreviateHome(exe_path);
    
    cout << "Process Status -- " << status << endl;
    cout << "memory -- " << memory << " {Virtual Memory}" << endl;
    cout << "Executable Path -- " << exe_path << endl;
}

void handle_search(vector<string> args) {
    if (args.size() != 2) { 
        cerr << "search: exactly one argument expected" << endl; 
        return; 
    }
    
    string target = args[1];
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) { 
        perror("search: getcwd failed"); 
        return; 
    }
    
    bool found = search_recursive(cwd, target);
    cout << (found ? "True" : "False") << endl;
}

void handle_history(vector<string> args) {
    int count = DISPLAY_HISTORY;
    if (args.size() > 1) {
        try {
            count = stoi(args[1]);
            if (count <= 0) { 
                cout << "history: argument must be a positive integer" << endl; 
                return; 
            }
        } catch (const exception& e) { 
            cout << "history: argument must be a number" << endl; 
            return; 
        }
    }
    
    count = min(count, static_cast<int>(history_list.size()));
    int start_index = history_list.size() - count;
    
    for (int i = start_index; i < static_cast<int>(history_list.size()); i++) {
        cout << history_list[i] << endl;
    }
}