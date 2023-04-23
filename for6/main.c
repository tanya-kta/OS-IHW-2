#include <stdio.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "../info.h"

void child(int *decoder, int id) {
    shm_key = ftok(pathname, 0);
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

void parent(char *input_file, char *output_file) {
    char decoded[100010];
    int ind_dec = 0;

    int file = open(input_file, O_RDONLY, S_IRWXU);

    struct sembuf child_poster = { 0, 1, 0 };
    struct sembuf parent_waiter = { 0, -1, 0 };

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

            child_poster.sem_num = i;
            semop(semid, &child_poster, 1);
        }

        for (int i = 0; i < num_of_running; ++i) {
            parent_waiter.sem_num = i + pros_num;
            semop(semid, &parent_waiter, 1);

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
        child_poster.sem_num = i;
        semop(semid, &child_poster, 1);
    }
    for (int i = 0; i < pros_num; ++i) {
        parent_waiter.sem_num = i + pros_num;
        semop(semid, &parent_waiter, 1);
    }

    decoded[ind_dec] = '\0';
    printf("%s\n", decoded);

    close(file);
    file = open(output_file, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU);
    write(file, decoded, sizeof(char) * ind_dec);
    close(file);
}

void parentHandleCtrlC(int nsig){
    printf("Receive signal %d, CTRL-C pressed\n", nsig);

    for (int i = 0; semid != -1 && i < 2 * pros_num; ++i) {
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
    printf("Закрыта разделяемая память, переход к original handler\n");
    prev(nsig);
}

int main(int argc, char **argv) {
    prev = signal(SIGINT, parentHandleCtrlC);
    int decoder[26];
    getDecoder(decoder, argv[1]);
    printf("Введите желаемое число процессов-декодеров, не более 10: \n");
    scanf("%d", &pros_num);

    shm_key = ftok(pathname, 0);
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

    if ((semid = semget(shm_key, 2 * pros_num, 0666 | IPC_CREAT | IPC_EXCL)) < 0){
        if ((semid = semget(shm_key, 2 * pros_num, 0)) < 0) {
            printf("Can\'t connect to semaphor\n");
            exit(-1);
        }
        printf("Connect to Semaphor\n");
    } else {
        for (int i = 0; i < 2 * pros_num; ++i) {
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
    parent(argv[2], argv[3]);

    for (int i = 0; i < 2 * pros_num; ++i) {
        semctl(semid, i, IPC_RMID);
    }
    printf("Удалены все семафоры\n");
    shmdt(msg_p);
    shmctl(shmid, IPC_RMID, NULL);
    printf("Удалена разделяемая память\n");
    return 0;
}
