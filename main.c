#include <stdio.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "info.h"

void child(char *mem_name, int *decoder) {
    int shmid;

    sem_t *ch_sem = sem_open(child_sem, O_CREAT, 0666, 0);
    sem_t *pr_sem = sem_open(parent_sem, O_CREAT, 0666, 0);

    message_t *msg_p;  // адрес сообщения в разделяемой памяти
    if ((shmid = shm_open(mem_name, O_CREAT | O_RDWR, S_IRWXU)) == -1) {
        perror("shm_open");
        sysErr("client: object is already open");
    } else {
        printf("Object is open: name = %s, id = 0x%x\n", mem_name, shmid);
    }
    // получить доступ к памяти
    msg_p = mmap(0, sizeof(message_t), PROT_WRITE | PROT_READ, MAP_SHARED, shmid, 0);

    char buffer[30];

    while (1) {
        sem_wait(ch_sem);
        if (msg_p->type == MSG_TYPE_FINISH) {
            break;
        }
        printf("child: ");
        for (int i = 0; i < msg_p->size; ++i) {
            buffer[i] = getCodedLetter(decoder, msg_p->coded[i]);
            printf("%d ", msg_p->coded[i]);
        }
        printf("\n");
        for (int i = 0; i < msg_p->size; ++i) {
            msg_p->uncoded[i] = buffer[i];
        }

        msg_p->type = MSG_TYPE_STRING;
        sem_post(pr_sem);
    }
    close(shmid);
    sem_post(pr_sem);
}

void parent(char *mem_name, char *input_file) {
    char decoded[10010];
    int ind_dec = 0;

    int file = open(input_file, O_RDONLY, S_IRWXU);

    sem_t *ch_sem = sem_open(child_sem, O_CREAT, 0666, 0);
    sem_t *pr_sem = sem_open(parent_sem, O_CREAT, 0666, 0);

    int shmid;
    message_t *msg_p;  // адрес сообщения в разделяемой памяти
    if ((shmid = shm_open(mem_name, O_CREAT | O_RDWR, S_IRWXU)) == -1) {
        perror("shm_open");
        sysErr("server: object is already open");
    } else {
        printf("Object is open: name = %s, id = 0x%x\n", mem_name, shmid);
    }
    // Задание размера объекта памяти
    if (ftruncate(shmid, sizeof(message_t)) == -1) {
        perror("ftruncate");
        sysErr("server: memory sizing error");
    } else {
        printf("Memory size set and = %lu\n", sizeof(message_t));
    }
    // получить доступ к памяти
    msg_p = mmap(0, sizeof(message_t), PROT_WRITE | PROT_READ, MAP_SHARED, shmid, 0);
    msg_p->type = MSG_TYPE_EMPTY;

    while (1) {
        int size = 0;
        for (; size < 30; ++size) {
            int status = readInt(file, &msg_p->coded[size]);
            if (status == -1) {
                break;
            }
        }
        if (size == 0) {
            msg_p->type = MSG_TYPE_FINISH;
            sem_post(ch_sem);
            break;
        }
        msg_p->size = size;
        msg_p->type = MSG_TYPE_INT;
        sem_post(ch_sem);
        sem_wait(pr_sem);
        printf("parent: ");

        for (int i = 0; i < msg_p->size; ++i, ++ind_dec) {
            decoded[ind_dec] = msg_p->uncoded[i];
            printf("%c", decoded[ind_dec]);
        }
        printf("\n");
    }
    sem_wait(pr_sem);

    decoded[ind_dec] = '\0';
    printf("%s", decoded);

    close(shmid);
    sem_close(pr_sem);
    sem_close(ch_sem);
    if (sem_unlink(child_sem) == -1) {
        perror("sem_unlink");
        sysErr("server: error getting pointer to semaphore");
    }
    if (sem_unlink(parent_sem) == -1) {
        perror("sem_unlink");
        sysErr("server: error getting pointer to semaphore");
    }
    if (shm_unlink(mem_name) == -1) {
        perror("shm_unlink");
        sysErr("server: error getting pointer to shared memory");
    }

}

int main(int argc, char **argv) {
    char *mem_name = "shared-memory";
    int decoder[26];
    getDecoder(decoder, argv[1]);
    pid_t pid = fork();
    if (pid < 0) {
        sysErr("Fork error");
    } else if (pid == 0) {
        child(mem_name, decoder);
    } else {
        parent(mem_name, argv[2]);
        wait(0);
    }

    return 0;
}
