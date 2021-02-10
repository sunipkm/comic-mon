// Server side C/C++ program to demonstrate Socket programming
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>

#ifndef SO_REUSEPORT
#define SO_REUSEPORT 15
#endif

#define eprintf(...) fprintf(stderr, __VA_ARGS__);

volatile sig_atomic_t done = 0;
void sig_handler(int in)
{
    done = 1;
    eprintf("%s: Received signal %d\n", __func__, in);
}
#define PORT 12376

void *rcv_fcn(void *sock)
{
    char buffer[1024] = {0};
    while (!done)
    {
        if (*(int *)sock > 0)
        {
            int sz = recv(*(int *)sock, buffer, sizeof(buffer), MSG_NOSIGNAL);
            eprintf("%s: Received %d bytes: %s\n", __func__, sz, buffer);
            memset(buffer, 0x0, 1024);
        }
        usleep(1000000 / 120); // receive at 120 Hz
    }
    return NULL;
}

int main(int argc, char const *argv[])
{
    int server_fd, new_socket = -1, valread;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char hello[1024];

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)))
    {
        perror("setsockopt");
        printf("%s: %d\n", __func__, __LINE__);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT,
                   &opt, sizeof(opt)))
    {
        perror("setsockopt");
        printf("%s: %d\n", __func__, __LINE__);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    int flags = fcntl(server_fd, F_GETFL, 0);
    assert(flags != -1);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Forcefully attaching socket to the port 8080
    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    int counter = 0;

    pthread_t rcv_thread;

    int rc = pthread_create(&rcv_thread, NULL, rcv_fcn, (void *)&new_socket);
    if (rc < 0)
    {
        eprintf("Could not create receive thread, exiting...\b");
        close(new_socket);
        return 0;
    }

    signal(SIGINT, sig_handler);

    while (!done)
    {
        int sz = snprintf(hello + 1, 1023, "Hello from server, counter %d", ++counter);
        hello[0] = sz;
        sz = send(new_socket, hello, strlen(hello), MSG_NOSIGNAL);
        // eprintf("%s: Sent %d bytes: %s", __func__, sz, hello);
        if (sz < 0 && !done)
        {
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                                     (socklen_t *)&addrlen)) < 0)
            {
#ifdef SERVER_DEBUG
                perror("accept");
#endif
            }
        }
        usleep(1000000 / 30); // 60 Hz
    }

    pthread_join(rcv_thread, NULL);

    close(new_socket);

    return 0;
}
