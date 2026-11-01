/*
 * EXPERIMENT 9 - Deadlock Avoidance using the Banker's Algorithm
 * Use case: SomaiyaSat (KJS-SRS-01)
 *
 * Satellite processes share 4 scarce resources:
 *   TX  = radio transmitter        (1 unit)
 *   CAM = camera for SSTV          (1 unit)
 *   PWR = power, 1 unit = 100 mW   (8 units = 800 mW, a PocketQube budget)
 *   MEM = memory, 1 unit = 32 KB   (6 units)
 *
 * The program
 *   1. computes Need = Max - Allocation and Available
 *   2. runs the safety algorithm (prints each step)
 *   3. checks 4 resource requests: GRANT / DENY (unsafe) / WAIT / ERROR
 *
 * Compile : gcc -Wall -o exp9/bankers exp9/bankers.c
 * Run     : ./exp9/bankers                 runs the 4 example requests below
 *           ./exp9/bankers 1 0 0 3 2       checks ONE custom request:
 *                                          <process 0-3> <TX> <CAM> <PWR> <MEM>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define P 4   /* processes */
#define R 4   /* resource types */

const char *pname[P] = {"P0_TTC", "P1_SSTV", "P2_VOICE", "P3_AI_ROUTER"};
const char *rname[R] = {"TX", "CAM", "PWR", "MEM"};

int total[R] = {1, 1, 8, 6};

int max[P][R] = {
    {1, 0, 2, 1},   /* P0 TT&C / housekeeping */
    {1, 1, 5, 3},   /* P1 SSTV imaging        */
    {1, 0, 3, 2},   /* P2 Codec2 voice        */
    {0, 0, 2, 2},   /* P3 AI router           */
};
int alloc[P][R] = {
    {0, 0, 1, 1},
    {0, 1, 2, 1},
    {1, 0, 1, 1},   /* voice currently holds the transmitter */
    {0, 0, 1, 1},
};
int need[P][R], avail[R];

void print_vector(int v[]) {
    printf("(");
    for (int j = 0; j < R; j++) printf("%d%s", v[j], j < R - 1 ? ", " : "");
    printf(")");
}

void print_table(void) {
    printf("%-13s %-16s %-16s %-16s\n", "PROCESS", "MAX", "ALLOCATION", "NEED");
    printf("%-13s %-16s %-16s %-16s\n", "", "TX  CAM PWR MEM", "TX  CAM PWR MEM", "TX  CAM PWR MEM");
    for (int i = 0; i < P; i++) {
        printf("%-13s", pname[i]);
        int *rows[3] = {max[i], alloc[i], need[i]};
        for (int k = 0; k < 3; k++) {
            printf(" ");
            for (int j = 0; j < R; j++) printf("%-4d", rows[k][j]);
        }
        printf("\n");
    }
    printf("Available = ");
    print_vector(avail);
    printf("\n");
}

/* Safety algorithm: returns 1 if a safe sequence exists */
int is_safe(void) {
    int work[R], finish[P] = {0}, seq[P], count = 0;
    memcpy(work, avail, sizeof work);
    printf("  Work = "); print_vector(work); printf("\n");

    int found = 1;
    while (found) {
        found = 0;
        for (int i = 0; i < P; i++) {
            if (finish[i]) continue;
            int can_run = 1;
            for (int j = 0; j < R; j++)
                if (need[i][j] > work[j]) can_run = 0;
            if (can_run) {
                for (int j = 0; j < R; j++) work[j] += alloc[i][j];   /* it finishes and releases */
                finish[i] = 1;
                seq[count++] = i;
                found = 1;
                printf("  %-12s can finish -> releases its resources -> Work = ", pname[i]);
                print_vector(work);
                printf("\n");
            }
        }
    }
    if (count < P) {
        printf("  No other process can finish with Work = ");
        print_vector(work);
        printf("\n");
        return 0;
    }
    printf("  Safe sequence: ");
    for (int i = 0; i < P; i++) printf("%s%s", pname[seq[i]], i < P - 1 ? " -> " : "\n");
    return 1;
}

/* Resource request algorithm */
void request(int p, int req[]) {
    printf("\n--- REQUEST: %s asks for ", pname[p]);
    print_vector(req);
    printf(" ---\n");

    for (int j = 0; j < R; j++)
        if (req[j] > need[p][j]) {
            printf("ERROR: asks for more %s than it declared (Need = %d). Request rejected.\n",
                   rname[j], need[p][j]);
            return;
        }
    for (int j = 0; j < R; j++)
        if (req[j] > avail[j]) {
            printf("WAIT: only %d %s free right now. %s must wait.\n", avail[j], rname[j], pname[p]);
            return;
        }

    /* pretend to give the resources, then check safety */
    for (int j = 0; j < R; j++) { avail[j] -= req[j]; alloc[p][j] += req[j]; need[p][j] -= req[j]; }
    if (is_safe()) {
        printf("GRANT: system stays in a SAFE state.\n");
    } else {
        for (int j = 0; j < R; j++) { avail[j] += req[j]; alloc[p][j] -= req[j]; need[p][j] += req[j]; }
        printf("DENY: this would make the system UNSAFE (possible deadlock). Rolled back.\n");
    }
}

int main(int argc, char *argv[]) {
    /* Need and Available */
    for (int j = 0; j < R; j++) {
        int used = 0;
        for (int i = 0; i < P; i++) {
            need[i][j] = max[i][j] - alloc[i][j];
            used += alloc[i][j];
        }
        avail[j] = total[j] - used;
    }

    printf("SomaiyaSat resource manager - Banker's Algorithm\n");
    printf("Total: TX=%d CAM=%d PWR=%d (x100 mW) MEM=%d (x32 KB)\n\n", total[0], total[1], total[2], total[3]);
    print_table();

    printf("\nIs the current state safe?\n");
    printf(is_safe() ? "RESULT: SAFE\n" : "RESULT: UNSAFE\n");

    /* custom request from the command line (used by the website) */
    if (argc == 6) {
        int p = atoi(argv[1]), req[R];
        if (p < 0 || p >= P) { printf("ERROR: process must be 0-%d\n", P - 1); return 1; }
        for (int j = 0; j < R; j++) {
            req[j] = atoi(argv[j + 2]);
            if (req[j] < 0) { printf("ERROR: request values cannot be negative\n"); return 1; }
        }
        request(p, req);
        return 0;
    }

    /* 1. SSTV wants to pre-charge during eclipse: fits in Available, but leaves 0 of everything */
    int r1[R] = {0, 0, 3, 2};
    request(1, r1);                       /* expected: DENY */

    /* 2. AI router asks for 1 more power unit and 1 memory block */
    int r2[R] = {0, 0, 1, 1};
    request(3, r2);                       /* expected: GRANT */

    /* 3. SSTV asks again, but not enough power is free now */
    int r3[R] = {0, 0, 3, 2};
    request(1, r3);                       /* expected: WAIT */

    /* 4. SSTV asks for more power than its declared maximum (invalid request) */
    int r4[R] = {0, 0, 4, 0};
    request(1, r4);                       /* expected: ERROR */

    printf("\nFinal state:\n");
    print_table();
    return 0;
}
