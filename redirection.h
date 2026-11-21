#ifndef REDIRECTION_H
#define REDIRECTION_H

#include <vector>
#include <string>

bool handle_redirection(std::vector<std::string>& args, int& input_fd, int& output_fd, bool& has_input_redir, bool& has_output_redir);
void restore_stdin_stdout(int saved_stdin, int saved_stdout);
void process_redirection_in_pipeline(std::vector<std::vector<std::string>>& pipeline_commands, std::vector<int>& input_fds, std::vector<int>& output_fds, std::vector<bool>& has_input_redirs, std::vector<bool>& has_output_redirs);

#endif