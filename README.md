# Custom POSIX Shell

This project is an implementation of an interactive **POSIX shell** in C++.
It supports built-in commands, execution of system commands in the foreground and background, I/O redirection, pipelines, signal handling, command history and TAB autocompletion.

---

## 📂 Project Structure

```
.
├── main.cpp           # Entry point: read loop, ';' splitting, '&' detection, built-in dispatch
├── shell.h            # Shared global state and declarations
├── builtins.cpp/.h    # cd, echo, pwd, ls, pinfo, search, history
├── execution.cpp/.h   # fork/exec of system commands, background jobs, pipelines
├── redirection.cpp/.h # <, > and >> handling (also inside pipelines)
├── history.cpp/.h     # Persistent history and the raw-mode line reader (UP/DOWN keys)
├── autocomplete.cpp/.h# TAB completion of commands and files/directories
├── signals.cpp/.h     # CTRL-C, CTRL-Z, SIGCHLD handling and reaping of background jobs
├── utils.cpp/.h       # Tokenizing, prompt, ls -l formatting, /proc helpers for pinfo
└── Makefile           # Build script
```

| Requirement                         | File(s)                                  |
|-------------------------------------|------------------------------------------|
| 1. Prompt `<user@host:dir>`         | `utils.cpp` (`getPrompt`, `getDisplayPath`) |
| 2. `cd`, `echo`, `pwd`              | `builtins.cpp`                           |
| 3. `ls` with `-a`, `-l`             | `builtins.cpp`, `utils.cpp` (`printLongListing`) |
| 4. Foreground / background commands | `execution.cpp`, `main.cpp`              |
| 5. `pinfo`                          | `builtins.cpp`, `utils.cpp`              |
| 6. `search`                         | `builtins.cpp`, `utils.cpp`              |
| 7. I/O redirection                  | `redirection.cpp`                        |
| 8. Pipelines                        | `execution.cpp`                          |
| 9. Redirection with pipelines       | `redirection.cpp`, `execution.cpp`       |
| 10. CTRL-Z, CTRL-C, CTRL-D          | `signals.cpp`, `history.cpp`, `main.cpp` |
| 11. TAB autocomplete                | `autocomplete.cpp`                       |
| 12. `history`, UP/DOWN arrow keys   | `history.cpp`, `builtins.cpp`            |

---

## ⚙️ Features

- **Prompt** – `<username@hostname:path>`; the directory the shell was started from is shown as `~` (subdirectories as `~/dir/sub`, anything outside it as an absolute path).
- **Command lists** – several commands separated by `;` (split with `strtok`). Extra spaces and tabs are ignored.
- **`&`** – every command followed by `&` runs in the background, e.g. `gedit & vim & ls` starts two background jobs and runs `ls` in the foreground.
- **Built-in commands** (implemented without `exec`)
  - `cd` – supports `.`, `..`, `-`, `~`, `~/path`; no argument goes to the shell's home; more than one argument prints `Invalid arguments`
  - `pwd` – prints the absolute working directory
  - `echo` – prints its arguments
  - `ls` – flags `-a`, `-l` in any combination and order (`-la`, `-al`, `-l dir1 -a dir2`), with `.`, `..`, `~`, files and directories. `-l` output follows GNU `ls -l` (total, column alignment, dates)
  - `pinfo [pid]` – process status (`R`, `S`, `Z`, `T`, with `+` for the foreground process group), virtual memory and executable path, read from `/proc`
  - `search <name>` – recursively searches the current directory, prints `True`/`False`
  - `history [num]` – last 10 commands by default, or the last `num`. 20 commands are stored in `~/.myshell_history` (in `$HOME`) and kept across sessions, wherever the shell is started from. Consecutive duplicates are stored once
  - `exit` – terminates the shell
- **System commands** – run with `fork` + `execvp`, in the foreground or in the background with `&` (the PID is printed). Multiple background jobs are supported. The `SIGCHLD` handler reaps finished background jobs and the shell reports them, e.g. `sleep with pid 4242 exited normally` or `... exited abnormally (terminated by signal 9)`.
- **I/O redirection** – `<`, `>` (truncate), `>>` (append); output files are created with permissions `0644`; a missing input file prints an error.
- **Pipelines** – any number of `|`-connected commands, combined with redirection (e.g. `cat < in.txt | wc -l > lines.txt`). A redirection on any command of a pipeline takes precedence over the pipe, as in bash. Built-ins work inside pipelines.
- **Signals**
  - `CTRL-C` – interrupts the foreground job; at the prompt it discards the current line
  - `CTRL-Z` – stops the foreground job and moves it to the background
  - `CTRL-D` – logs out of the shell
- **Autocomplete (TAB)**
  - First word of a command (or after `;`, `|`, `&`) completes command names from built-ins and `$PATH`
  - Other words complete files/directories in the current directory (paths like `dir/fi` also work)
  - One match is completed (a `/` is added for directories); several matches fill the common prefix, or are listed below the prompt if there is no common prefix to add
  - A TAB inside pasted text (or input that is not a terminal) is treated as a space, so pasted commands containing tabs still work
- **History navigation** – UP/DOWN arrow keys cycle through earlier commands, which can then be edited and run.

---

## 🛠️ Building the Project

Ensure you have **g++** installed. Then run:

```bash
make
```

This will generate the executable named **`shell`**.

To clean compiled files:

```bash
make clean
```

---

## 🚀 Running the Shell

Start the shell with:

```bash
./shell
```

You'll see a prompt of the form:

```
<username@system_name:current_directory>
```

---

## 📖 Example Usage

```bash
<user@host:~> cd newdir/test
<user@host:~/newdir/test> cd ..
<user@host:~/newdir> pwd
/home/user/project/newdir
<user@host:~/newdir> cd
<user@host:~> ls -al test_dir
<user@host:~> sleep 30 &
[12345]
<user@host:~> pinfo 12345
Process Status -- S
memory -- 5432 {Virtual Memory}
Executable Path -- /usr/bin/sleep
<user@host:~> search main.cpp
True
<user@host:~> cat sample.txt | head -7 | tail -5 > out.txt
<user@host:~> history 3
search main.cpp
cat sample.txt | head -7 | tail -5 > out.txt
history 3
<user@host:~> exit
```

---

## 👩‍💻 Contributors

- Parimal Mate – Developer

---

## 📜 License

This project is developed as part of an academic assignment and is free for educational use.
