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

// Background processes reaped by the SIGCHLD handler, waiting to be reported.
// Plain arrays because the handler must not allocate memory.
static const int MAX_FINISHED = 256;
static pid_t finished_pids[MAX_FINISHED];
static int finished_status[MAX_FINISHED];
static volatile sig_atomic_t finished_count = 0;

static void record_finished(pid_t pid, int status) {
    if (finished_count < MAX_FINISHED) {
        finished_pids[finished_count] = pid;
        finished_status[finished_count] = status;
        finished_count = finished_count + 1;
    }
}

// Reaps finished background processes only. Foreground children are waited
// for explicitly (with SIGCHLD blocked), so they are never reaped here.
// The main program only changes background_processes while SIGCHLD is
// blocked, so reading it here is safe.
void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno;
    for (auto it = background_processes.begin(); it != background_processes.end(); ++it) {
        int status;
        if (waitpid(it->first, &status, WNOHANG) == it->first) {
            record_finished(it->first, status);
        }
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

bool background_jobs_finished() {
    return finished_count > 0;
}

static string describe_exit(int status) {
    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) == 0) return "exited normally";
        return "exited abnormally (exit status " + to_string(WEXITSTATUS(status)) + ")";
    }
    if (WIFSIGNALED(status)) {
        return "exited abnormally (terminated by signal " + to_string(WTERMSIG(status)) + ")";
    }
    return "exited abnormally";
}

// Reports background processes that have finished and removes them from
// background_processes.
void cleanup_background_processes() {
    sigset_t mask, old_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &old_mask);

    // Catch anything that finished without the handler running yet
    for (auto it = background_processes.begin(); it != background_processes.end(); ) {
        int status;
        pid_t result = waitpid(it->first, &status, WNOHANG);
        if (result == it->first) {
            record_finished(it->first, status);
            ++it;
        } else if (result == -1 && errno == ECHILD) {
            bool recorded = false;
            for (int i = 0; i < finished_count; i++) {
                if (finished_pids[i] == it->first) recorded = true;
            }
            if (recorded) ++it;
            else it = background_processes.erase(it);
        } else {
            ++it;
        }
    }

    for (int i = 0; i < finished_count; i++) {
        auto it = background_processes.find(finished_pids[i]);
        if (it == background_processes.end()) continue;
        cout << it->second << " with pid " << finished_pids[i] << " "
             << describe_exit(finished_status[i]) << endl;
        background_processes.erase(it);
    }
    finished_count = 0;

    sigprocmask(SIG_SETMASK, &old_mask, nullptr);
}
