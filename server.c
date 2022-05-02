#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>
#include "utils.h"

#define SERVER_DELTA_TIME 500.0f // Milliseconds

char *shared_memory = NULL;             // Stores whole shared data
char *send_to_client = NULL;              // To send updates to the client each delta time
uint32_t *server_time = NULL;           // Time passed on server
char *is_little_endian = NULL;          // To know if current system is little-endian or not
char *to_exit = NULL;                   // Check if server must be stopped
unsigned char *to_exit_client = NULL;   // Array needed to close disconnected clients
unsigned char *player_count = NULL;     // Current number of players
uint8_t *player_next_id = NULL;         // Next player unique ID
char *next_team_id = NULL;              // team ID for the next player
uint32_t *last_package_npk = NULL;      // NPK for packages
unsigned char *game_state = NULL;       // Current game state
struct Player *players = NULL;          // Stores all players data

void getSharedMemory();
void setDefaults();
void gameloop();
void startNetwork();
void processClient(uint8_t id, int socket);
void addPlayer(char *name, uint16_t name_len, uint8_t *id, uint8_t *team_id);
void removePlayer(uint16_t id);
struct Player* findPlayerById(uint8_t id);
void processPackage(char *msg, int socket);

// Package types
void pkgLabdien(char *msg, uint32_t content_size, int socket);    // 0

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

    to_exit_client = mmap(
        NULL,
        512,
        PROT_READ|PROT_WRITE,
        MAP_SHARED|MAP_ANONYMOUS,
        -1,
        0
    );

    uint32_t shared_size = 0;
    server_time = (uint32_t*) (shared_memory + shared_size); shared_size = sizeof(uint32_t);
    send_to_client = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    is_little_endian = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    to_exit = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    player_count = (unsigned char*) (shared_memory + shared_size); shared_size += sizeof(char);
    player_next_id = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    next_team_id = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    last_package_npk = (uint32_t*) (shared_memory + shared_size); shared_size += sizeof(uint32_t);
    game_state = (unsigned char*) (shared_memory + shared_size); shared_size += sizeof(char);
    players = (struct Player*) (shared_memory + shared_size); shared_size += sizeof(struct Player) * MAX_PLAYERS;
}

void setDefaults()
{
    *is_little_endian = isLittleEndianSystem();
    *player_next_id = 1;
    *next_team_id = 1;
    *last_package_npk = 1;
}

void gameloop()
{
    float time = 0;
    while (1) {
        // Stop server gameloop
        if (*to_exit == 1) {
            exit(0);
        }

        *send_to_client = 0;

        // Calculate server time
        time += SERVER_DELTA_TIME / 1000;
        *server_time = (uint32_t) time;
        usleep(SERVER_DELTA_TIME * 1000);

        *send_to_client = 1;
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
        char input[MAX_PACKAGE_SIZE];
        uint32_t nread = read(client_socket, input, MAX_PACKAGE_SIZE);
        if (nread == -1 || nread == 0) {
            continue;
        }

        if(unpackPackage(input, nread, *last_package_npk, *is_little_endian) == -1) {
            continue;
        }
        processPackage(input, client_socket);

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
                removePlayer(new_client_id);
            }

            exit(0);
        }
        else {
            close(client_socket);
        }
    }
}

void processClient(uint8_t id, int socket)
{
    struct Player *this_player = findPlayerById(id);
    if (this_player != NULL) {
        printf("%s connected. ID=%d, teamID=%d\n", this_player->name, id, this_player->team_id);
    }
    else {
        printf("Ghost connected.\n");
    }

    to_exit_client[id] = 0;

    int pid = 0;
    pid = fork();

    while (1) {
        if (to_exit_client[id] == 1) {
            to_exit_client[id] = 0;
            exit(0);
        }

        if (*send_to_client == 0) {
            continue;
        }

        if (pid != 0) {
            char input[MAX_PACKAGE_SIZE];
            uint32_t nread = read(socket, input, MAX_PACKAGE_SIZE);
            if (nread == -1 || nread == 0) {
                to_exit_client[id] = 1;
                exit(0);
            }

            if(unpackPackage(input, nread, *last_package_npk, *is_little_endian) == -1) {
                continue;
            }
            processPackage(input, socket);
        }

        switch (*game_state) {
            // LOBBY
            case 0: {
                uint32_t current_players_len = sizeof(struct Player) * MAX_PLAYERS + sizeof(uint8_t);
                char current_players[current_players_len];

                current_players[0] = *player_count;
                for (int i = 0; i < current_players_len; i++) {
                    current_players[i + 1] = *((char*)(players) + i);
                }

                *last_package_npk += 1;
                char *message = preparePackage(*last_package_npk, 3, current_players, &current_players_len, current_players_len, *is_little_endian);
                write(socket, message, current_players_len);
                break;
            }
        }
    }
}

// Adds player with a passed name
void addPlayer(char *name, uint16_t name_len, uint8_t *id, uint8_t *team_id)
{
    if (findPlayerById(*player_next_id) != NULL) {
        return;
    }

    if (*player_count >= MAX_PLAYERS) {
        *id = *player_next_id;
        *player_next_id += 1;
        return;
    }

    struct Player* new_player = players;
    for (int i = 1; new_player->id != 0; i++) {
        new_player = (players + i);
    }

    new_player->id = *player_next_id;
    new_player->team_id = *next_team_id;
    new_player->is_ready = 0;
    new_player->name_len = name_len;
    for (int i = 0; i < name_len; i++) {
        new_player->name[i] = name[i];
    }

    // Update values for the next player
    *player_next_id += 1;
    *player_count += 1;
    *next_team_id = *next_team_id == 1 ? 2 : 1;

    *id = new_player->id;
    *team_id = new_player->team_id;
}

void removePlayer(uint16_t id)
{
    struct Player* player = findPlayerById(id);

    if (player == NULL) {
        printf("Ghost left.\n");
        return;
    }

    printf("%s left the game.\n", player->name);
    *next_team_id = player->team_id;
    *player_count -= 1;

    player->id = 0;
    player->team_id = 0;
    player->is_ready = 0;
    for (int i = 0; i < player->name_len; i++) {
        player->name[i] = 0;
    }
    player->name_len = 0;
}

// To access player by its ID
struct Player* findPlayerById(uint8_t id)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].id == id) {
            return &players[i];
        }
    }
    return NULL;
}

void processPackage(char *msg, int socket)
{
    *last_package_npk = getPackageNPK(msg, *is_little_endian);
    if (*last_package_npk >= UINT32_MAX) {
        *last_package_npk = 0;
    }

    switch (getPackageType(msg)) {
        case 0: {
            pkgLabdien(msg, getPackageContentSize(msg, *is_little_endian), socket);
            break;
        }
    }
}

void pkgLabdien(char *msg, uint32_t content_size, int socket)
{
    char *name = getPackageContent(msg, content_size);
    uint8_t player_id = 0;
    uint8_t player_team_id = 0;
    addPlayer(name, content_size, &player_id, &player_team_id);

    char playerData[2];
    playerData[0] = player_id;
    playerData[1] = player_team_id;
    uint32_t playerDataLen = 2;
    *last_package_npk += 1;
    char *pkgACK = preparePackage(*last_package_npk, 1, playerData, &playerDataLen, playerDataLen, *is_little_endian);
    write(socket, pkgACK, playerDataLen);
}
