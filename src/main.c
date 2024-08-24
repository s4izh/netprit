#include "signal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define CONTROL_PORT 5000
#define WORKER_PORT 5001
#define TARGET_FILE "/tmp/netprit_worker"

#define ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

typedef enum {
    NETPRIT_OK = 0,
    NETPRIT_ERR_SOCKET,
    NETPRIT_ERR_BIND,
    NETPRIT_ERR_LISTEN,
    NETPRIT_ERR_ACCEPT,
    NETPRIT_ERR,
    NETPRIT_ERR_COUNT,
} netprit_err_t;

const char* const netprit_err_strings[] = {
    "NETPRIT_OK",         "NETPRIT_ERR_SOCKET", "NETPRIT_ERR_BIND",
    "NETPRIT_ERR_LISTEN", "NETPRIT_ERR_ACCEPT", "NETPRIT_ERR",
};
_Static_assert(ARRAY_LEN(netprit_err_strings) == NETPRIT_ERR_COUNT,
               "You must keep your `netprit_err_t` enum and your "
               "`NETPRIT_ERR_COUNT` array in-sync!");

const char* netprit_err_str(netprit_err_t err)
{
    if (err >= 0 && err < NETPRIT_ERR_COUNT)
        return netprit_err_strings[err];
    return NULL;
};

void netprit_print_err(netprit_err_t err)
{
    fprintf(stderr, "Error: %s", netprit_err_str(err));
    if (errno == 0)
        fprintf(stderr, "\n");
    else
        fprintf(stderr, ", errno: %s (%d) \n", strerror(errno), errno);
};

netprit_err_t netprit_server_create_socket(const int port, int* out_sockfd)
{
    int* sockfd = out_sockfd;
    struct sockaddr_in server_addr;

    if ((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
        return NETPRIT_ERR_SOCKET;

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port); // ensure big endian use

    if (bind(*sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) <
        0) {
        close(*sockfd);
        return NETPRIT_ERR_BIND;
    }

    if (listen(*sockfd, 3) < 0) {
        close(*sockfd);
        return NETPRIT_ERR_LISTEN;
    }

    return NETPRIT_OK;
}

netprit_err_t worker_handler(int client_socket)
{
    printf("hello from worker\n");

    return NETPRIT_OK;
}

void control_loop(int* p_socket)
{
    struct sockaddr_in client_addr;
    int addrlen = sizeof(client_addr);
    int client_socket;
    int server_socket = *p_socket;

    while (1) {
        if ((client_socket = accept(server_socket, (struct sockaddr*)&client_addr,
                                    (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            close(server_socket);
            return;
        }
        kill(-1, SIGKILL);

        close(client_socket);
    }
    return;
}

netprit_err_t control_handler(int server_socket)
{
    int res;
    pthread_t tid;

    int* p_socket = malloc(sizeof(int));
    *p_socket = server_socket;

    res = pthread_create(&tid, NULL, (void*)control_loop, (void*)p_socket);
    if (res != 0) {
        return NETPRIT_ERR;
    }

    return NETPRIT_OK;
}

int accept_connections(int server_socket, int port,
                       netprit_err_t (*handler)(int))
{
    struct sockaddr_in client_addr;
    int addrlen = sizeof(client_addr);
    int client_socket;

    printf("Waiting for connections on port %d...\n", port);

    if ((client_socket = accept(server_socket, (struct sockaddr*)&client_addr,
                                (socklen_t*)&addrlen)) < 0) {
        perror("Accept failed");
        close(server_socket);
        return NETPRIT_ERR_ACCEPT;
    }

    int res = handler(client_socket);
    if (res != NETPRIT_OK) {
        netprit_print_err(res);
    }
    close(client_socket);

    return NETPRIT_OK;
}

int main(int argc, char* argv[])
{
    int res;

    int fd_control_socket;
    int fd_worker_socket;
    fd_set fdset;

    res = netprit_server_create_socket(CONTROL_PORT, &fd_control_socket);
    if (res != NETPRIT_OK) {
        netprit_print_err(res);
        return EXIT_FAILURE;
    }

    res = netprit_server_create_socket(WORKER_PORT, &fd_worker_socket);
    if (res != NETPRIT_OK) {
        netprit_print_err(res);
        return EXIT_FAILURE;
    }

    printf("Server is running...\n");

    while (1) {
        FD_ZERO(&fdset);
        FD_SET(fd_control_socket, &fdset);
        FD_SET(fd_worker_socket, &fdset);

        int max_fd = (fd_control_socket > fd_worker_socket) ? fd_control_socket
                                                            : fd_worker_socket;

        int activity = select(max_fd + 1, &fdset, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
        }
        if (FD_ISSET(fd_control_socket, &fdset)) {
            res = control_handler(fd_control_socket);
            if (res != NETPRIT_OK) {
                netprit_print_err(res);
                return EXIT_FAILURE;
            }
        }
        if (FD_ISSET(fd_worker_socket, &fdset)) {
            res = accept_connections(fd_worker_socket, WORKER_PORT,
                                     &worker_handler);
            if (res != NETPRIT_OK) {
                netprit_print_err(res);
                return EXIT_FAILURE;
            }
            printf("clearing worker_socket from set\n");
            FD_CLR(fd_control_socket, &fdset);
        }
    }

    return 0;
}
