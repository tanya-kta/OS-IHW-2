#include <stdio.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "../info.h"

void child(int *decoder, int id) {
    if ((shmid = shm_open(mem_name, O_CREAT | O_RDWR, S_IRWXU)) == -1) {
        perror("shm_open");
        sysErr("client: object is already open");
    } else {
        printf("Object in child id %d is open: name = %s, id = 0x%x\n", id, mem_name, shmid);
    }
    if (ftruncate(shmid, sizeof(message_t) * pros_num) == -1) {
        perror("ftruncate");
        sysErr("server: memory sizing error");
    } else {
        printf("Memory size set and = %lu\n", sizeof(message_t) * pros_num);
    }
    // получить доступ к памяти
    msg_p = mmap(0, sizeof(message_t) * pros_num, PROT_WRITE | PROT_READ, MAP_SHARED, shmid, 0);

    char *name = getChildSemaphoreName(id);
    sem_t *ch_sem = sem_open(name, O_CREAT, 0666, 0);
    free(name);
    name = getParentSemaphoreName(id);
    sem_t *pr_sem = sem_open(name, O_CREAT, 0666, 0);
    free(name);

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

void childHandleCtrlC(int nsig){
    printf("Receive signal %d, CTRL-C pressed\n", nsig);
    for (int i = 0; i < pros_num; ++i) {
        char *name = getChildSemaphoreName(i);
        sem_open(name, O_CREAT, 0666, 0);
        if (sem_unlink(name) == -1) {
            perror("sem_unlink");
            sysErr("server: error getting pointer to semaphore");
        }
        free(name);
    }
    printf("Закрыты семафоры детей\n");
    for (int i = 0; i < pros_num; ++i) {
        char *name = getParentSemaphoreName(i);
        sem_open(name, O_CREAT, 0666, 0);
        if (sem_unlink(name) == -1) {
            perror("sem_unlink");
            sysErr("server: error getting pointer to semaphore");
        }
        free(name);
    }
    printf("Закрыты семафоры родителя\n");
    if (shmid != -1) {
        if (shm_unlink(mem_name) == -1) {
            perror("shm_unlink");
            sysErr("server: error getting pointer to shared memory");
        }
    }
    printf("Закрыта разделяемая память\n");
    exit(0);
}

int main(int argc, char **argv) {
    int decoder[26];
    getDecoder(decoder, argv[1]);
    pros_num = atoi(argv[2]);
    prev = signal(SIGINT, childHandleCtrlC);

    for (int i = 0; i < pros_num; ++i) {
        if (fork() == 0) {
            signal(SIGINT, prev);
            child(decoder, i);
            exit(0);
        }
    }
    sem_t *last_sem = sem_open(last_semaphore, O_CREAT, 0666, 0);
    sem_wait(last_sem);
    sem_unlink(last_semaphore);
    return 0;
}
