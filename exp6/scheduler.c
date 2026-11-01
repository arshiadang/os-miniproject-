/*
 * EXPERIMENT 6 - Shortest Job First and Round Robin Scheduling
 * Use case: SomaiyaSat (KJS-SRS-01)
 *
 * During one ground pass the satellite has several downlink jobs waiting for
 * its single radio. Each job is treated as a process:
 *     burst time = how long the radio is busy sending it (seconds)
 *
 * Jobs are read from exp6/tasks.txt   (format: id name arrival burst)
 * The program runs:
 *     - SJF (non-preemptive)
 *     - Round Robin for every time quantum given on the command line
 * and prints a Gantt chart, waiting/turnaround time per job and the averages.
 *
 * Compile : gcc -Wall -o exp6/scheduler exp6/scheduler.c
 * Run     : ./exp6/scheduler exp6/tasks.txt 4 8 16
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX 50

typedef struct {
    char id[10], name[30];
    int arrival, burst;
    int remaining, finish;
} Job;

Job jobs[MAX];
int n = 0;

/* Gantt chart pieces */
char g_id[500][10];
int g_end[500], g_count = 0;

void gantt_add(const char *id, int end) {
    /* join with the previous piece if the same job continues */
    if (g_count > 0 && strcmp(g_id[g_count - 1], id) == 0) {
        g_end[g_count - 1] = end;
        return;
    }
    strcpy(g_id[g_count], id);
    g_end[g_count++] = end;
}

void gantt_print(void) {
    printf("Gantt chart:\n|");
    for (int i = 0; i < g_count; i++) printf(" %s |", g_id[i]);
    printf("\n0");
    for (int i = 0; i < g_count; i++) printf("%*d", (int)strlen(g_id[i]) + 3, g_end[i]);
    printf("\n");
}

int load_jobs(const char *file) {
    FILE *f = fopen(file, "r");
    if (!f) { perror(file); return 0; }
    char line[200];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;      /* skip comments */
        Job j;
        if (sscanf(line, "%9s %29s %d %d", j.id, j.name, &j.arrival, &j.burst) != 4) continue;
        if (j.burst <= 0 || j.arrival < 0) {
            printf("ERROR: job %s has invalid arrival/burst (%d, %d)\n", j.id, j.arrival, j.burst);
            fclose(f);
            return 0;
        }
        jobs[n++] = j;
    }
    fclose(f);
    return n > 0;
}

void reset(void) {
    for (int i = 0; i < n; i++) { jobs[i].remaining = jobs[i].burst; jobs[i].finish = -1; }
    g_count = 0;
}

void print_results(void) {
    float total_wt = 0, total_tat = 0;
    printf("%-4s %-20s %7s %5s %6s %5s %5s\n", "ID", "JOB", "ARRIVE", "BURST", "FINISH", "TAT", "WT");
    for (int i = 0; i < n; i++) {
        int tat = jobs[i].finish - jobs[i].arrival;   /* turnaround = finish - arrival */
        int wt = tat - jobs[i].burst;                 /* waiting = turnaround - burst  */
        total_tat += tat;
        total_wt += wt;
        printf("%-4s %-20s %7d %5d %6d %5d %5d\n", jobs[i].id, jobs[i].name,
               jobs[i].arrival, jobs[i].burst, jobs[i].finish, tat, wt);
    }
    printf("Average waiting time    = %.2f s\n", total_wt / n);
    printf("Average turnaround time = %.2f s\n\n", total_tat / n);
}

/* ---------- SJF (non-preemptive) ---------- */
void sjf(void) {
    reset();
    int time = 0, done = 0;
    while (done < n) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (jobs[i].finish != -1 || jobs[i].arrival > time) continue;
            if (best == -1 || jobs[i].burst < jobs[best].burst) best = i;
        }
        if (best == -1) { time++; continue; }          /* radio idle, nothing arrived */
        time += jobs[best].burst;                      /* run the shortest job fully */
        jobs[best].finish = time;
        gantt_add(jobs[best].id, time);
        done++;
    }
    printf("===== SJF (non-preemptive) =====\n");
    gantt_print();
    print_results();
}

/* ---------- Round Robin ---------- */
void round_robin(int q) {
    reset();
    int queue[1000], front = 0, rear = 0;
    int added[MAX] = {0};
    int time = 0, done = 0;

    while (done < n) {
        /* add every job that has arrived */
        for (int i = 0; i < n; i++)
            if (!added[i] && jobs[i].arrival <= time) { queue[rear++] = i; added[i] = 1; }

        if (front == rear) { time++; continue; }       /* queue empty -> idle */

        int p = queue[front++];
        int run = jobs[p].remaining < q ? jobs[p].remaining : q;
        time += run;
        jobs[p].remaining -= run;
        gantt_add(jobs[p].id, time);

        /* jobs that arrived during this slice join the queue first */
        for (int i = 0; i < n; i++)
            if (!added[i] && jobs[i].arrival <= time) { queue[rear++] = i; added[i] = 1; }

        if (jobs[p].remaining == 0) { jobs[p].finish = time; done++; }
        else queue[rear++] = p;                        /* back of the queue */
    }
    printf("===== Round Robin, time quantum = %d s =====\n", q);
    gantt_print();
    print_results();
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s exp6/tasks.txt <quantum> [more quanta]\n", argv[0]);
        return 1;
    }
    if (!load_jobs(argv[1])) return 1;

    printf("SomaiyaSat ground pass: %d downlink jobs\n\n", n);
    sjf();
    for (int k = 2; k < argc; k++) {
        int q = atoi(argv[k]);
        if (q <= 0) { printf("ERROR: time quantum must be > 0 (got '%s')\n", argv[k]); return 1; }
        round_robin(q);
    }
    return 0;
}
