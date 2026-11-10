#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>

#include "shm_common.h"


static char CHILD_PROGRAMM_NAME[] = "child_proccess";


static void die(const char *msg) {
    int saved = errno;
    write(STDERR_FILENO, msg, strlen(msg));
    write(STDERR_FILENO, ": ", 2);
    const char *err = strerror(saved);
    write(STDERR_FILENO, err, strlen(err));
    write(STDERR_FILENO, "\n", 1);
    exit(EXIT_FAILURE);
}


static int uint_to_string(char *buf, int buf_size, unsigned int num) {
    if (buf_size < 1) return 0;

    char temp[32];
    int pos = 0;

    if (num == 0) {
        temp[pos++] = '0';
    } else {
        while (num > 0) {
            temp[pos++] = '0' + (num % 10);
            num /= 10;
        }
    }

    int buf_pos = 0;

    for (int i = pos - 1; i >= 0 && buf_pos < buf_size - 1; i--) {
        buf[buf_pos++] = temp[i];
    }

    buf[buf_pos] = '\0';

    return buf_pos;
}


static void build_name(char *buf, int buf_size, const char *prefix, unsigned int num) {
    int i = 0;

    while (prefix[i] != '\0' && i < buf_size - 1) {
        buf[i] = prefix[i];
        i++;
    }

    uint_to_string(buf + i, buf_size - i, num);
}


int main(int argc, char **argv) {
    if (argc < 2) {
        const char msg[] = "usage: parent_proccess <file>\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        exit(EXIT_FAILURE);
    }

    const char *filename = argv[1];

    if (access(filename, R_OK) == -1) {
        die("access");
    }

    char shm_name[64];
    char sem_name[64];

    const pid_t pid = getpid();
    build_name(shm_name, sizeof(shm_name), "/lab3_shm_", (unsigned int)pid);
    build_name(sem_name, sizeof(sem_name), "/lab3_sem_", (unsigned int)pid);

    shm_unlink(shm_name);
    sem_unlink(sem_name);

    int shm = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (shm == -1) {
        die("shm_open");
    }

    if (ftruncate(shm, sizeof(shm_t)) == -1) {
        die("ftruncate");
    }

    shm_t *shm_ptr = mmap(NULL, sizeof(shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
    if (shm_ptr == MAP_FAILED) {
        die("mmap");
    }

    shm_ptr->length = 0;

    sem_t *sem = sem_open(sem_name, O_CREAT | O_EXCL, 0600, 1);
    if (sem == SEM_FAILED) {
        die("sem_open");
    }

    const pid_t child = fork();

    switch (child) {
        case -1: {
            die("fork");
        } break;

        case 0: {
            char *const args[] = {
                CHILD_PROGRAMM_NAME,
                (char *)filename,
                shm_name,
                sem_name,
                NULL
            };

            execv("./child_proccess", args);
            die("execv");
        } break;

        default: {
            for (;;) {
                if (sem_wait(sem) == -1) {
                    die("sem_wait");
                }

                uint32_t len = shm_ptr->length;

                if (len == UINT32_MAX) {
                    if (sem_post(sem) == -1) {
                        die("sem_post");
                    }
                    break;
                }

                if (len > 0) {
                    if (write(STDOUT_FILENO, shm_ptr->data, len) == -1) {
                        die("write");
                    }
                    shm_ptr->length = 0;
                }

                if (sem_post(sem) == -1) {
                    die("sem_post");
                }
            }

            waitpid(child, NULL, 0);
        } break;
    }

    sem_close(sem);
    sem_unlink(sem_name);
    munmap(shm_ptr, sizeof(shm_t));
    close(shm);
    shm_unlink(shm_name);

    return 0;
}
