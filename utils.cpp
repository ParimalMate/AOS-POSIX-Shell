#include "utils.h"
#include "shell.h"
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <iomanip>
#include <limits.h>
#include <fcntl.h>
#ifdef __linux__
#include <sys/sysmacros.h>
#endif

using namespace std;

vector<string> splitCommands(string input) {
    vector<string> commands;
    vector<char> buffer(input.begin(), input.end());
    buffer.push_back('\0');

    char *token = strtok(buffer.data(), ";");
    while (token != nullptr) {
        string command = token;
        trim(command);
        if (!command.empty()) commands.push_back(command);
        token = strtok(nullptr, ";");
    }
    return commands;
}

vector<string> splitArgs(const string& s) {
    vector<string> args;
    string token;
    bool inQuotes = false;
    char quoteChar = '\0';

    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if ((c == '"' || c == '\'') && !inQuotes) {
            inQuotes = true;
            quoteChar = c;
        }
        else if (inQuotes && c == quoteChar) {
            inQuotes = false;
        }
        else if (!inQuotes && isspace(static_cast<unsigned char>(c))) {
            if (!token.empty()) {
                args.push_back(token);
                token.clear();
            }
        }
        else if (!inQuotes && (c == '<' || c == '>')) {
            // Redirection operators are separate tokens even without spaces
            if (!token.empty()) {
                args.push_back(token);
                token.clear();
            }
            if (c == '>' && i + 1 < s.size() && s[i + 1] == '>') {
                args.push_back(">>");
                i++;
            } else {
                args.push_back(string(1, c));
            }
        } else {
            token.push_back(c);
        }
    }

    if (!token.empty()) args.push_back(token);
    return args;
}

void trim(string &s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == string::npos) {
        s = "";
    } else {
        size_t end = s.find_last_not_of(" \t");
        s = s.substr(start, end - start + 1);
    }
}

string getPrompt() {
    char hostname[1024];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strcpy(hostname, "unknown");
    }
    hostname[sizeof(hostname) - 1] = '\0';
    struct passwd *pw = getpwuid(getuid());
    string username = pw ? pw->pw_name : to_string(getuid());
    string displayPath = getDisplayPath();
    return username + "@" + hostname + ":" + displayPath + "> ";
}

// Replaces a leading shell-home prefix in `path` with "~".
string abbreviateHome(const string &path) {
    if (path == home_directory) return "~";
    if (home_directory != "/" && path.size() > home_directory.size() &&
        path.compare(0, home_directory.size(), home_directory) == 0 &&
        path[home_directory.size()] == '/') {
        return "~" + path.substr(home_directory.size());
    }
    return path;
}

string getDisplayPath() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) return "?";
    return abbreviateHome(cwd);
}

string getBaseName(string path) {
    size_t pos = path.find_last_of('/');
    return (pos == string::npos) ? path : path.substr(pos + 1);
}

string getFilePermissions(mode_t mode) {
    string permissions;
    if (S_ISDIR(mode)) permissions += 'd';
    else if (S_ISLNK(mode)) permissions += 'l';
    else if (S_ISCHR(mode)) permissions += 'c';
    else if (S_ISBLK(mode)) permissions += 'b';
    else if (S_ISFIFO(mode)) permissions += 'p';
    else if (S_ISSOCK(mode)) permissions += 's';
    else permissions += '-';

    permissions += (mode & S_IRUSR) ? 'r' : '-';
    permissions += (mode & S_IWUSR) ? 'w' : '-';
    if (mode & S_ISUID) permissions += (mode & S_IXUSR) ? 's' : 'S';
    else permissions += (mode & S_IXUSR) ? 'x' : '-';
    permissions += (mode & S_IRGRP) ? 'r' : '-';
    permissions += (mode & S_IWGRP) ? 'w' : '-';
    if (mode & S_ISGID) permissions += (mode & S_IXGRP) ? 's' : 'S';
    else permissions += (mode & S_IXGRP) ? 'x' : '-';
    permissions += (mode & S_IROTH) ? 'r' : '-';
    permissions += (mode & S_IWOTH) ? 'w' : '-';
    if (mode & S_ISVTX) permissions += (mode & S_IXOTH) ? 't' : 'T';
    else permissions += (mode & S_IXOTH) ? 'x' : '-';
    return permissions;
}

string formatFileSize(off_t size) {
    stringstream ss;
    ss << size;
    return ss.str();
}

// Same rule as GNU ls: show the time for files modified in the last six
// months, otherwise show the year.
static string formatModTime(time_t mtime) {
    time_t now = time(nullptr);
    const time_t six_months = 31556952 / 2;
    bool recent = (mtime > now - six_months) && (mtime <= now);

    struct tm *timeInfo = localtime(&mtime);
    if (timeInfo == nullptr) return "?";
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), recent ? "%b %e %H:%M" : "%b %e  %Y", timeInfo);
    return timeBuf;
}

struct LongEntry {
    string permissions, links, owner, group, size, major, minor, date, name;
    bool is_device;
};

static string padLeft(const string &s, size_t width) {
    return s.size() >= width ? s : string(width - s.size(), ' ') + s;
}

static string padRight(const string &s, size_t width) {
    return s.size() >= width ? s : s + string(width - s.size(), ' ');
}

void printLongListing(const string &dir, const vector<string> &names, bool showTotal) {
    vector<LongEntry> entries;
    long long totalBlocks = 0;
    size_t linksW = 0, ownerW = 0, groupW = 0, sizeW = 0, majorW = 0, minorW = 0;

    for (const string &name : names) {
        string fullPath;
        if (dir.empty()) fullPath = name;
        else if (dir[dir.size() - 1] == '/') fullPath = dir + name;
        else fullPath = dir + "/" + name;

        struct stat statBuf;
        if (lstat(fullPath.c_str(), &statBuf) != 0) {
            perror(("ls: cannot access '" + fullPath + "'").c_str());
            continue;
        }
        totalBlocks += statBuf.st_blocks;

        LongEntry e;
        e.permissions = getFilePermissions(statBuf.st_mode);
        e.links = to_string(static_cast<unsigned long>(statBuf.st_nlink));
        struct passwd *pw = getpwuid(statBuf.st_uid);
        struct group *gr = getgrgid(statBuf.st_gid);
        e.owner = pw ? pw->pw_name : to_string(statBuf.st_uid);
        e.group = gr ? gr->gr_name : to_string(statBuf.st_gid);
        e.is_device = S_ISCHR(statBuf.st_mode) || S_ISBLK(statBuf.st_mode);
        if (e.is_device) {
            e.major = to_string(static_cast<unsigned long>(major(statBuf.st_rdev)));
            e.minor = to_string(static_cast<unsigned long>(minor(statBuf.st_rdev)));
            majorW = max(majorW, e.major.size());
            minorW = max(minorW, e.minor.size());
        } else {
            e.size = formatFileSize(statBuf.st_size);
            sizeW = max(sizeW, e.size.size());
        }
        e.date = formatModTime(statBuf.st_mtime);
        e.name = name;
        if (S_ISLNK(statBuf.st_mode)) {
            char linkTarget[PATH_MAX];
            ssize_t len = readlink(fullPath.c_str(), linkTarget, sizeof(linkTarget) - 1);
            if (len != -1) {
                linkTarget[len] = '\0';
                e.name += " -> " + string(linkTarget);
            }
        }

        linksW = max(linksW, e.links.size());
        ownerW = max(ownerW, e.owner.size());
        groupW = max(groupW, e.group.size());
        entries.push_back(e);
    }

    if (majorW > 0) sizeW = max(sizeW, majorW + 2 + minorW);

    if (showTotal) {
        // st_blocks counts 512-byte units; ls reports 1K blocks, rounded up
        cout << "total " << (totalBlocks * 512 + 1023) / 1024 << "\n";
    }

    for (const LongEntry &e : entries) {
        string sizeField = e.is_device
            ? padLeft(padLeft(e.major, majorW) + ", " + padLeft(e.minor, minorW), sizeW)
            : padLeft(e.size, sizeW);
        cout << e.permissions << " "
             << padLeft(e.links, linksW) << " "
             << padRight(e.owner, ownerW) << " "
             << padRight(e.group, groupW) << " "
             << sizeField << " "
             << e.date << " "
             << e.name << "\n";
    }
    cout.flush();
}

bool search_recursive(const string &currentDir, const string &target) {
    DIR *dir = opendir(currentDir.c_str());
    if (dir == nullptr) return false;

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        string name = entry->d_name;
        if (name == "." || name == "..") continue;
        string fullPath = currentDir + "/" + name;
        if (name == target) {
            closedir(dir);
            return true;
        }

        // lstat so symbolic links to directories cannot cause endless recursion
        struct stat statBuf;
        if (lstat(fullPath.c_str(), &statBuf) == 0 && S_ISDIR(statBuf.st_mode)) {
            bool foundInSubdir = search_recursive(fullPath, target);
            if (foundInSubdir) {
                closedir(dir);
                return true;
            }
        }
    }
    closedir(dir);
    return false;
}

string get_exe_path(pid_t pid) {
    char exePath[PATH_MAX];
    string procExe = "/proc/" + to_string(pid) + "/exe";
    ssize_t len = readlink(procExe.c_str(), exePath, sizeof(exePath) - 1);
    if (len == -1) return "Cannot access executable path";
    exePath[len] = '\0';
    return string(exePath);
}

// Reads the state from /proc/<pid>/stat and appends "+" when the process
// belongs to the terminal's foreground process group.
string get_process_status(pid_t pid) {
    string statPath = "/proc/" + to_string(pid) + "/stat";
    FILE* file = fopen(statPath.c_str(), "r");
    if (file == nullptr) return "Cannot access status";

    char buffer[2048];
    size_t bytesRead = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[bytesRead] = '\0';

    // The command name is in parentheses and may contain spaces
    char *closeParen = strrchr(buffer, ')');
    if (closeParen == nullptr) return "Unknown";

    char state;
    int ppid, pgrp, session, ttyNr, tpgid;
    if (sscanf(closeParen + 1, " %c %d %d %d %d %d", &state, &ppid, &pgrp, &session, &ttyNr, &tpgid) != 6) {
        return "Unknown";
    }

    string status(1, state == 't' ? 'T' : state);
    if (tpgid > 0 && pgrp == tpgid) status += "+";
    return status;
}

string get_virtual_memory(pid_t pid) {
    string statusPath = "/proc/" + to_string(pid) + "/status";
    FILE* file = fopen(statusPath.c_str(), "r");
    if (file == nullptr) return "0";

    char line[256];
    string memory;
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "VmSize:", 7) == 0) {
            char* colonPos = strchr(line, ':');
            if (colonPos) {
                memory = colonPos + 1;
                trim(memory);
                size_t kbPos = memory.find("kB");
                if (kbPos != string::npos) {
                    memory = memory.substr(0, kbPos);
                }
                trim(memory);
            }
            break;
        }
    }
    fclose(file);
    return memory.empty() ? "0" : memory;
}
