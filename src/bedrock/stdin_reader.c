
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stdin_reader.h"

#if defined(_WIN32) || defined(__EMSCRIPTEN__)

int stdin_reader_init(void) { return 0; }
void stdin_reader_shutdown(void) {}
int stdin_reader_poll(char *out, size_t out_size) { (void)out; (void)out_size; return -1; }
int stdin_reader_is_active(void) { return 0; }
#else

#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <fcntl.h>

#define STDIN_LINE_MAX 1024
#define STDIN_QUEUE_SIZE 64

struct stdin_queue {
    char lines[STDIN_QUEUE_SIZE][STDIN_LINE_MAX];
    atomic_int head;
    atomic_int tail;
};

static struct stdin_queue g_queue = {0};
static pthread_t g_stdin_thread;
static atomic_int g_running = 0;
static atomic_int g_initialized = 0;

static int queue_push(const char *line) {
    int tail = atomic_load(&g_queue.tail);
    int next_tail = (tail + 1) % STDIN_QUEUE_SIZE;

    if (next_tail == atomic_load(&g_queue.head)) {
        return -1;
    }

    strncpy(g_queue.lines[tail], line, STDIN_LINE_MAX - 1);
    g_queue.lines[tail][STDIN_LINE_MAX - 1] = '\0';
    atomic_store(&g_queue.tail, next_tail);
    return 0;
}

static int queue_pop(char *out, size_t out_size) {
    int head = atomic_load(&g_queue.head);
    int tail = atomic_load(&g_queue.tail);

    if (head == tail) {
        return -1;
    }

    strncpy(out, g_queue.lines[head], out_size - 1);
    out[out_size - 1] = '\0';
    atomic_store(&g_queue.head, (head + 1) % STDIN_QUEUE_SIZE);
    return 0;
}

static void *stdin_reader_thread(void *arg) {
    (void)arg;

    char line[STDIN_LINE_MAX];

    while (atomic_load(&g_running)) {

        fd_set fds;
        struct timeval tv;

        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) {
            continue;
        }

        if (fgets(line, sizeof(line), stdin) == NULL) {

            break;
        }

        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '\0') {
            continue;
        }

        if (queue_push(line) < 0) {
            fprintf(stderr, "[stdin_reader] Queue full, dropping: %s\n", line);
        }
    }

    return NULL;
}

int stdin_reader_init(void) {
    if (atomic_load(&g_initialized)) {
        return 0;
    }

    if (isatty(STDIN_FILENO)) {

        return 0;
    }

    atomic_store(&g_running, 1);

    if (pthread_create(&g_stdin_thread, NULL, stdin_reader_thread, NULL) != 0) {
        fprintf(stderr, "[stdin_reader] Failed to create thread\n");
        atomic_store(&g_running, 0);
        return -1;
    }

    atomic_store(&g_initialized, 1);
    fprintf(stderr, "[stdin_reader] Initialized (pipe mode)\n");
    return 0;
}

void stdin_reader_shutdown(void) {
    if (!atomic_load(&g_initialized)) {
        return;
    }

    atomic_store(&g_running, 0);
    pthread_join(g_stdin_thread, NULL);
    atomic_store(&g_initialized, 0);
}

int stdin_reader_poll(char *out, size_t out_size) {
    return queue_pop(out, out_size);
}

int stdin_reader_is_active(void) {
    return atomic_load(&g_initialized) && atomic_load(&g_running);
}

#endif
