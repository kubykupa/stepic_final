#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <cstring>

#include <sys/types.h>
#include <sys/socket.h>
//#include <sys/epoll.h>

#include <arpa/inet.h>

const char* HOST        = NULL;
const char* DIRECTORY   = NULL;
const char* PORT        = NULL;

static const int SERVER_MAX_CLIENTS_QUEUE = 1000;
static const int READ_BUFFER_SIZE = 1024;

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

int make_socket_non_blocking (int sfd) {
    int flags = fcntl (sfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl get flags");
        return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl (sfd, F_SETFL, flags) == -1) {
        perror("fcntl set flags");
        return -1;
    }

    return 0;
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
    //char msg[] = "hello from server";
    //send_all(client_socket, msg, sizeof(msg));

    char buffer[READ_BUFFER_SIZE];
    int nbytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (nbytes == -1) {
        perror("read client socket");
        return;
    }

    buffer[nbytes] = '\0';
    //printf("request [%s]\n", buffer);

    char* find_index = strstr(buffer, "GET /index.html");
    if (find_index == NULL) {
        send_all(client_socket, (char*) RESPONSE_404, strlen(RESPONSE_404));
    } else {
        nbytes = sprintf(buffer, RESPONSE_200, strlen(RESPONSE_HTML), RESPONSE_HTML);
        send_all(client_socket, buffer, nbytes);
    }
}

void run_server() {
    char buf[1024];
    int bytes_read;

    int listener = create_and_bind();
    //make_socket_non_blocking(listener);
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
        //printf("server: got connection from [%s]\n", client_addr_str);

        if (!fork()) { // тут начинается рабочий процесс
            close(listener); // ему не нужен слушающий сокет
            process_client(client_socket);
            close(client_socket);
            //printf("finish client: [%s]\n", client_addr_str);
            exit(0);
        }
        close(client_socket);  // а этот сокет больше не нужен демону-родителю
    }
}

int main (int argc, char **argv) {
    parse_input_params(argc, argv);
    demonization_if_needed();

    run_server();
    //printf("Server exit\n");
    return 0;
}

