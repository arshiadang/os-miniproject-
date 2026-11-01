/*
 * EXPERIMENT 4 - File and Process Handling using System Calls
 * Use case: SomaiyaSat (KJS-SRS-01)
 *
 * The parent process is the satellite's "mission controller". It runs one
 * ground pass as 3 child processes:
 *
 *   Child 1 (fork + exec) : python3 ai/generate_pass.py  -> data/telemetry.csv
 *   Child 2 (fork + exec) : python3 ai/router.py         -> data/decisions.csv
 *   Child 3 (fork only)   : C code that reads decisions.csv with read()
 *                           and writes logs/mission_report.txt with write()
 *
 * The parent waits for each child with waitpid() and checks its exit status.
 * If the AI router exits with 2 (telemetry missing), the parent goes into
 * SAFE MODE and writes a beacon-only plan itself.
 *
 * Compile : gcc -Wall -o exp4/mission exp4/mission.c
 * Run     : ./exp4/mission                 (normal pass, battery 85 %)
 *           ./exp4/mission --battery 15    (low battery -> AI may only send a beacon)
 *           ./exp4/mission --fail          (sensor failure -> safe mode)
 * Run from the project folder.
 */
#include <fcntl.h>      /* open */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>   /* waitpid */
#include <unistd.h>     /* fork, exec, read, write, close */

int log_fd;             /* file descriptor of logs/mission_log.txt */

/* write a line to the screen AND to the log file using write() */
void log_msg(const char *msg) {
    char line[300];
    int n = snprintf(line, sizeof line, "[mission pid=%d] %s\n", getpid(), msg);
    write(STDOUT_FILENO, line, n);
    write(log_fd, line, n);
}

/* fork a child, exec a program in it, wait for it, return its exit status */
int run_child(const char *stage, char *const args[]) {
    char msg[200];
    pid_t pid = fork();

    if (pid < 0) {                       /* fork failed */
        perror("fork");
        exit(1);
    }
    if (pid == 0) {                      /* CHILD process */
        execvp(args[0], args);           /* replace child with the program */
        perror("execvp");                /* only runs if exec failed */
        _exit(127);
    }

    /* PARENT process */
    snprintf(msg, sizeof msg, "created child %d for %s", pid, stage);
    log_msg(msg);

    int status;
    waitpid(pid, &status, 0);            /* wait for this child to finish */
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    snprintf(msg, sizeof msg, "child %d (%s) finished, exit status = %d", pid, stage, code);
    log_msg(msg);
    return code;
}

/* SAFE MODE: the parent itself writes a beacon-only decisions file */
void write_safe_mode_plan(void) {
    int fd = open("data/decisions.csv", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return; }
    char *header = "t_s,snr_db,battery_pct,ai_action,final_action,overridden\n";
    write(fd, header, strlen(header));
    char line[100];
    for (int t = 0; t < 600; t += 60) {
        int n = snprintf(line, sizeof line, "%d,NA,NA,NONE,SAFE_BEACON,yes\n", t);
        write(fd, line, n);
    }
    close(fd);
    log_msg("SAFE MODE: wrote beacon-only plan to data/decisions.csv");
}

/* CHILD 3: count actions in decisions.csv using read(), write report with write() */
int make_report(void) {
    const char *names[] = {"TX_TTC", "TX_SSTV", "TX_VOICE", "SAFE_BEACON", "IDLE"};
    int count[5] = {0}, total = 0;

    int in = open("data/decisions.csv", O_RDONLY);
    if (in < 0) { perror("open decisions.csv"); return 1; }

    char buf[4096], line[256];
    int len = 0, first_line = 1;
    ssize_t n;
    while ((n = read(in, buf, sizeof buf)) > 0) {          /* read 4 KB at a time */
        for (int i = 0; i < n; i++) {
            if (buf[i] != '\n') {
                if (len < 255) line[len++] = buf[i];
                continue;
            }
            line[len] = '\0';
            len = 0;
            if (first_line) { first_line = 0; continue; }   /* skip header */
            /* final_action is the 5th column: skip 4 commas */
            char *p = line;
            for (int c = 0; c < 4 && p; c++) { p = strchr(p, ','); if (p) p++; }
            if (!p) continue;
            total++;
            for (int a = 0; a < 5; a++)
                if (strncmp(p, names[a], strlen(names[a])) == 0 && p[strlen(names[a])] == ',')
                    count[a]++;
        }
    }
    close(in);

    int out = open("logs/mission_report.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { perror("open report"); return 1; }
    char text[200];
    int k = snprintf(text, sizeof text, "SOMAIYASAT MISSION REPORT (synthetic data)\n"
                                        "Total decisions: %d\n", total);
    write(out, text, k);
    for (int a = 0; a < 5; a++) {
        k = snprintf(text, sizeof text, "%-12s %d\n", names[a], count[a]);
        write(out, text, k);
    }
    close(out);
    return 0;
}

int main(int argc, char *argv[]) {
    int sensor_fail = 0;
    char *battery = "85";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fail") == 0) sensor_fail = 1;
        else if (strcmp(argv[i], "--battery") == 0 && i + 1 < argc) battery = argv[++i];
    }

    log_fd = open("logs/mission_log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0) { perror("open log (run from the project folder)"); return 1; }

    log_msg("===== ground pass started =====");
    unlink("data/telemetry.csv");        /* start every pass with fresh telemetry */

    /* Child 1: acquire telemetry */
    char *gen_args[] = {"python3", "ai/generate_pass.py", "--battery", battery,
                        sensor_fail ? "--fail" : NULL, NULL};
    int s1 = run_child("telemetry acquisition", gen_args);

    /* Child 2: AI router */
    char *ai_args[] = {"python3", "ai/router.py", NULL};
    int s2 = run_child("AI router", ai_args);

    int safe_mode = 0;
    if (s2 != 0) {                       /* 2 = telemetry missing, 3 = model missing */
        log_msg(s2 == 2 ? "AI router could not read telemetry -> entering SAFE MODE"
                        : "AI router failed -> entering SAFE MODE");
        write_safe_mode_plan();
        safe_mode = 1;
    }

    /* Child 3: report, written in C with read()/write() only (no exec) */
    pid_t pid = fork();
    if (pid == 0) {
        _exit(make_report());
    }
    char msg[100];
    snprintf(msg, sizeof msg, "created child %d for report", pid);
    log_msg(msg);
    int status;
    waitpid(pid, &status, 0);
    int s3 = WEXITSTATUS(status);
    snprintf(msg, sizeof msg, "child %d (report) finished, exit status = %d", pid, s3);
    log_msg(msg);

    printf("\nSUMMARY: acquisition=%d  router=%d  report=%d  mode=%s\n\n",
           s1, s2, s3, safe_mode ? "SAFE MODE" : "NORMAL");
    fflush(stdout);                      /* print before the raw write() output below */

    /* show the report the child wrote */
    int fd = open("logs/mission_report.txt", O_RDONLY);
    char buf[512];
    ssize_t n;
    while (fd >= 0 && (n = read(fd, buf, sizeof buf)) > 0) write(STDOUT_FILENO, buf, n);
    if (fd >= 0) close(fd);

    log_msg("===== ground pass finished =====");
    close(log_fd);
    return 0;
}
