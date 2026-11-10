#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <semaphore.h>

#include "shm_common.h"


static void die(const char *msg) {
    int saved = errno;
    write(STDERR_FILENO, msg, strlen(msg));
    write(STDERR_FILENO, ": ", 2);
    const char *err = strerror(saved);
    write(STDERR_FILENO, err, strlen(err));
    write(STDERR_FILENO, "\n", 1);
    exit(EXIT_FAILURE);
}


static int long_to_string(char *buf, int buf_size, long int num) {
    if (buf_size < 2) return 0;

    char temp[64];
    int pos = 0;
    int is_negative = 0;

    if (num < 0) {
        is_negative = 1;
        num = -num;
    }

    if (num == 0) {
        temp[pos++] = '0';
    } else {
        while (num > 0) {
            temp[pos++] = '0' + (num % 10);
            num /= 10;
        }
    }

    int buf_pos = 0;

    if (is_negative && buf_pos < buf_size - 1) {
        buf[buf_pos++] = '-';
    }

    for (int i = pos - 1; i >= 0 && buf_pos < buf_size - 1; i--) {
        buf[buf_pos++] = temp[i];
    }

    if (buf_pos < buf_size - 1) {
        buf[buf_pos++] = '\n';
    }

    buf[buf_pos] = '\0';

    return buf_pos;
}


static void send_data(shm_t *shm, sem_t *sem, const char *buf, uint32_t len) {
    for (;;) {
        if (sem_wait(sem) == -1) {
            die("sem_wait");
        }
        if (shm->length == 0) {
            break;
        }
        if (sem_post(sem) == -1) {
            die("sem_post");
        }
    }

    shm->length = len;
    memcpy(shm->data, buf, len);

    if (sem_post(sem) == -1) {
        die("sem_post");
    }
}


static void send_done(shm_t *shm, sem_t *sem) {
    for (;;) {
        if (sem_wait(sem) == -1) {
            die("sem_wait");
        }
        if (shm->length == 0) {
            break;
        }
        if (sem_post(sem) == -1) {
            die("sem_post");
        }
    }

    shm->length = UINT32_MAX;

    if (sem_post(sem) == -1) {
        die("sem_post");
    }
}


int main(int argc, char **argv) {
    if (argc < 4) {
        const char msg[] = "usage: child_proccess <file> <shm_name> <sem_name>\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        exit(EXIT_FAILURE);
    }

    const char *filename = argv[1];
    const char *shm_name = argv[2];
    const char *sem_name = argv[3];

    int shm = shm_open(shm_name, O_RDWR, 0);
    if (shm == -1) {
        die("shm_open");
    }

    shm_t *shm_ptr = mmap(NULL, sizeof(shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
    if (shm_ptr == MAP_FAILED) {
        die("mmap");
    }

    sem_t *sem = sem_open(sem_name, 0);
    if (sem == SEM_FAILED) {
        die("sem_open");
    }

    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        die("open");
    }

    size_t cap = 4096;
    size_t len = 0;
    char *content = malloc(cap);
    if (content == NULL) {
        die("malloc");
    }

    ssize_t n;
    while ((n = read(fd, content + len, cap - len)) > 0) {
        len += (size_t)n;
        if (len == cap) {
            cap *= 2;
            char *tmp = realloc(content, cap);
            if (tmp == NULL) {
                die("realloc");
            }
            content = tmp;
        }
    }
    if (n == -1) {
        die("read");
    }
    content[len] = '\0';
    close(fd);

    long int total_res = 0;

    char *lines = strtok(content, "\n");
    while (lines) {
        long int line_res = 0;

        char *current = lines;
        char *endptr;
        while (*current != '\0') {
            long num = strtol(current, &endptr, 10);
            if (endptr == current) {
                current++;
            } else {
                line_res += num;
                current = endptr;
            }
        }
        total_res += line_res;

        char outbuf[64];
        int out_len = long_to_string(outbuf, sizeof(outbuf), line_res);
        send_data(shm_ptr, sem, outbuf, out_len);

        lines = strtok(NULL, "\n");
    }

    char total_outbuf[64];
    int total_len = long_to_string(total_outbuf, sizeof(total_outbuf), total_res);
    send_data(shm_ptr, sem, total_outbuf, total_len);

    send_done(shm_ptr, sem);

    free(content);

    sem_close(sem);
    munmap(shm_ptr, sizeof(shm_t));
    close(shm);

    return 0;
}
