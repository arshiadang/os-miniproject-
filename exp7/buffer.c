/*
 * EXPERIMENT 7 - Producer-Consumer using Semaphores and Mutex
 * Use case: SomaiyaSat (KJS-SRS-01)
 *
 * The satellite has ONE radio but THREE sensors. Sensor threads (producers)
 * put a few telemetry frames into a shared buffer of 3 slots. The radio thread
 * (consumer) takes frames out and transmits them.
 *
 *   empty  semaphore = number of free slots    (producer waits if buffer FULL)
 *   full   semaphore = number of filled slots  (consumer waits if buffer EMPTY)
 *   mutex            = only one thread changes the buffer / frame number at a time
 *
 * Every frame gets a unique frame number. At the end we check that every
 * number was transmitted exactly once. The unsafe mode deliberately starts
 * all producers together on their first write so the race is easy to see.
 *
 *   ./exp7/buffer          SAFE   : with mutex   -> no duplicates, no lost frames
 *   ./exp7/buffer unsafe   UNSAFE : mutex removed -> race condition (duplicates / lost frames)
 *
 * Compile : gcc -Wall -pthread -o exp7/buffer exp7/buffer.c
 */
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 3
#define PRODUCERS 3
#define FRAMES_EACH 4
#define TOTAL (PRODUCERS * FRAMES_EACH)

typedef struct { int frame_no; int sensor; } Frame;

Frame buffer[BUFFER_SIZE];
int in = 0, out = 0;          /* where to put / take next */
int next_frame_no = 0;        /* shared counter -> must be protected */

sem_t empty, full;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int use_mutex = 1;
pthread_barrier_t unsafe_start;

int sent[TOTAL];              /* how many times each frame number was transmitted */
const char *sensor_name[PRODUCERS] = {"battery", "temperature", "camera"};
FILE *logfile;

void lock(void)   { if (use_mutex) pthread_mutex_lock(&mutex); }
void unlock(void) { if (use_mutex) pthread_mutex_unlock(&mutex); }

void *sensor_thread(void *arg) {                 /* PRODUCER */
    int id = *(int *)arg;
    for (int i = 0; i < FRAMES_EACH; i++) {
        sem_wait(&empty);                        /* wait for a free slot */
        if (!use_mutex && i == 0)
            pthread_barrier_wait(&unsafe_start);
        lock();
        /* ---------- critical section ---------- */
        int no = next_frame_no;
        if (!use_mutex && i == 0) usleep(1000);
        next_frame_no = no + 1;
        int slot = in;
        if (!use_mutex && i == 0) usleep(1000);
        buffer[slot].frame_no = no;
        buffer[slot].sensor = id;
        in = (slot + 1) % BUFFER_SIZE;
        /* --------------------------------------- */
        unlock();
        sem_post(&full);                         /* one more filled slot */
    }
    return NULL;
}

void *radio_thread(void *arg) {                  /* CONSUMER */
    (void)arg;
    for (int i = 0; i < TOTAL; i++) {
        sem_wait(&full);                         /* wait for a filled slot */
        lock();
        Frame f = buffer[out];
        out = (out + 1) % BUFFER_SIZE;
        unlock();
        sem_post(&empty);                        /* one more free slot */

        if (f.frame_no >= 0 && f.frame_no < TOTAL) sent[f.frame_no]++;
        fprintf(logfile, "TX frame %03d from %s\n", f.frame_no, sensor_name[f.sensor]);
        usleep(100);                             /* time to transmit */
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "unsafe") == 0) use_mutex = 0;

    logfile = fopen("logs/buffer_log.txt", "w");
    if (!logfile) { perror("logs/buffer_log.txt (run from the project folder)"); return 1; }

    sem_init(&empty, 0, BUFFER_SIZE);            /* all slots free at start */
    sem_init(&full, 0, 0);                       /* no filled slots at start */
    if (!use_mutex) pthread_barrier_init(&unsafe_start, NULL, PRODUCERS);

    printf("Mode: %s | %d sensor threads x %d frames, buffer = %d slots\n",
           use_mutex ? "SAFE (semaphores + mutex)" : "UNSAFE (mutex removed)",
           PRODUCERS, FRAMES_EACH, BUFFER_SIZE);

    pthread_t producers[PRODUCERS], radio;
    int ids[PRODUCERS];
    pthread_create(&radio, NULL, radio_thread, NULL);
    for (int i = 0; i < PRODUCERS; i++) {
        ids[i] = i;
        pthread_create(&producers[i], NULL, sensor_thread, &ids[i]);
    }
    for (int i = 0; i < PRODUCERS; i++) pthread_join(producers[i], NULL);
    pthread_join(radio, NULL);
    if (!use_mutex) pthread_barrier_destroy(&unsafe_start);
    fclose(logfile);

    int duplicates = 0, lost = 0;
    for (int i = 0; i < TOTAL; i++) {
        if (sent[i] == 0) lost++;
        if (sent[i] > 1) duplicates += sent[i] - 1;
    }
    printf("Frames created       : %d (should be %d)\n", next_frame_no, TOTAL);
    printf("Frames transmitted   : %d\n", TOTAL);
    printf("Duplicate frames     : %d\n", duplicates);
    printf("Lost frames          : %d\n", lost);
    if (duplicates == 0 && lost == 0 && next_frame_no == TOTAL)
        printf("RESULT: NO RACE CONDITION - every frame sent exactly once\n");
    else
        printf("RESULT: RACE CONDITION - frames were duplicated or lost\n");
    printf("Log of transmitted frames: logs/buffer_log.txt\n");
    return 0;
}
