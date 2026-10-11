/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read-only dynamic thread sampler for the Encore thread controller.
 *
 * Usage: sampler PID OUTPUT_PATH
 *
 * The existing thread/bin/sampler is the frame-time probe used by guardian.
 * This source is built as thread-sampler so both tools can coexist.
 */
#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define SAMPLE_DELAY_US 250000U
#define MAX_THREADS 4096U
#define TOP_THREADS 4U

struct thread_sample {
    int tid;
    uint64_t cpu_time;
};

struct delta_sample {
    int tid;
    uint64_t delta;
};

static int parse_positive_pid(const char *text, pid_t *pid) {
    char *end = NULL;
    unsigned long value;

    if (!text || !*text) {
        return -1;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || end == text || *end || value < 1 || value > INT_MAX) {
        return -1;
    }
    *pid = (pid_t)value;
    return 0;
}

static int parse_tid(const char *name, int *tid) {
    char *end = NULL;
    unsigned long value;

    if (!name || !*name) {
        return -1;
    }
    errno = 0;
    value = strtoul(name, &end, 10);
    if (errno || end == name || *end || value < 1 || value > INT_MAX) {
        return -1;
    }
    *tid = (int)value;
    return 0;
}

static int parse_stat_cpu(const char *path, uint64_t *cpu_time) {
    char line[4096];
    char *close_name;
    char *cursor;
    unsigned long long utime = 0;
    unsigned long long stime = 0;
    unsigned field;
    FILE *file = fopen(path, "re");

    if (!file) {
        return -1;
    }
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return -1;
    }
    fclose(file);

    /* comm is enclosed in parentheses and can itself contain ')'. */
    close_name = strrchr(line, ')');
    if (!close_name) {
        return -1;
    }
    cursor = close_name + 1;

    /* After comm, field 3 is the one-character state; utime/stime are 14/15. */
    while (*cursor == ' ' || *cursor == '\t') {
        ++cursor;
    }
    if (!*cursor) {
        return -1;
    }
    ++cursor;

    for (field = 4; field <= 15; ++field) {
        char *end;
        unsigned long long value;

        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        if (!*cursor) {
            return -1;
        }

        errno = 0;
        value = strtoull(cursor, &end, 10);
        if (errno == ERANGE || end == cursor) {
            return -1;
        }
        if (field == 14) {
            utime = value;
        } else if (field == 15) {
            stime = value;
        }
        cursor = end;
    }

    if (UINT64_MAX - (uint64_t)utime < (uint64_t)stime) {
        return -1;
    }
    *cpu_time = (uint64_t)utime + (uint64_t)stime;
    return 0;
}

static size_t collect_samples(pid_t pid, struct thread_sample *samples) {
    char task_path[PATH_MAX];
    DIR *directory;
    struct dirent *entry;
    size_t count = 0;

    if (snprintf(task_path, sizeof(task_path), "/proc/%d/task", (int)pid) >=
        (int)sizeof(task_path)) {
        return 0;
    }
    directory = opendir(task_path);
    if (!directory) {
        return 0;
    }

    while ((entry = readdir(directory)) != NULL && count < MAX_THREADS) {
        char stat_path[PATH_MAX];
        uint64_t cpu_time;
        int tid;

        if (parse_tid(entry->d_name, &tid) != 0) {
            continue;
        }
        if (snprintf(stat_path, sizeof(stat_path), "%s/%d/stat", task_path, tid) >=
            (int)sizeof(stat_path)) {
            continue;
        }
        if (parse_stat_cpu(stat_path, &cpu_time) != 0) {
            continue;
        }
        samples[count++] = (struct thread_sample){tid, cpu_time};
    }
    closedir(directory);
    return count;
}

static int compare_tid(const void *left, const void *right) {
    const struct thread_sample *a = left;
    const struct thread_sample *b = right;
    return (a->tid > b->tid) - (a->tid < b->tid);
}

static int compare_delta_desc(const void *left, const void *right) {
    const struct delta_sample *a = left;
    const struct delta_sample *b = right;

    if (a->delta < b->delta) {
        return 1;
    }
    if (a->delta > b->delta) {
        return -1;
    }
    return (a->tid > b->tid) - (a->tid < b->tid);
}

static int find_tid(const struct thread_sample *samples, size_t count, int tid,
                    uint64_t *cpu_time) {
    size_t low = 0;
    size_t high = count;

    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (samples[middle].tid == tid) {
            *cpu_time = samples[middle].cpu_time;
            return 0;
        }
        if (samples[middle].tid < tid) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return -1;
}

static int write_atomic(const char *output, const struct delta_sample *deltas,
                        size_t count) {
    char temporary[PATH_MAX];
    int fd;
    FILE *file;
    size_t limit = count < TOP_THREADS ? count : TOP_THREADS;

    if (snprintf(temporary, sizeof(temporary), "%s.tmp.%d", output,
                 (int)getpid()) >= (int)sizeof(temporary)) {
        return -1;
    }
    fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        return -1;
    }
    file = fdopen(fd, "w");
    if (!file) {
        close(fd);
        unlink(temporary);
        return -1;
    }
    for (size_t i = 0; i < limit; ++i) {
        if (fprintf(file, "%d\n", deltas[i].tid) < 0) {
            fclose(file);
            unlink(temporary);
            return -1;
        }
    }
    if (fflush(file) != 0 || fsync(fileno(file)) != 0 || fclose(file) != 0) {
        unlink(temporary);
        return -1;
    }
    if (rename(temporary, output) != 0) {
        unlink(temporary);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    struct thread_sample first[MAX_THREADS];
    struct thread_sample second[MAX_THREADS];
    struct delta_sample deltas[MAX_THREADS];
    pid_t pid;
    size_t first_count;
    size_t second_count;
    size_t delta_count = 0;

    if (argc != 3 || parse_positive_pid(argv[1], &pid) != 0 ||
        !argv[2][0] || strlen(argv[2]) >= PATH_MAX) {
        fprintf(stderr, "Usage: %s PID OUTPUT_PATH\n", argv[0]);
        return 2;
    }

    first_count = collect_samples(pid, first);
    if (!first_count) {
        fprintf(stderr, "sampler: unable to read /proc/%d/task\n", (int)pid);
        return 3;
    }
    qsort(first, first_count, sizeof(first[0]), compare_tid);

    for (;;) {
        if (usleep(SAMPLE_DELAY_US) == 0) {
            break;
        }
        if (errno != EINTR) {
            return 4;
        }
    }

    second_count = collect_samples(pid, second);
    for (size_t i = 0; i < second_count && delta_count < MAX_THREADS; ++i) {
        uint64_t before;
        if (find_tid(first, first_count, second[i].tid, &before) == 0 &&
            second[i].cpu_time > before) {
            deltas[delta_count++] = (struct delta_sample){
                second[i].tid,
                second[i].cpu_time - before,
            };
        }
    }

    qsort(deltas, delta_count, sizeof(deltas[0]), compare_delta_desc);
    if (write_atomic(argv[2], deltas, delta_count) != 0) {
        fprintf(stderr, "sampler: failed to atomically write %s: %s\n",
                argv[2], strerror(errno));
        return 5;
    }
    return 0;
}
