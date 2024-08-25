#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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
    NETPRIT_ERR_PIPE,
    NETPRIT_ERR,
    NETPRIT_ERR_COUNT,
} netprit_err_t;

const char* const netprit_err_strings[] = {
    "NETPRIT_OK",         "NETPRIT_ERR_SOCKET", "NETPRIT_ERR_BIND",
    "NETPRIT_ERR_LISTEN", "NETPRIT_ERR_ACCEPT", "NETPRIT_ERR_PIPE",
    "NETPRIT_ERR",
};
_Static_assert(ARRAY_LEN(netprit_err_strings) == NETPRIT_ERR_COUNT,
               "You must keep your `netprit_err_t` enum and your "
               "`NETPRIT_ERR_COUNT` array in-sync!");

int g_launched_pid = -1;
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

const char* netprit_err_str(netprit_err_t err)
{
    if (err >= 0 && err < NETPRIT_ERR_COUNT)
        return netprit_err_strings[err];
    return NULL;
}

void netprit_print_err(netprit_err_t err)
{
    fprintf(stderr, "Error: %s", netprit_err_str(err));
    if (errno == 0)
        fprintf(stderr, "\n");
    else
        fprintf(stderr, ", errno: %s (%d) \n", strerror(errno), errno);
}

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

netprit_err_t read_and_launch_process(int client_socket)
{
    int pipefd[2];
    int res;
    char buffer[128];
    ssize_t nbytes;

    res = pipe(pipefd);
    if (res < 0) {
        return NETPRIT_ERR_PIPE;
    }

    pthread_mutex_lock(&g_mutex);
    g_launched_pid = fork();
    pthread_mutex_unlock(&g_mutex);

    switch (g_launched_pid) {
    case -1: {
        perror("fork");
        break;
    }
    case 0: {
        close(pipefd[0]);

        printf("Worker child: changing stdout to socketfd\n");

        // podría enviar directamente al client_socket
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);

        // close(pipefd[1]);
        // printf("soy el hijo %d, me quedo en while 1\n", getpid());
        execlp("ls", "ls", "-l", NULL);
        // perror("execlp");

        // while (1) {
        //     sleep(1);
        //     printf("hola\n");
        // }

        break;
    }
    default: {
        // close the write end
        close(pipefd[1]);

        while ((nbytes = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[nbytes] = '\0';
            printf("Received from child:\n%s", buffer);
            write(client_socket, buffer, nbytes);
            // write(1, buffer, sizeof(buffer));
        }

        // close the read end
        close(pipefd[0]);

        printf("Worker parent: waiting child to finish\n");
        waitpid(g_launched_pid, NULL, 0);

        pthread_mutex_lock(&g_mutex);
        g_launched_pid = -1;
        pthread_mutex_unlock(&g_mutex);

        printf("child died\n");

        break;
    }
    }

    return NETPRIT_OK;
}

void netprit_worker_handler(int server_socket)
{
    struct sockaddr_in client_addr;
    int addrlen = sizeof(client_addr);
    int client_socket;
    int res;

    if ((client_socket = accept(server_socket, (struct sockaddr*)&client_addr,
                                (socklen_t*)&addrlen)) < 0) {
        perror("Accept failed");
        return;
    }

    printf("Worker: connection accepted\n");

    res = read_and_launch_process(client_socket);
    if (res != NETPRIT_OK) {
        netprit_print_err(res);
    }

    close(client_socket);
}

void netprit_control_loop(int server_socket)
{
    struct sockaddr_in client_addr;
    int addrlen = sizeof(client_addr);
    int client_socket;

    while (1) {
        printf("Waiting on control loop\n");
        if ((client_socket =
                 accept(server_socket, (struct sockaddr*)&client_addr,
                        (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            close(server_socket);
            return;
        }

        pthread_mutex_lock(&g_mutex);
        if (g_launched_pid != -1) {
            printf("killing pid %d\n", g_launched_pid);
            kill(g_launched_pid, SIGKILL);
            g_launched_pid = -1;
        }
        pthread_mutex_unlock(&g_mutex);

        close(client_socket);
    }

    return;
}

netprit_err_t netprit_server_thread(int server_socket, void (*handler)(int),
                                    pthread_t* tid)
{
    int res;
    res = pthread_create(tid, NULL, (void*)handler,
                         (void*)(intptr_t)server_socket);
    if (res != 0) {
        return NETPRIT_ERR;
    }

    return NETPRIT_OK;
}

int main(int argc, char* argv[])
{
    int res;

    pthread_t control_tid;
    pthread_t worker_tid;

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

    printf("Server running...\n");

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
            res = netprit_server_thread(
                fd_control_socket, (void*)netprit_control_loop, &control_tid);
            if (res != NETPRIT_OK) {
                netprit_print_err(res);
                return EXIT_FAILURE;
            }
            FD_CLR(fd_control_socket, &fdset);
        }
        if (FD_ISSET(fd_worker_socket, &fdset)) {
            res = netprit_server_thread(
                fd_worker_socket, (void*)netprit_worker_handler, &worker_tid);
            if (res != NETPRIT_OK) {
                netprit_print_err(res);
                return EXIT_FAILURE;
            }
            FD_CLR(fd_worker_socket, &fdset);
        }
    }

    return 0;
}
