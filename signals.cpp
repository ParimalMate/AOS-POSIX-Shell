#include "signals.h"
#include "shell.h"
#include "utils.h"
#include <signal.h>
#include <sys/wait.h>
#include <cstring>
#include <cerrno>
#include <iostream>

using namespace std;

volatile sig_atomic_t prompt_interrupted = 0;

// Reaps finished background processes only. Foreground children are waited
// for explicitly (with SIGCHLD blocked), so they are never reaped here.
void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno;
    for (auto it = background_processes.begin(); it != background_processes.end(); ++it) {
        int status;
        waitpid(it->first, &status, WNOHANG);
    }
    errno = saved_errno;
}

void sigint_handler(int sig) {
    (void)sig;
    if (is_foreground_running && foreground_pgid != -1) kill(-foreground_pgid, SIGINT);
    else prompt_interrupted = 1;
}

void sigtstp_handler(int sig) {
    (void)sig;
    if (is_foreground_running && foreground_pgid != -1) kill(-foreground_pgid, SIGTSTP);
    // No foreground job: CTRL-Z has no effect on the shell.
}

void setup_signal_handlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, nullptr);

    // No SA_RESTART so a pending read() at the prompt is interrupted and the
    // input loop can discard the current line on CTRL-C.
    sa.sa_handler = sigint_handler;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);

    sa.sa_handler = sigtstp_handler;
    sa.sa_flags = 0;
    sigaction(SIGTSTP, &sa, nullptr);

    // The shell hands the terminal to foreground jobs and takes it back;
    // ignore the job-control signals it would otherwise receive while doing so.
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
}

// Removes entries for background processes that have finished.
void cleanup_background_processes() {
    sigset_t mask, old_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &old_mask);

    for (auto it = background_processes.begin(); it != background_processes.end(); ) {
        int status;
        pid_t result = waitpid(it->first, &status, WNOHANG);
        if (result == it->first || (result == -1 && errno == ECHILD)) {
            it = background_processes.erase(it);
        } else {
            ++it;
        }
    }

    sigprocmask(SIG_SETMASK, &old_mask, nullptr);
}
