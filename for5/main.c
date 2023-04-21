#include <stdio.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "../info.h"

void child(char *mem_name, int *decoder, int id, int pros_num) {
    int shmid;

    message_t *msg_p;  // адрес сообщения в разделяемой памяти
    if ((shmid = shm_open(mem_name, O_CREAT | O_RDWR, S_IRWXU)) == -1) {
        perror("shm_open");
        sysErr("client: object is already open");
    } else {
        printf("Object in child id %d is open: name = %s, id = 0x%x\n", id, mem_name, shmid);
    }
    // получить доступ к памяти
    msg_p = mmap(0, sizeof(message_t) * pros_num, PROT_WRITE | PROT_READ, MAP_SHARED, shmid, 0);

    char buffer[30];

    while (1) {
        sem_wait(&msg_p[id].child_sem);
        if (msg_p[id].type == MSG_TYPE_FINISH) {
            break;
        }
        printf("child id %d: ", id);
        for (int i = 0; i < msg_p[id].size; ++i) {
            buffer[i] = getCodedLetter(decoder, msg_p[id].coded[i]);
            printf("%d ", msg_p[id].coded[i]);
        }
        printf("\n");
        for (int i = 0; i < msg_p[id].size; ++i) {
            msg_p[id].uncoded[i] = buffer[i];
        }

        msg_p[id].type = MSG_TYPE_STRING;
        sem_post(&msg_p[id].parent_sem);
    }
    close(shmid);
    sem_post(&msg_p[id].parent_sem);
}

void parent(message_t *msg_p, char *input_file, char *output_file, int pros_num) {
    char decoded[100010];
    int ind_dec = 0;

    int file = open(input_file, O_RDONLY, S_IRWXU);

    int status = 1;
    while (status == 1) {
        int num_of_running = 0;
        for (int i = 0; i < pros_num; ++i, ++num_of_running) {
            int size = 0;
            for (; size < MAX_INTS; ++size) {
                status = readInt(file, &msg_p[i].coded[size]);
                if (status == -1) {
                    break;
                }
            }
            if (size == 0) {
                break;
            }
            msg_p[i].size = size;
            msg_p[i].type = MSG_TYPE_INT;
            sem_post(&msg_p[i].child_sem);
        }

        for (int i = 0; i < num_of_running; ++i) {
            sem_wait(&msg_p[i].parent_sem);
            printf("parent from child id %d: ", i);
            for (int j = 0; j < msg_p[i].size; ++j, ++ind_dec) {
                decoded[ind_dec] = msg_p[i].uncoded[j];
                printf("%c", decoded[ind_dec]);
            }
            printf("\n");
        }
    }

    for (int i = 0; i < pros_num; ++i) {
        msg_p[i].type = MSG_TYPE_FINISH;
        sem_post(&msg_p[i].child_sem);
    }
    for (int i = 0; i < pros_num; ++i) {
        sem_wait(&msg_p[i].parent_sem);
    }

    decoded[ind_dec] = '\0';
    printf("%s\n", decoded);

    close(file);
    file = open(output_file, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU);
    write(file, decoded, sizeof(char) * ind_dec);
    close(file);
}

int main(int argc, char **argv) {
    char *mem_name = "shared-memory";
    int decoder[26];
    getDecoder(decoder, argv[1]);
    printf("Введите желаемое число процессов-декодеров, не более 10: \n");
    int pros_num;
    scanf("%d", &pros_num);

    int shmid;
    message_t *msg_p;  // адрес сообщения в разделяемой памяти
    if ((shmid = shm_open(mem_name, O_CREAT | O_RDWR, S_IRWXU)) == -1) {
        perror("shm_open");
        sysErr("server: object is already open");
    } else {
        printf("Object is open: name = %s, id = 0x%x\n", mem_name, shmid);
    }
    // Задание размера объекта памяти
    if (ftruncate(shmid, sizeof(message_t) * pros_num) == -1) {
        perror("ftruncate");
        sysErr("server: memory sizing error");
    } else {
        printf("Memory size set and = %lu\n", sizeof(message_t) * pros_num);
    }
    // получить доступ к памяти
    msg_p = mmap(0, sizeof(message_t) * pros_num, PROT_WRITE | PROT_READ, MAP_SHARED, shmid, 0);

    for (int i = 0; i < pros_num; ++i) {
        if (sem_init(&msg_p[i].child_sem, 1, 0) == -1) {
            sysErr("Creating child semaphore went wrong");
        }
        if (sem_init(&msg_p[i].parent_sem, 1, 0) == -1) {
            sysErr("Creating child semaphore went wrong");
        }
    }

    for (int i = 0; i < pros_num; ++i) {
        if (fork() == 0) {
            child(mem_name, decoder, i, pros_num);
            exit(0);
        }
    }
    parent(msg_p, argv[2], argv[3], pros_num);

    for (int i = 0; i < pros_num; ++i) {
        sem_destroy(&msg_p[i].child_sem);
        sem_destroy(&msg_p[i].parent_sem);
    }
    close(shmid);
    if (shm_unlink(mem_name) == -1) {
        perror("shm_unlink");
        sysErr("server: error getting pointer to shared memory");
    }
    return 0;
}
