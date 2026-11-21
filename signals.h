#ifndef SIGNALS_H
#define SIGNALS_H

void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigtstp_handler(int sig);

#endif