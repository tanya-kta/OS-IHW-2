#include <stdio.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "../info.h"

void parentHandleCtrlC(int nsig){
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
    if (argc != 4) {
        printf("Неверный запуск\n");
        exit(1);
    }
    prev = signal(SIGINT, parentHandleCtrlC);
    pros_num = atoi(argv[3]);

    shm_key = ftok(pathname_for_two, 0);
    if((shmid = shmget(shm_key, sizeof(message_t) * pros_num,
                        0666 | IPC_CREAT | IPC_EXCL)) < 0)  {
        if((shmid = shmget(shm_key, sizeof(message_t) * pros_num, 0)) < 0) {
            printf("Can\'t connect to shared memory\n");
            exit(-1);
        }
        msg_p = shmat(shmid, NULL, 0);
        printf("Connect to Shared Memory from parent\n");
    } else {
        msg_p = shmat(shmid, NULL, 0);
        printf("New Shared Memory from parent\n");
    }

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

    int in_file = open(argv[1], O_RDONLY, S_IRWXU);
    int out_file = open(argv[2], O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU);

    struct sembuf child_poster = { 0, 1, 0 };
    struct sembuf parent_waiter = { 0, -1, 0 };

    int status = 1;
    while (status == 1) {
        int num_of_running = 0;
        for (int i = 0; i < pros_num; ++i, ++num_of_running) {
            int size = 0;
            for (; size < MAX_INTS; ++size) {
                status = readInt(in_file, &msg_p[i].coded[size]);
                if (status == -1) {
                    break;
                }
            }
            if (size == 0) {
                break;
            }
            msg_p[i].size = size;
            msg_p[i].type = MSG_TYPE_INT;

            child_poster.sem_num = i;
            semop(semid, &child_poster, 1);
        }

        for (int i = 0; i < num_of_running; ++i) {
            parent_waiter.sem_num = i + pros_num;
            semop(semid, &parent_waiter, 1);

            printf("parent from child id %d: ", i);
            for (int j = 0; j < msg_p[i].size; ++j) {
                printf("%c", msg_p[i].uncoded[j]);
                write(out_file, &msg_p[i].uncoded[j], 1);
            }
            printf("\n");
        }
    }
    close(in_file);
    close(out_file);

    for (int i = 0; i < pros_num; ++i) {
        msg_p[i].type = MSG_TYPE_FINISH;
        child_poster.sem_num = i;
        semop(semid, &child_poster, 1);
    }
    for (int i = 0; i < pros_num; ++i) {
        parent_waiter.sem_num = i + pros_num;
        semop(semid, &parent_waiter, 1);
    }

    shmdt(msg_p);
    shmctl(shmid, IPC_RMID, NULL);
    printf("Удалена разделяемая память\n");

    struct sembuf post_last = { 2 * pros_num, 1, 0 };
    semop(semid, &post_last, 1);
    return 0;
}
