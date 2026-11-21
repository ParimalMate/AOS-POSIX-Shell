#include "autocomplete.h"
#include "shell.h"
#include "utils.h"
#include <iostream>
#include <vector>
#include <set>
#include <string>
#include <cstdlib>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

struct Candidate {
    string name;
    bool is_directory;
    bool operator<(const Candidate &other) const { return name < other.name; }
};

static const char *BUILTIN_COMMANDS[] = {"cd", "echo", "pwd", "ls", "pinfo", "search", "history", "exit"};
static const size_t ASK_BEFORE_LISTING = 100;

static bool is_word_separator(char c) {
    return isspace(static_cast<unsigned char>(c)) || c == ';' || c == '|' || c == '&' || c == '<' || c == '>';
}

// A word is in command position if it is the first word of the line or
// follows ';', '|' or '&'.
static bool is_command_position(const string &input, size_t word_start) {
    size_t i = word_start;
    while (i > 0 && isspace(static_cast<unsigned char>(input[i - 1]))) i--;
    if (i == 0) return true;
    char prev = input[i - 1];
    return prev == ';' || prev == '|' || prev == '&';
}

static void collect_commands(const string &prefix, set<Candidate> &out) {
    for (const char *builtin : BUILTIN_COMMANDS) {
        string name = builtin;
        if (name.compare(0, prefix.size(), prefix) == 0) out.insert({name, false});
    }

    const char *path_env = getenv("PATH");
    if (path_env == nullptr) return;

    string path_list = path_env;
    size_t start = 0;
    while (start <= path_list.size()) {
        size_t end = path_list.find(':', start);
        if (end == string::npos) end = path_list.size();
        string dir_path = path_list.substr(start, end - start);
        if (dir_path.empty()) dir_path = ".";
        start = end + 1;

        DIR *dir = opendir(dir_path.c_str());
        if (dir == nullptr) continue;
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            string name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (name.compare(0, prefix.size(), prefix) != 0) continue;

            string full_path = dir_path + "/" + name;
            struct stat st;
            if (stat(full_path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
                access(full_path.c_str(), X_OK) == 0) {
                out.insert({name, false});
            }
        }
        closedir(dir);
    }
}

// `word` may contain a directory part ("src/ma"); entries of that directory
// whose names start with the part after the last '/' are collected.
static void collect_files(const string &word, string &base_out, set<Candidate> &out) {
    size_t slash = word.find_last_of('/');
    string dir_part = (slash == string::npos) ? "" : word.substr(0, slash + 1);
    base_out = (slash == string::npos) ? word : word.substr(slash + 1);

    string dir_to_open = dir_part.empty() ? "." : dir_part;
    if (dir_to_open.compare(0, 2, "~/") == 0) {
        dir_to_open = home_directory + dir_to_open.substr(1);
    }

    DIR *dir = opendir(dir_to_open.c_str());
    if (dir == nullptr) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (name[0] == '.' && (base_out.empty() || base_out[0] != '.')) continue;
        if (name.compare(0, base_out.size(), base_out) != 0) continue;

        string full_path = dir_to_open + (dir_to_open[dir_to_open.size() - 1] == '/' ? "" : "/") + name;
        struct stat st;
        bool is_dir = stat(full_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        out.insert({name, is_dir});
    }
    closedir(dir);
}

static string longest_common_prefix(const vector<Candidate> &candidates) {
    if (candidates.empty()) return "";
    string prefix = candidates[0].name;
    for (size_t i = 1; i < candidates.size(); i++) {
        size_t j = 0;
        while (j < prefix.size() && j < candidates[i].name.size() && prefix[j] == candidates[i].name[j]) j++;
        prefix.resize(j);
    }
    return prefix;
}

static bool confirm_long_listing(size_t count) {
    cout << "\nDisplay all " << count << " possibilities? (y or n)";
    cout.flush();
    char answer = 0;
    while (read(STDIN_FILENO, &answer, 1) == 1) {
        if (answer == 'y' || answer == 'Y' || answer == ' ') return true;
        if (answer == 'n' || answer == 'N' || answer == 127 || answer == '\n') return false;
    }
    return false;
}

static void list_candidates(const vector<Candidate> &candidates, const string &input) {
    if (candidates.size() <= ASK_BEFORE_LISTING || confirm_long_listing(candidates.size())) {
        cout << "\n";
        for (size_t i = 0; i < candidates.size(); i++) {
            if (i > 0) cout << "  ";
            cout << candidates[i].name << (candidates[i].is_directory ? "/" : "");
        }
    }
    // Redraw the prompt with the unchanged input; the cursor stays at its end
    cout << "\n" << getPrompt() << input;
    cout.flush();
}

void handle_tab_completion(string &input) {
    size_t word_start = input.size();
    while (word_start > 0 && !is_word_separator(input[word_start - 1])) word_start--;
    string word = input.substr(word_start);

    set<Candidate> found;
    string base;
    if (is_command_position(input, word_start) && word.find('/') == string::npos) {
        base = word;
        collect_commands(word, found);
    } else {
        collect_files(word, base, found);
    }

    vector<Candidate> candidates(found.begin(), found.end());

    if (candidates.empty()) {
        cout << '\a';
        cout.flush();
        return;
    }

    if (candidates.size() == 1) {
        string completion = candidates[0].name.substr(base.size());
        completion += candidates[0].is_directory ? "/" : " ";
        input += completion;
        cout << completion;
        cout.flush();
        return;
    }

    string common = longest_common_prefix(candidates);
    if (common.size() > base.size()) {
        string completion = common.substr(base.size());
        input += completion;
        cout << completion;
        cout.flush();
        return;
    }

    list_candidates(candidates, input);
}
