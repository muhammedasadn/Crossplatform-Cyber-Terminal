/*
 * pty.c — Robust PTY implementation for Linux
 */

#include "pty.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <pty.h>   // openpty

/* ─────────────────────────────── */
/* Initialize PTY and spawn shell */
/* ─────────────────────────────── */
int pty_init(PTY *p, int cols, int rows) {
    if (!p) return -1;

    p->cols = cols;
    p->rows = rows;
    p->master_fd = -1;
    p->shell_pid = -1;

    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;

    int master_fd = -1;
    int slave_fd  = -1;

    if (openpty(&master_fd, &slave_fd, NULL, NULL, &ws) == -1) {
        perror("pty_init: openpty failed");
        return -1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("pty_init: fork failed");
        close(master_fd);
        close(slave_fd);
        return -1;
    }

    if (pid == 0) {
        /* ── CHILD PROCESS ── */

        close(master_fd);

        if (setsid() < 0) {
            perror("child: setsid failed");
            _exit(1);
        }

        if (ioctl(slave_fd, TIOCSCTTY, 0) < 0) {
            perror("child: TIOCSCTTY failed");
            _exit(1);
        }

        if (ioctl(slave_fd, TIOCSWINSZ, &ws) < 0) {
            perror("child: TIOCSWINSZ failed");
        }

        /* Redirect stdio */
        if (dup2(slave_fd, STDIN_FILENO) < 0 ||
            dup2(slave_fd, STDOUT_FILENO) < 0 ||
            dup2(slave_fd, STDERR_FILENO) < 0) {
            perror("child: dup2 failed");
            _exit(1);
        }

        close(slave_fd);

        /* Environment setup */
        setenv("TERM", "xterm-256color", 1);

        char cols_str[16], rows_str[16];
        snprintf(cols_str, sizeof(cols_str), "%d", cols);
        snprintf(rows_str, sizeof(rows_str), "%d", rows);

        setenv("COLUMNS", cols_str, 1);
        setenv("LINES", rows_str, 1);

        execl("/bin/bash", "-bash", NULL);

        /* If exec fails */
        perror("child: exec failed");
        _exit(1);
    }

    /* ── PARENT PROCESS ── */

    close(slave_fd);

    p->master_fd = master_fd;
    p->shell_pid = pid;

    /* Set non-blocking */
    int flags = fcntl(master_fd, F_GETFL);
    if (flags != -1) {
        fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
    }

    /* Send SIGWINCH after short delay */
    usleep(80000);
    kill(pid, SIGWINCH);

    printf("PTY initialized: PID=%d FD=%d SIZE=%dx%d\n",
           pid, master_fd, cols, rows);

    return 0;
}

/* ─────────────────────────────── */
/* Read from PTY                  */
/* ─────────────────────────────── */
int pty_read(PTY *p, char *buf, int bufsize) {
    if (!p || p->master_fd < 0 || !buf) return -1;

    int n = (int)read(p->master_fd, buf, (size_t)(bufsize - 1));

    if (n > 0) {
        buf[n] = '\0';
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("pty_read failed");
        return -1;
    }

    return n;
}

/* ─────────────────────────────── */
/* Write to PTY                   */
/* ─────────────────────────────── */
int pty_write(PTY *p, const char *buf, int len) {
    if (!p || p->master_fd < 0 || !buf) return -1;

    int written = (int)write(p->master_fd, buf, (size_t)len);

    if (written < 0) {
        perror("pty_write failed");
    }

    return written;
}

/* ─────────────────────────────── */
/* Resize PTY                     */
/* ─────────────────────────────── */
void pty_resize(PTY *p, int cols, int rows) {
    if (!p || p->master_fd < 0) return;

    p->cols = cols;
    p->rows = rows;

    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;

    if (ioctl(p->master_fd, TIOCSWINSZ, &ws) < 0) {
        perror("pty_resize: ioctl failed");
    }

    /* Update environment in shell */
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "export COLUMNS=%d LINES=%d\r", cols, rows);

    pty_write(p, cmd, (int)strlen(cmd));
}

/* ─────────────────────────────── */
/* Destroy PTY                    */
/* ─────────────────────────────── */
void pty_destroy(PTY *p) {
    if (!p) return;

    if (p->shell_pid > 0) {
        kill(p->shell_pid, SIGHUP);
        waitpid(p->shell_pid, NULL, 0);
        p->shell_pid = -1;
    }

    if (p->master_fd >= 0) {
        close(p->master_fd);
        p->master_fd = -1;
    }

    printf("PTY destroyed\n");
}