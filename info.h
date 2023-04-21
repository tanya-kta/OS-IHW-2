// message.h
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>

// коды сообщений
#define MSG_TYPE_INT    1     // сообщение о передаче кодированных чисел
#define MSG_TYPE_STRING 2     // сообщение о передаче декодированной строки
#define MSG_TYPE_FINISH 3     // сообщение о том, что пора завершать обмен
#define MAX_INTS        30    // максимальная длина текстового сообщения

const char *child_sem = "/child-semaphore";
const char *parent_sem = "/parent-semaphore";

// структура сообщения, помещаемого в разделяемую память
typedef struct {
    int type;
    int size;
    sem_t child_sem;
    sem_t parent_sem;
    union {
        char uncoded[MAX_INTS * sizeof(int)];
        int coded[MAX_INTS];
    };
} message_t;

void sysErr(char *msg) {
    puts(msg);
    exit(1);
}

void getDecoder(int *decoder, char *filename) {
    int file = open(filename, O_RDONLY, S_IRWXU);
    char letter;
    char line[13];
    int code;
    for (int i = 0; i < 26; ++i) {
        for (int ind = 0; ind < 12; ++ind) {
            int num = read(file, &line[ind], sizeof(char));
            if (num == 0 || line[ind] == '\n' || line[ind] == '\0') {
                line[ind] = '\0';
                break;
            }
        }
        sscanf(line, "%c %d", &letter, &code);
        decoder[letter - 'a'] = code;
    }
}

char getCodedLetter(int *decoder, int code) {
    for (int i = 0; i < 26; ++i) {
        if (decoder[i] == code) {
            return 'a' + i;
        }
    }
    return 0;
}

int readInt(int file, int *p) {
    char line[10];
    for (int ind = 0; ind < 10; ++ind) {
        int num = read(file, &line[ind], sizeof(char));
        if (num == 0 || line[ind] == '\n' || line[ind] == ' ') {
            line[ind] = '\0';
            break;
        }
    }
    if (strlen(line) == 0) {
        return -1;
    }
    sscanf(line, "%d", p);
    return 1;
}

char *getChildSemaphoreName(int index) {
    char digit = '0' + index;
    char total[strlen(child_sem) + 2];
    total[0] = '\0';
    strcat(total, child_sem);
    return strcat(total, &digit);
}

char *getParentSemaphoreName(int index) {
    char digit = '0' + index;
    char total[strlen(parent_sem) + 2];
    total[0] = '\0';
    strcat(total, parent_sem);
    return strcat(total, &digit);
}
