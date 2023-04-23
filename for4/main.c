#include <stdio.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "../info.h"

void child(int *decoder, sem_t *ch_sem, sem_t *pr_sem, int id) {
    if ((shmid = shm_open(mem_name, O_CREAT | O_RDWR, S_IRWXU)) == -1) {
        perror("shm_open");
        sysErr("client: object is already open");
    } else {
        printf("Object in child id %d is open: name = %s, id = 0x%x\n", id, mem_name, shmid);
    }
    // получить доступ к памяти
    msg_p = mmap(0, sizeof(message_t) * pros_num, PROT_WRITE | PROT_READ, MAP_SHARED, shmid, 0);

    char buffer[MAX_INTS];

    while (1) {
        sem_wait(ch_sem);
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
        sem_post(pr_sem);
    }
    close(shmid);
    sem_post(pr_sem);
}

void parent(char *input_file, char *output_file, sem_t **pr_sems, sem_t **ch_sems) {
    char decoded[100010];
    int ind_dec = 0;

    if ((shmid = shm_open(mem_name, O_CREAT | O_RDWR, S_IRWXU)) == -1) {
        perror("shm_open");
        sysErr("server: object is already open");
    } else {
        printf("Object in parent is open: name = %s, id = 0x%x\n", mem_name, shmid);
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
            sem_post(ch_sems[i]);
        }

        for (int i = 0; i < num_of_running; ++i) {
            sem_wait(pr_sems[i]);
            printf("parent from child id %d: ", i);
            for (int j = 0; j < msg_p[i].size; ++j, ++ind_dec) {
                decoded[ind_dec] = msg_p[i].uncoded[j];
                printf("%c", decoded[ind_dec]);
            }
            printf("\n");
        }
    }
    close(file);

    for (int i = 0; i < pros_num; ++i) {
        msg_p[i].type = MSG_TYPE_FINISH;
        sem_post(ch_sems[i]);
    }
    for (int i = 0; i < pros_num; ++i) {
        sem_wait(pr_sems[i]);
        sem_close(pr_sems[i]);
        sem_close(ch_sems[i]);
    }

    decoded[ind_dec] = '\0';
    printf("%s\n", decoded);

    file = open(output_file, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU);
    write(file, decoded, sizeof(char) * ind_dec);
    close(file);

    close(shmid);
    if (shm_unlink(mem_name) == -1) {
        perror("shm_unlink");
        sysErr("server: error getting pointer to shared memory");
    }
}

void parentHandleCtrlC(int nsig){
    printf("Receive signal %d, CTRL-C pressed\n", nsig);
    for (int i = 0; child_semaphores_pointer != NULL && i < pros_num; ++i) {
        sem_destroy(child_semaphores_pointer[i]);
        if (sem_unlink(getChildSemaphoreName(i)) == -1) {
            perror("sem_unlink");
            sysErr("server: error getting pointer to semaphore");
        }
    }
    printf("Закрыты семафоры детей\n");
    for (int i = 0; parent_semaphores_pointer != NULL && i < pros_num; ++i) {
        sem_destroy(parent_semaphores_pointer[i]);
        if (sem_unlink(getParentSemaphoreName(i)) == -1) {
            perror("sem_unlink");
            sysErr("server: error getting pointer to semaphore");
        }
    }
    printf("Закрыты семафоры родителя\n");
    if ((shmid = shm_open(mem_name, O_CREAT | O_RDWR, S_IRWXU)) == -1) {
        if (shm_unlink(mem_name) == -1) {
            perror("shm_unlink");
            sysErr("server: error getting pointer to shared memory");
        }
    }
    printf("Закрыта разделяемая память, переход к original handler\n");
    prev(nsig);
}

int main(int argc, char **argv) {
    if (argc != 5) {
        printf("Неверный запуск\n");
        exit(1);
    }
    prev = signal(SIGINT, parentHandleCtrlC);
    int decoder[26];
    getDecoder(decoder, argv[1]);
    pros_num = atoi(argv[4]);

    sem_t *parent_semaphores[pros_num];
    for (int i = 0; i < pros_num; ++i) {
        parent_semaphores[i] = sem_open(getParentSemaphoreName(i), O_CREAT, 0666, 0);
    }
    parent_semaphores_pointer = parent_semaphores;
    sem_t *child_semaphores[pros_num];
    for (int i = 0; i < pros_num; ++i) {
        child_semaphores[i] = sem_open(getChildSemaphoreName(i), O_CREAT, 0666, 0);
    }
    child_semaphores_pointer = child_semaphores;

    for (int i = 0; i < pros_num; ++i) {
        if (fork() == 0) {
            signal(SIGINT, prev);
            child(decoder, child_semaphores[i], parent_semaphores[i], i);
            exit(0);
        }
    }

    parent(argv[2], argv[3], parent_semaphores, child_semaphores);
    for (int i = 0; i < pros_num; ++i) {
        if (sem_unlink(getChildSemaphoreName(i)) == -1) {
            perror("sem_unlink");
            sysErr("server: error getting pointer to semaphore");
        }
        if (sem_unlink(getParentSemaphoreName(i)) == -1) {
            perror("sem_unlink");
            sysErr("server: error getting pointer to semaphore");
        }
    }
    return 0;
}
