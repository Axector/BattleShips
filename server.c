#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include "utils.h"

#define MAX_PLAYERS 10
#define PORT 12345
#define SHARED_MEMORY_SIZE 1024 * 1024
#define SERVER_DELTA_TIME 250.0f // Milliseconds

char *shared_memory = NULL;             // Stores whole shared data
uint32_t *server_time = NULL;           // Time passed on server
char *is_little_endian = NULL;          // To know if current system is little-endian or not
char *to_exit = NULL;                   // Check if server must be stopped
unsigned char *player_count = NULL;     // Current number of players
uint16_t *player_next_id = NULL;        // Next player unique ID
char *next_team_id = NULL;              // team ID for the next player
unsigned char *game_state = NULL;       // Current game state
struct Player *players = NULL;          // Stores all players data

void getSharedMemory();
void setDefaults();
void gameloop();
void startNetwork();
void processClient(int id, int socket);
uint16_t addPlayer(char *name);
struct Player* findPlayerById(int id);
char readPackage(int socket);

// Package types
char pkgLabdien(char *msg, size_t msg_size);

int main ()
{
    getSharedMemory();
    setDefaults();

    int pid = 0;
    pid = fork();
    if (pid == 0) {
        startNetwork();
    }
    else {
        gameloop();
    }

    return 0;
}

void getSharedMemory()
{
    shared_memory = mmap(
        NULL,
        SHARED_MEMORY_SIZE,
        PROT_READ|PROT_WRITE,
        MAP_SHARED|MAP_ANONYMOUS,
        -1,
        0
    );

    server_time = (uint32_t*) shared_memory; uint32_t shared_size = sizeof(uint32_t);
    is_little_endian = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    to_exit = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    player_count = (unsigned char*) (shared_memory + shared_size); shared_size += sizeof(char);
    player_next_id = (uint16_t*) (shared_memory + shared_size); shared_size += sizeof(uint16_t);
    next_team_id = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    game_state = (unsigned char*) (shared_memory + shared_size); shared_size += sizeof(char);
    players = (struct Player*) (shared_memory + shared_size); shared_size += sizeof(struct Player) * MAX_PLAYERS;
}

void setDefaults()
{
    *is_little_endian = isLittleEndianSystem();
    *player_next_id = 1;
    *next_team_id = 1;
}

void gameloop()
{
    float time = 0;
    while (1) {
        // Stop server gameloop
        if (*to_exit == 1) {
            exit(0);
        }

        // Calculate server time
        time += SERVER_DELTA_TIME / 1000;
        *server_time = (uint32_t) time;
        usleep(SERVER_DELTA_TIME * 1000);
    }
}

void startNetwork()
{
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("Socket failure");
        *to_exit = 1;
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
        *to_exit = 1;
        exit(EXIT_FAILURE);
    }

    if (listen(server, MAX_PLAYERS) < 0) {
        perror("listen failure");
        *to_exit = 1;
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

        int new_client_id = *player_next_id;

        // Read package with player's info
        if(readPackage(client_socket) == -1) {
            continue;
        }

        int cpid = 0;
        cpid = fork();

        if (cpid == 0) {
            close(server);
            cpid = fork();

            if (cpid == 0) {
                processClient(new_client_id, client_socket);
            }
            else {
                wait(NULL);
                printf("%s left the game.\n", findPlayerById(new_client_id)->name);
                *player_count -= 1;
            }

            exit(0);
        }
        else {
            close(client_socket);
        }
    }
}

void processClient(int id, int socket)
{
    printf("%s connected. ID=%d, SOCKET=%d\n", findPlayerById(id)->name, id, socket);

    char in[100];

    while (1) {
        ssize_t nread = read(socket, in, 100);

        if (nread == -1 || nread == 0) {
            exit(0);
        }
        in[nread] = '\0';

        printf("[Client-%d] Received: %s\t[%ld]\n", id, in, nread);
        write(socket, in, strlen(in));
    }
}

// Adds player with a passed name
uint16_t addPlayer(char *name)
{
    if (findPlayerById(*player_next_id) != NULL) {
        return 0;
    }

    struct Player* new_player = (players + *player_count);
    new_player->id = *player_next_id;
    new_player->team_id = *next_team_id;
    new_player->is_ready = 0;
    new_player->name_len = strlen(name);
    strcpy(new_player->name, name);

    // Update values for the next player
    *player_next_id += 1;
    *player_count += 1;
    *next_team_id = *next_team_id == 1 ? 2 : 1;

    // return player ID
    return new_player->id;
}

// To access player by its ID
struct Player* findPlayerById(int id)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].id == id) {
            return &players[i];
        }
    }
    return NULL;
}

char readPackage(int socket)
{
    char input[MAX_PACKAGE_SIZE];
    size_t nread = read(socket, input, MAX_PACKAGE_SIZE);

    if (nread == -1 || nread == 0) {
        return -1;
    }

    if (removePackageSeparator(input, &nread) == -1) {
        return -1;
    }

    unescapePackage(input, &nread);

    if (getPackageChecksum(input, nread) != calculatePackageChecksum(input, nread)) {
        return -1;
    }

    switch (getPackageType(input)) {
        case 0:
            return pkgLabdien(input, nread);
    }

    return 0;
}

char pkgLabdien(char *msg, size_t msg_size)
{
    char *name = (char*) (msg + sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint32_t));
    uint16_t id = addPlayer(name);

    if(id == 0) {
        return -1;
    }

    return id;
}
