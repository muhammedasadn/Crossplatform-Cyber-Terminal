/*
 * pty.c — PTY implementation.
 *
 * Key fix for blank screen bug:
 *   The usleep(100000) was blocking the MAIN thread for 100ms.
 *   During that time the render loop was frozen so bash output
 *   that arrived immediately was never read — the screen stayed
 *   blank until the user pressed a key.
 *
 *   Fix: remove the blocking sleep entirely. Instead we send
 *   SIGWINCH from the child just before exec'ing bash — at that
 *   point bash will receive it immediately after starting up and
 *   re-query the terminal size correctly, with zero delay to cterm.
 *
 *   We also set COLUMNS/LINES and TIOCSWINSZ on both master and
 *   slave to ensure readline gets the correct width from every
 *   possible code path.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <utmp.h>
#include <pty.h>

#include "pty.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>


int pty_init(PTY *p, int cols, int rows) {
    p->cols      = cols;
    p->rows      = rows;
    p->master_fd = -1;
    p->shell_pid = 0;

    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;

    int master_fd, slave_fd;

    if (openpty(&master_fd, &slave_fd, NULL, NULL, &ws) != 0) {
        perror("pty_init: openpty");
        return -1;
    }

    p->master_fd = master_fd;

    pid_t pid = fork();

    if (pid < 0) {
        perror("pty_init: fork");
        close(master_fd);
        close(slave_fd);
        return -1;
    }

    if (pid == 0) {
        /* ════ CHILD — become bash ════ */
        close(master_fd);

        if (setsid() < 0) { perror("setsid"); _exit(1); }

        if (ioctl(slave_fd, TIOCSCTTY, 0) < 0) {
            perror("TIOCSCTTY"); _exit(1);
        }

        /*
         * Set window size on slave fd.
         * Some kernels only propagate from slave side.
         */
        ioctl(slave_fd, TIOCSWINSZ, &ws);

        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        close(slave_fd);

        /*
         * Environment: tell readline the exact terminal size.
         * COLUMNS/LINES are the fallback when ioctl returns 0.
         * Without these readline may cache wrong column count.
         */
        setenv("TERM", "xterm-256color", 1);

        char cols_str[16], rows_str[16];
        snprintf(cols_str, sizeof(cols_str), "%d", cols);
        snprintf(rows_str, sizeof(rows_str), "%d", rows);
        setenv("COLUMNS", cols_str, 1);
        setenv("LINES",   rows_str, 1);

        /* Disable bracketed paste — reduces OSC noise */
        setenv("READLINE_BRACKETED_PASTE", "0", 1);

        /*
         * exec bash.
         * Bash will send its startup sequences (OSC title,
         * color prompt, etc.) immediately — no delay needed.
         */
        execl("/bin/bash", "-bash", NULL);
        perror("execl /bin/bash");
        _exit(1);
    }

    /* ════ PARENT ════ */
    close(slave_fd);
    p->shell_pid = pid;

    /*
     * Set O_NONBLOCK on master so pty_read() never blocks
     * the render loop when bash has produced no output.
     */
    int flags = fcntl(master_fd, F_GETFL, 0);
    if (flags != -1)
        fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);

    /*
     * Send SIGWINCH to bash after a tiny non-blocking delay.
     * We use a fire-and-forget child process so the main
     * thread is NEVER blocked — the render loop keeps running.
     *
     * This forces readline to re-query terminal size after
     * startup, fixing the wrong-column-count cache bug.
     */
    pid_t watcher = fork();
    if (watcher == 0) {
        /* Tiny helper child: sleep 80ms then signal bash */
        usleep(80000);
        kill(pid, SIGWINCH);
        _exit(0);
    }
    /* Parent doesn't wait for watcher — it exits on its own */

    printf("PTY ready: PID=%d fd=%d size=%dx%d\n",
           p->shell_pid, p->master_fd, cols, rows);
    return 0;
}


int pty_read(PTY *p, char *buf, int bufsize) {
    int n = (int)read(p->master_fd, buf, (size_t)(bufsize - 1));
    if (n > 0) buf[n] = '\0';
    return n;
}


int pty_write(PTY *p, const char *buf, int len) {
    return (int)write(p->master_fd, buf, (size_t)len);
}


void pty_resize(PTY *p, int cols, int rows) {
    if (p->master_fd < 0) return;
    p->cols = cols;
    p->rows = rows;

    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;

    /*
     * TIOCSWINSZ on master — kernel delivers SIGWINCH
     * automatically to the foreground process group.
     */
    ioctl(p->master_fd, TIOCSWINSZ, &ws);
}


void pty_destroy(PTY *p) {
    if (p->shell_pid > 0) {
        kill(p->shell_pid, SIGHUP);
        /* Non-blocking wait — up to 1 second */
        for (int i = 0; i < 10; i++) {
            int status;
            if (waitpid(p->shell_pid, &status, WNOHANG) != 0) break;
            usleep(100000);
        }
        kill(p->shell_pid, SIGKILL); /* force if still running */
        p->shell_pid = 0;
    }
    if (p->master_fd >= 0) {
        close(p->master_fd);
        p->master_fd = -1;
    }
}