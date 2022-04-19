#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>

#define MAX_CLIENTS 10
#define PORT 12345
#define SHARED_MEMORY_SIZE 1024

char *shared_memory = NULL;
unsigned char *client_count = NULL;
unsigned char *client_next_id = NULL;
int *shared_data = NULL;

void get_shared_memory();
void gameloop();
void start_network();
void process_client(int id, int socket);
char* msg_decoder(char* msg);

int main ()
{
    get_shared_memory();

    int pid = 0;
    pid = fork();
    if (pid == 0) {
        start_network();
    }
    else {
        gameloop();
    }

    return 0;
}

void get_shared_memory()
{
    shared_memory = mmap(
        NULL,
        SHARED_MEMORY_SIZE,
        PROT_READ|PROT_WRITE,
        MAP_SHARED|MAP_ANONYMOUS,
        -1,
        0
    );
    client_count = (unsigned char*) shared_memory;
    client_next_id = (unsigned char*) (shared_memory + sizeof(char));
    shared_data = (int*) (shared_memory + sizeof(char) * 2);
}

void gameloop()
{
    printf("Starting game loop! (It will run forever - Use Ctrl+C)\n");
    int i = 0;
    while (1) {
        for (i = 0; i < *client_count; i++) {
            shared_data[MAX_CLIENTS+i] += shared_data[i];
            shared_data[i] = 0;
        }

        sleep(1);
    }
}

void start_network()
{
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("Socket failure");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    if (bind(
        server,
        (struct sockaddr*) &server_address,
        sizeof(server_address)
    ) < 0) {
        perror("bind failure");
        exit(EXIT_FAILURE);
    }

    if (listen(server, MAX_CLIENTS) < 0) {
        perror("listen failure");
        exit(EXIT_FAILURE);
    }
    printf("Server is listening...\n\n");

    struct sockaddr_in client_address;
    socklen_t client_address_size = sizeof(client_address);

    while (1) {
        int client_socket = accept(
            server,
            (struct sockaddr*) &client_address,
            &client_address_size
        );

        if (client_socket < 0) {
            perror("accept failure");
            continue;
        }

        int new_client_id = *client_next_id;
        *client_next_id += 1;
        *client_count += 1;

        int cpid = 0;
        cpid = fork();

        if (cpid == 0) {
            close(server);
            cpid = fork();

            if (cpid == 0) {
                process_client(new_client_id, client_socket);
            }
            else {
                wait(NULL);
                printf("Successfully orphaned client %d\n", new_client_id);
                *client_count -= 1;
            }

            exit(0);
        }
        else {
            close(client_socket);
        }
    }
}

void process_client(int id, int socket)
{
    printf("Processing client id=%d, socket=%d\t", id, socket);
    printf("Client count: %d\n", *client_count);

    char in[100];
    char out[100];

    while (1) {
        ssize_t nread = read(socket, in, 100);

        if (nread == -1 || nread == 0) {
            exit(0);
        }
        in[nread] = '\0';

        printf("[Client-%d] Received: %s\t[%ld]\n", id, in, nread);

        strcpy(out, msg_decoder(in));
        write(socket, in, strlen(in));

        printf("[Server] Sent to Client-%d: %s\n", id, out);
    }
}

char* msg_decoder(char* msg)
{
    char res[150];

    for (int i = 0; i < strlen(msg); i++) {
        char temp[100];
        strcpy(temp, res);
        sprintf(res, "%s %x", temp, (int)msg[i]);
    }

    strcpy(msg, res);
    return msg;
}
