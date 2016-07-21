#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <cstring>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>

const char* HOST        = NULL;
const char* DIRECTORY   = NULL;
const char* PORT        = NULL;

static const int SERVER_MAX_CLIENTS_QUEUE = 1000;
static const int READ_BUFFER_SIZE = 1024;
static const int THREAD_COUNT = 4;

const char* RESPONSE_HTML = "<b>Hello world!</b>";
const char* RESPONSE_404 = "HTTP/1.0 404 NOT FOUND\r\n"
                           "Content-length: 0\r\n"
                           "Content-Type: text/html\r\n\r\n";
const char* RESPONSE_200 = "HTTP/1.0 200 OK\r\n"
                            "Content-length: %d\r\n"
                            "Connection: close\r\n"
                            "Content-Type: text/html\r\n"
                            "\r\n"
                            "%s";

bool NEED_DEAMON = true;

enum THREAD_STATUS {
    IDLE = 0,
    PROCESSING,
    FINISHED
};

pthread_t THREADS[THREAD_COUNT];
THREAD_STATUS STATUSES[THREAD_COUNT];
int CLIENTS[THREAD_COUNT];

void parse_input_params(int argc, char** argv) {
    int c;
    while ((c = getopt (argc, argv, "h:p:d:D")) != -1) {
        switch (c) {
            case 'h':
                HOST = optarg;
                break;
            case 'p':
                PORT = optarg;
                break;
            case 'd':
                DIRECTORY = optarg;
                break;
            case 'D':
                NEED_DEAMON = false;
                break;
            default:
                printf("Usage: %s -h <host> -p <port> -d <folder>\n", argv[0]);
                abort();
        }
    }
    printf("host = %s, port = %s, dir = %s\n", HOST, PORT, DIRECTORY);
}

void demonization_if_needed() {
    if (NEED_DEAMON) {
        printf("Deamonization is needed\n");
        pid_t pid = fork();
        if (pid != 0 ) {
            printf("Parent process should be closed\n");
            exit(0);
        }
        printf("Ok, we a daemon now: %d\n", getpid());
    }
}

int send_all(int socket, char *buf, int len, int flags = 0) {
    int total = 0;
    int sent_bytes;

    while (total < len) {
        sent_bytes = send(socket, buf + total, len - total, flags);
        if (sent_bytes == -1) {
            perror("send error");
            break;
        }
        total += sent_bytes;
    }

    return sent_bytes == -1 ? -1 : total;
}

int create_and_bind() {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(PORT));
    addr.sin_addr.s_addr = inet_addr(HOST);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(2);
    }

    return listener;
}

void process_client(int client_socket) {
    char buffer[READ_BUFFER_SIZE];
    int nbytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (nbytes == -1) {
        perror("read client socket");
        return;
    }

    buffer[nbytes] = '\0';
    printf("request [%s]\n", buffer);

    char* find_index = strstr(buffer, "GET /index.html");
    if (find_index == NULL) {
        send_all(client_socket, (char*) RESPONSE_404, strlen(RESPONSE_404));
    } else {
        nbytes = sprintf(buffer, RESPONSE_200, strlen(RESPONSE_HTML), RESPONSE_HTML);
        send_all(client_socket, buffer, nbytes);
    }
}

void* job(void* args) {
    int index = *((int*) args);
    printf("job for index: %d\n", index);
    process_client(CLIENTS[index]);
    STATUSES[index] = FINISHED;
    free(args);
    return 0;
}


void start_job(int index) {
    int* arg = (int*) malloc(sizeof(int));
    *arg = index;
    int status = pthread_create(&THREADS[index], NULL, job, arg);
    if (status != 0) {
        perror("create thread failed");
    }
}

void wait_job(pthread_t thread) {
    int status = pthread_join(thread, NULL);
    if (status != 0) {
        perror("thread join failed");
    }
}

void update_clients(int client_socket) {
    while (1) {
        for (int i = 0; i < THREAD_COUNT; ++i) {
            if (STATUSES[i] == FINISHED) {
                wait_job(THREADS[i]);
                close(CLIENTS[i]);
                STATUSES[i] = IDLE;
                printf("client finished: [%d]\n", i);
            }
            if (STATUSES[i] == IDLE) {
                printf("client start: [%d]\n", i);
                CLIENTS[i] = client_socket;
                STATUSES[i] = PROCESSING;
                start_job(i);
                return;
            }
        }
    }
}

void run_server() {
    char buf[1024];
    int bytes_read;

    int listener = create_and_bind();
    if (listen(listener, SERVER_MAX_CLIENTS_QUEUE) == -1) {
        perror("listen");
        exit(3);
    }

    struct sockaddr_in client_addr;
    socklen_t sin_size;
    char client_addr_str[INET_ADDRSTRLEN];
    while (1) {
        int client_socket = accept(listener, (struct sockaddr *)&client_addr, &sin_size);
        if (client_socket == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(AF_INET, &client_addr, client_addr_str, INET_ADDRSTRLEN);
        printf("server: got connection from [%s]\n", client_addr_str);
        update_clients(client_socket);
    }
}

void init() {
    for (size_t i = 0; i < THREAD_COUNT; ++i) {
        STATUSES[i] = IDLE;
    }
}

int main (int argc, char **argv) {
    init();
    parse_input_params(argc, argv);
    demonization_if_needed();

    run_server();
    printf("Server exit\n");
    return 0;
}

