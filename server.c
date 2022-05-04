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
char *send_to_client = NULL;            // To send updates to the client each delta time
uint32_t *server_time = NULL;           // Time passed on server
char *is_little_endian = NULL;          // To know if current system is little-endian or not
char *to_exit = NULL;                   // Check if server must be stopped
unsigned char *to_exit_client = NULL;   // Array needed to close disconnected clients
unsigned char *players_count = NULL;     // Current number of players
uint8_t *player_next_id = NULL;         // Next player unique ID
uint32_t *last_package_npk = NULL;      // NPK for packages
unsigned char *game_state = NULL;       // Current game state
char *is_ready_all = NULL;
struct Player *players = NULL;         // Stores all players data
struct Ship *ships = NULL;
uint8_t *battlefield = NULL;

void getSharedMemory();
void setDefaults();
void gameloop();
void startNetwork();
void processClient(uint8_t id, int socket);
void addPlayer(uint8_t *name, uint16_t name_len, uint8_t *id, uint8_t *team_id);
void removePlayer(uint16_t id);
struct Player* findPlayerById(uint8_t id);
void processPackage(uint8_t *msg, int socket);

// Package types
void pkgLABDIEN(uint8_t *msg, uint32_t content_size, int socket);      // 0
void pkgREADY(uint8_t *msg, uint32_t content_size);                    // 4

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
    players_count = (unsigned char*) (shared_memory + shared_size); shared_size += sizeof(char);
    player_next_id = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    last_package_npk = (uint32_t*) (shared_memory + shared_size); shared_size += sizeof(uint32_t);
    game_state = (unsigned char*) (shared_memory + shared_size); shared_size += sizeof(char);
    is_ready_all = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    players = (struct Player*) (shared_memory + shared_size); shared_size += sizeof(struct Player) * MAX_PLAYERS;
    ships = (struct Ship*) (shared_memory + shared_size); shared_size += sizeof(struct Ship) * MAX_SHIPS;
    battlefield = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t) * BATTLEFIELD_X_MAX * BATTLEFIELD_Y_MAX;
}

void setDefaults()
{
    *is_little_endian = isLittleEndianSystem();
    *player_next_id = 1;
    *last_package_npk = 1;
}

void gameloop()
{
    float time = 0;
    uint32_t timer_time = 0;
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

        if (*is_ready_all == 1) {
            if (timer_time >= 6) {
                *is_ready_all = 0;
                timer_time = 0;
                *game_state = 1;
            }
            else {
                timer_time++;
            }
        }
        else if (timer_time > 0 && *is_ready_all == 0) {
            timer_time = 0;
        }
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
        uint8_t input[MAX_PACKAGE_SIZE];
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
            uint8_t input[MAX_PACKAGE_SIZE];
            uint32_t nread = read(socket, input, MAX_PACKAGE_SIZE);
            if (nread == -1 || nread == 0) {
                to_exit_client[id] = 1;
                exit(0);
            }

            if(unpackPackage(input, nread, *last_package_npk, *is_little_endian) == -1) {
                continue;
            }
            processPackage(input, socket);
            continue;
        }

        switch (*game_state) {
            // LOBBY
            case 0: {
                uint32_t current_players_len = (sizeof(struct Player) - sizeof(uint8_t)) * MAX_PLAYERS + sizeof(uint8_t);
                uint8_t current_players[current_players_len];

                uint32_t content_size = 0;
                current_players[content_size++] = *players_count;
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    current_players[content_size++] = players[i].id;
                    current_players[content_size++] = players[i].team_id;
                    current_players[content_size++] = players[i].is_ready;
                    current_players[content_size++] = players[i].name_len;
                    for (int l = 0; l < MAX_PLAYER_NAME_LEN; l++) {
                        current_players[content_size++] = players[i].name[l];
                    }
                }

                *last_package_npk += 1;
                uint8_t *message = preparePackage(*last_package_npk, 3, current_players, &current_players_len, current_players_len, *is_little_endian);
                write(socket, message, current_players_len);

                break;
            }
            // START_SETUP
            case 1: {
                uint8_t msg[2];
                msg[0] = BATTLEFIELD_X_MAX;
                msg[1] = BATTLEFIELD_Y_MAX;
                uint32_t content_size = 2;
                *last_package_npk += 1;
                uint8_t* pSTART_SETUP = preparePackage(*last_package_npk, 5, msg, &content_size, content_size, *is_little_endian);
                write(socket, pSTART_SETUP, content_size);
                *game_state = 2;
                break;
            }
            // STATE
            case 2: {
                uint32_t battlefield_size = BATTLEFIELD_X_MAX * BATTLEFIELD_Y_MAX;
                uint32_t full_content_size =
                    sizeof(uint8_t) * 2 +
                    sizeof(uint8_t) * battlefield_size +
                    sizeof(struct Ship) * MAX_SHIPS + sizeof(uint8_t) +
                    sizeof(struct Player) * MAX_PLAYERS + sizeof(uint8_t);

                uint8_t content[full_content_size];
                uint32_t content_size = 0;

                content[content_size++] = BATTLEFIELD_X_MAX;
                content[content_size++] = BATTLEFIELD_Y_MAX;
                for (int i = 0; i < battlefield_size; i++) {
                    content[content_size++] = battlefield[i];
                }
                content[content_size++] = MAX_SHIPS;
                for (int i = 0; i < sizeof(struct Ship) * MAX_SHIPS; i++) {
                    content[content_size++] = *(((char*)ships) + i);
                }
                content[content_size++] = *players_count;
                for (int i = 0; i < sizeof(struct Player) * MAX_SHIPS; i++) {
                    content[content_size++] = *(((char*)players) + i);
                }

                *last_package_npk += 1;
                uint8_t *message = preparePackage(*last_package_npk, 6, content, &full_content_size, full_content_size, *is_little_endian);
                write(socket, message, full_content_size);

                break;
            }
        }
    }
}

// Adds player with a passed name
void addPlayer(uint8_t *name, uint16_t name_len, uint8_t *id, uint8_t *team_id)
{
    if (findPlayerById(*player_next_id) != NULL) {
        return;
    }

    if (*players_count >= MAX_PLAYERS) {
        *id = *player_next_id;
        *player_next_id += 1;
        return;
    }

    struct Player* new_player = players;
    for (int i = 1; new_player->id != 0; i++) {
        new_player = (players + i);
    }

    new_player->id = *player_next_id;
    new_player->team_id = (*players_count % 2) ? 2 : 1;
    new_player->is_ready = 0;
    new_player->name_len = name_len;
    for (int i = 0; i < name_len; i++) {
        new_player->name[i] = name[i];
    }

    // Update values for the next player
    *player_next_id += 1;
    *players_count += 1;

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
    *players_count -= 1;

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

void processPackage(uint8_t *msg, int socket)
{
    *last_package_npk = getPackageNPK(msg, *is_little_endian);
    if (*last_package_npk >= UINT32_MAX) {
        *last_package_npk = 0;
    }

    switch (getPackageType(msg)) {
        case 0: {
            pkgLABDIEN(msg, getPackageContentSize(msg, *is_little_endian), socket);
            break;
        }
        case 4: {
            pkgREADY(msg, getPackageContentSize(msg, *is_little_endian));
            break;
        }
    }
}

void pkgLABDIEN(uint8_t *msg, uint32_t content_size, int socket)
{
    uint8_t *name = getPackageContent(msg, content_size);
    uint8_t player_id = 0;
    uint8_t player_team_id = 0;
    addPlayer(name, content_size, &player_id, &player_team_id);

    uint8_t playerData[2];
    playerData[0] = player_id;
    playerData[1] = player_team_id;
    uint32_t playerDataLen = 2;
    *last_package_npk += 1;
    uint8_t *pkgACK = preparePackage(*last_package_npk, 1, playerData, &playerDataLen, playerDataLen, *is_little_endian);
    write(socket, pkgACK, playerDataLen);
}

void pkgREADY(uint8_t *msg, uint32_t content_size)
{
    uint8_t *content = getPackageContent(msg, content_size);

    struct Player *player = findPlayerById(content[0]);
    player->is_ready = content[1];

    printf("%s %s ready\n", player->name, (player->is_ready == 1) ? "is" : "is not");

    for (int i = 0; i < *players_count; i++) {
        if (players[i].is_ready == 0) {
            *is_ready_all = 0;
            return;
        }
    }
    *is_ready_all = 1;
}
