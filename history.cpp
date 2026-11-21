#include "history.h"
#include "shell.h"
#include "utils.h"
#include "autocomplete.h"
#include <cerrno>
#include <fstream>
#include <termios.h>
#include <unistd.h>
#include <iostream>
#include <deque>

using namespace std;

void load_history() {
    ifstream historyFile(history_file);
    if (!historyFile.is_open()) {
        return;
    }
    string commandLine;
    while (getline(historyFile, commandLine) && static_cast<int>(history_list.size()) < MAX_HISTORY) {
        if (!commandLine.empty()) {
            history_list.push_back(commandLine);
        }
    }
    historyFile.close();
}

void save_history() {
    ofstream historyFile(history_file);
    if (!historyFile.is_open()) {
        return;
    }
    for (const string& command : history_list) {
        historyFile << command << endl;
    }
    historyFile.close();
}

void add_to_history(const string& command) {
    if (command.empty()) {
        return;
    }
    if (!history_list.empty() && history_list.back() == command) {
        return;
    }
    history_list.push_back(command);
    if (static_cast<int>(history_list.size()) > MAX_HISTORY) {
        history_list.pop_front();
    }
    save_history();
}

static void redraw_input_line(const string &text) {
    // Return to column 0, clear the line, then print prompt and text
    cout << "\r\033[K" << getPrompt() << text;
    cout.flush();
}

string get_input_with_history() {
    struct termios originalSettings, rawSettings;
    bool isTerminal = tcgetattr(STDIN_FILENO, &originalSettings) == 0;
    if (isTerminal) {
        rawSettings = originalSettings;
        rawSettings.c_lflag &= ~(ICANON | ECHO);
        rawSettings.c_cc[VMIN] = 1;
        rawSettings.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &rawSettings);
    }
    
    string userInput;
    char keyPressed;
    history_index = -1;
    current_input.clear();
    prompt_interrupted = 0;
    
    cout << getPrompt();
    cout.flush();
    
    while (true) {
        ssize_t bytesRead = read(STDIN_FILENO, &keyPressed, 1);
        if (bytesRead == -1) {
            if (errno == EINTR) {
                if (prompt_interrupted) {
                    // CTRL-C at the prompt: discard the line, show a fresh prompt
                    prompt_interrupted = 0;
                    userInput.clear();
                    history_index = -1;
                    cout << "\n" << getPrompt();
                    cout.flush();
                }
                continue;
            }
            if (userInput.empty()) userInput = "\x04";
            break;
        }
        if (bytesRead == 0) {
            // End of input behaves like CTRL-D on an empty line
            if (userInput.empty()) userInput = "\x04";
            else cout << endl;
            break;
        }

        if (keyPressed == '\n' || keyPressed == '\r') {
            cout << endl;
            break;
        }
        else if (keyPressed == 127 || keyPressed == '\b') {
            if (!userInput.empty()) {
                userInput.pop_back();
                cout << "\b \b";
                cout.flush();
            }
        }
        else if (keyPressed == '\t') {
            handle_tab_completion(userInput);
        }
        else if (keyPressed == 27) {
            char escapeSequence[2];
            if (read(STDIN_FILENO, &escapeSequence[0], 1) == 1 && 
                read(STDIN_FILENO, &escapeSequence[1], 1) == 1) {
                if (escapeSequence[0] == 91) {
                    if (escapeSequence[1] == 65) {
                        if (history_index < static_cast<int>(history_list.size()) - 1) {
                            if (history_index == -1) {
                                current_input = userInput;
                            }
                            history_index++;
                            userInput = history_list[history_list.size() - 1 - history_index];
                            redraw_input_line(userInput);
                        }
                    }
                    else if (escapeSequence[1] == 66) {
                        if (history_index > -1) {
                            history_index--;
                            if (history_index == -1) {
                                userInput = current_input;
                            } else {
                                userInput = history_list[history_list.size() - 1 - history_index];
                            }
                            redraw_input_line(userInput);
                        }
                    }
                }
            }
        }
        else if (keyPressed == 4) {
            userInput = "\x04";
            break;
        }
        else if (isprint(static_cast<unsigned char>(keyPressed))) {
            userInput += keyPressed;
            cout << keyPressed;
            cout.flush();
        }
    }
    if (isTerminal) tcsetattr(STDIN_FILENO, TCSANOW, &originalSettings);
    return userInput;
}
