#include <stdio.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "../info.h"

void child(int *decoder, int id) {
    if((shmid = shmget(shm_key, sizeof(message_t) * pros_num,
                        0666 | IPC_CREAT | IPC_EXCL)) < 0)  {
        if((shmid = shmget(shm_key, sizeof(message_t) * pros_num, 0)) < 0) {
            printf("Can\'t connect to shared memory\n");
            exit(-1);
        };
        msg_p = shmat(shmid, NULL, 0);
        printf("Connect to Shared Memory from child %d\n", id);
    } else {
        msg_p = shmat(shmid, NULL, 0);
        printf("New Shared Memory from child %d\n", id);
    }

    struct sembuf parent_poster = { id + pros_num, 1, 0 };
    struct sembuf child_waiter = { id, -1, 0 };

    char buffer[MAX_INTS];

    while (1) {
        semop(semid, &child_waiter, 1);

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
        semop(semid, &parent_poster, 1);
    }
    semop(semid, &parent_poster, 1);
}

void childHandleCtrlC(int nsig){
    printf("Receive signal %d, CTRL-C pressed\n", nsig);

    for (int i = 0; semid != -1 && i < 2 * pros_num + 1; ++i) {
        semctl(semid, i, IPC_RMID);
        printf("удален %d семафор\n", i);
    }
    printf("Удалены все семафоры или они не были созданы\n");
    if (msg_p != NULL) {
        shmdt(msg_p);
    }
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, NULL);
    }
    printf("Закрыта разделяемая память\n");
    exit(0);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Неверный запуск\n");
        exit(1);
    }
    int decoder[26];
    getDecoder(decoder, argv[1]);
    pros_num = atoi(argv[2]);
    prev = signal(SIGINT, childHandleCtrlC);

    shm_key = ftok(pathname_for_two, 0);
    if ((semid = semget(shm_key, 2 * pros_num + 1, 0666 | IPC_CREAT | IPC_EXCL)) < 0){
        if ((semid = semget(shm_key, 2 * pros_num + 1, 0)) < 0) {
            printf("Can\'t connect to semaphor\n");
            exit(-1);
        }
        printf("Connect to Semaphor\n");
    } else {
        for (int i = 0; i < 2 * pros_num + 1; ++i) {
            semctl(semid, i, SETVAL, 0);
        }
        printf("All new semaphores initialized\n");
    }

    for (int i = 0; i < pros_num; ++i) {
        if (fork() == 0) {
            signal(SIGINT, prev);
            child(decoder, i);
            exit(0);
        }
    }
    struct sembuf wait_last = { 2 * pros_num, -1, 0 };
    semop(semid, &wait_last, 1);
    for (int i = 0; semid != -1 && i < 2 * pros_num + 1; ++i) {
        semctl(semid, i, IPC_RMID);
        printf("удален %d семафор\n", i);
    }
    return 0;
}
