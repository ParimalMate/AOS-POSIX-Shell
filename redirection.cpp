#include "redirection.h"
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <string>

using namespace std;

bool handle_redirection(vector<string>& args, int& input_fd, int& output_fd, 
                       bool& has_input_redir, bool& has_output_redir) {
    vector<string> new_args;
    string input_file, output_file;
    bool append_mode = false;
    
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "<") {
            if (i + 1 >= args.size()) { 
                cerr << "Syntax error: no input file provided after <" << endl; 
                return false; 
            }
            input_file = args[i + 1]; 
            has_input_redir = true; 
            i++; 
        } else if (args[i] == ">") {
            if (i + 1 >= args.size()) { 
                cerr << "Syntax error: no output file provided after >" << endl; 
                return false; 
            }
            output_file = args[i + 1]; 
            has_output_redir = true; 
            append_mode = false; 
            i++; 
        } else if (args[i] == ">>") {
            if (i + 1 >= args.size()) { 
                cerr << "Syntax error: no output file provided after >>" << endl; 
                return false; 
            }
            output_file = args[i + 1]; 
            has_output_redir = true; 
            append_mode = true; 
            i++; 
        } else {
            new_args.push_back(args[i]);
        }
    }
    
    args = new_args;
    
    if (has_input_redir) {
        input_fd = open(input_file.c_str(), O_RDONLY);
        if (input_fd == -1) { 
            perror(("Cannot open input file: " + input_file).c_str()); 
            return false; 
        }
    }
    
    if (has_output_redir) {
        int flags = O_WRONLY | O_CREAT;
        if (append_mode) {
            flags |= O_APPEND;
        } else {
            flags |= O_TRUNC;
        }
        
        output_fd = open(output_file.c_str(), flags, 0644);
        if (output_fd == -1) {
            perror(("Cannot open output file: " + output_file).c_str());
            if (input_fd != -1) {
                close(input_fd);
            }
            return false;
        }
    }
    
    return true;
}

void restore_stdin_stdout(int saved_stdin, int saved_stdout) {
    cout.flush();
    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);
}

void process_redirection_in_pipeline(vector<vector<string>>& pipeline_commands, 
                                    vector<int>& input_fds, vector<int>& output_fds, 
                                    vector<bool>& has_input_redirs, vector<bool>& has_output_redirs) {
    for (size_t i = 0; i < pipeline_commands.size(); i++) {
        int input_fd = -1, output_fd = -1;
        bool has_input_redir = false, has_output_redir = false;
        
        bool success = handle_redirection(pipeline_commands[i], input_fd, output_fd, 
                                         has_input_redir, has_output_redir);
        
        if (!success) {
            for (size_t j = 0; j < i; j++) {
                if (input_fds[j] != -1) {
                    close(input_fds[j]);
                }
                if (output_fds[j] != -1) {
                    close(output_fds[j]);
                }
            }
            input_fds.clear();
            output_fds.clear();
            has_input_redirs.clear();
            has_output_redirs.clear();
            return;
        }
        
        input_fds.push_back(input_fd);
        output_fds.push_back(output_fd);
        has_input_redirs.push_back(has_input_redir);
        has_output_redirs.push_back(has_output_redir);
    }
}