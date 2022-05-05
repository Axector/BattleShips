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

#define SERVER_DELTA_TIME 250.0f // Milliseconds

/// Variables
char *shared_memory = NULL;             // Stores whole shared data

uint32_t *server_time = NULL;           // Time passed on server
uint8_t *player_next_id = NULL;         // Next player unique ID
uint32_t *last_package_npk = NULL;      // NPK for packages

char *to_exit = NULL;                   // Check if server must be stopped
char *is_ready_all = NULL;
char *send_to_client = NULL;            // To send updates to the client each delta time
char *is_little_endian = NULL;          // To know if current system is little-endian or not
unsigned char *to_exit_client = NULL;   // Array needed to close disconnected clients

uint8_t *players_count = NULL;          // Current number of players
struct Player *players = NULL;          // Stores all players data
struct Ship *ships = NULL;

uint8_t *count_active_player = NULL;
uint8_t *count_active_ships = NULL;
uint8_t *battlefield_x = NULL;
uint8_t *battlefield_y = NULL;
uint8_t *battlefield = NULL;
uint8_t *game_state = NULL;             // Current game state


/// Functions in this file
void getSharedMemory();
void setDefaults();
void gameloop();
void startNetwork();
void processClient(uint8_t id, int socket);

void addPlayer(uint8_t *name, uint16_t name_len, uint8_t *id, uint8_t *team_id);
void removePlayer(uint8_t id);
void placeObjectOnBattlefield(uint8_t id, uint8_t x, uint8_t y);
void placeShip(struct Ship* ship);
void clearShip(struct Ship* ship);
struct Player* getNextPlayer(uint8_t n);
struct Ship* getNextShip(uint8_t n);
void processPackage(uint8_t *msg, int socket);

// Package types
void pkgLABDIEN(uint8_t *msg, uint32_t content_size, int socket);       // 0
void pkgREADY(uint8_t *msg, uint32_t content_size);                     // 4
void pkgSTART_ANY(uint8_t type, int socket);                            // 5 / 9
void pkgSTATE(int socket);                                              // 6
void pkgTEV_JALIEK(int socket);                                         // 7
void pkgES_LIEKU(uint8_t *msg, uint32_t content_size, int socket);      // 8
void pkgTEV_JAIET(int socket);                                          // 10
void pkgGAJIENS(uint8_t *msg, uint32_t content_size);                   // 11


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
    player_next_id = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    last_package_npk = (uint32_t*) (shared_memory + shared_size); shared_size += sizeof(uint32_t);
    to_exit = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    is_ready_all = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    send_to_client = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    is_little_endian = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    players_count = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    players = (struct Player*) (shared_memory + shared_size); shared_size += sizeof(struct Player) * MAX_PLAYERS;
    count_active_ships = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    ships = (struct Ship*) (shared_memory + shared_size); shared_size += sizeof(struct Ship) * MAX_SHIPS;
    count_active_player = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    battlefield_x = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    battlefield_y = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    battlefield = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t) * BATTLEFIELD_X_MAX * BATTLEFIELD_Y_MAX;
    game_state = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
}

void setDefaults()
{
    *is_little_endian = isLittleEndianSystem();
    *player_next_id = 1;
    *last_package_npk = 1;
    *battlefield_x = BATTLEFIELD_X_MAX;
    *battlefield_y = BATTLEFIELD_Y_MAX;

    for (int i = 0, type = 1; i < MAX_SHIPS; i += 2) {
        for (int k = 0; k < 2; k++) {
            ships[i + k].type = type;
            ships[i + k].x = 0;
            ships[i + k].y = 0;
            ships[i + k].dir = 0;
            ships[i + k].team_id = k + 1;
            ships[i + k].damage = 0;
        }
        type++;
    }
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
    struct Player *this_player = findPlayerById(players, id);
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

        *send_to_client = 0;

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

        // LOBBY
        if (*game_state == 0) {
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
        }
        // START_SETUP
        else if (*game_state == 1) {
            pkgSTART_ANY(5, socket);
            getNextPlayer(*count_active_player)->active = 1;
            *game_state = 2;
        }
        // STATE [SETUP]
        else if (*game_state == 2) {
            pkgTEV_JALIEK(socket);
            pkgSTATE(socket);
        }
        // START_GAME
        else if (*game_state == 3) {
            pkgSTART_ANY(9, socket);
            *game_state = 4;
        }
        // STATE [GAME]
        else if (*game_state == 4) {
            pkgTEV_JAIET(socket);
            pkgSTATE(socket);
        }
        // END_GAME
        else if (*game_state == 5) {
            uint32_t content_size = 2;
            uint8_t msg[content_size];
            msg[0] = 0;     // winner id
            msg[1] = 0;     // winner team_id
            *last_package_npk += 1;
            uint8_t* pEND_GAME = preparePackage(*last_package_npk, 12, msg, &content_size, content_size, *is_little_endian);
            write(socket, pEND_GAME, content_size);
        }
    }
}

// Adds player with a passed name
void addPlayer(uint8_t *name, uint16_t name_len, uint8_t *id, uint8_t *team_id)
{
    if (findPlayerById(players, *player_next_id) != NULL) {
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

void removePlayer(uint8_t id)
{
    struct Player* player = findPlayerById(players, id);

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

void placeObjectOnBattlefield(uint8_t id, uint8_t x, uint8_t y)
{
    if (x < 0 || x >= *battlefield_x || y < 0 || y >= *battlefield_y) {
        return;
    }

    battlefield[x * BATTLEFIELD_X_MAX + y] = id;
}

void placeShip(struct Ship* ship)
{
    uint8_t dx = (ship->dir == 1) ? 1 : (ship->dir == 3) ? -1 : 0;
    uint8_t dy = (ship->dir == 0) ? -1 : (ship->dir == 2) ? 1 : 0;

    for (int i = 0; i < 6 - ship->type; i++) {
        placeObjectOnBattlefield(ship->type, ship->x + i * dx, ship->y + i * dy);
    }
}

void clearShip(struct Ship* ship)
{
    uint8_t dx = (ship->dir == 1) ? 1 : (ship->dir == 3) ? -1 : 0;
    uint8_t dy = (ship->dir == 0) ? -1 : (ship->dir == 2) ? 1 : 0;

    for (int i = 0; i < 6 - ship->type; i++) {
        placeObjectOnBattlefield(0, ship->x + i * dx, ship->y + i * dy);
    }
}

struct Player* getNextPlayer(uint8_t n)
{
    uint8_t team_id = (n % 2) ? 2 : 1;
    for (int i = 0, j = 0; j < MAX_PLAYERS; j++) {
        if (players[j].id != 0 && players[j].team_id == team_id) {
            if (i == n / 2) {
                return &players[j];
            }
            i++;
        }
    }

    return NULL;
}

struct Ship* getNextShip(uint8_t n)
{
    uint8_t team_id = (n % 2) ? 2 : 1;
    for (int i = 0, j = 0; j < MAX_PLAYERS; j++) {
        if (ships[j].type != 0 && ships[j].team_id == team_id) {
            if (i == n / 2) {
                return &ships[j];
            }
            i++;
        }
    }

    return NULL;
}

void processPackage(uint8_t *msg, int socket)
{
    uint32_t npk = getPackageNPK(msg, *is_little_endian);
    if (npk != 0) {
        *last_package_npk = npk;
    }
    if (*last_package_npk >= UINT32_MAX) {
        *last_package_npk = 0;
    }

    uint8_t msg_type = getPackageType(msg);
    if (msg_type == 0) {
        pkgLABDIEN(msg, getPackageContentSize(msg, *is_little_endian), socket);
    }
    else if (msg_type == 4) {
        pkgREADY(msg, getPackageContentSize(msg, *is_little_endian));
    }
    else if (msg_type == 8) {
        pkgES_LIEKU(msg, getPackageContentSize(msg, *is_little_endian), socket);
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

    struct Player *player = findPlayerById(players, content[0]);
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

void pkgSTART_ANY(uint8_t type, int socket)
{
    uint32_t content_size = 2;
    uint8_t msg[content_size];
    msg[0] = *battlefield_x;
    msg[1] = *battlefield_y;
    *last_package_npk += 1;
    uint8_t* pSTART_ANY = preparePackage(*last_package_npk, type, msg, &content_size, content_size, *is_little_endian);
    write(socket, pSTART_ANY, content_size);
}

void pkgSTATE(int socket)
{
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
}

void pkgTEV_JALIEK(int socket)
{
    if (*count_active_ships >= MAX_SHIPS) {
        *count_active_player = 0;
        *count_active_ships = 0;
        *game_state = 3;
        return;
    }

    struct Player* player = getNextPlayer(*count_active_player);
    struct Ship* ship = getNextShip(*count_active_ships);

    uint32_t content_size = 2;
    uint8_t msg[content_size];

    msg[0] = player->id;
    msg[1] = ship->type;

    *last_package_npk += 1;
    uint8_t* pTEV_JALIEK = preparePackage(*last_package_npk, 7, msg, &content_size, content_size, *is_little_endian);
    write(socket, pTEV_JALIEK, content_size);
}

void pkgES_LIEKU(uint8_t *msg, uint32_t content_size, int socket)
{
    uint8_t *content = getPackageContent(msg, content_size);

    struct Player* player = findPlayerById(players, content[0]);
    if (player->active == 0) {
        return;
    }

    player->active = 0;
    struct Ship* ship = findShipByIdAndTeamId(ships, content[1], player->team_id)
    ship->x = content[2];
    ship->y = content[3];
    ship->dir = content[4];
    placeShip(ship);

    *count_active_player += 1;
    *count_active_ships += 1;

    if (*count_active_player >= *players_count) {
        *count_active_player = 0;
    }

    pkgTEV_JALIEK(socket);
}

void pkgTEV_JAIET(int socket)
{
    struct Player* player = getNextPlayer(*count_active_player);
    struct Ship* ship = getNextShip(*count_active_ships);

    uint32_t content_size = 6;
    uint8_t msg[content_size];
    msg[0] = player->id;
    msg[1] = ship->type;
    msg[2] = ship->x;
    msg[3] = ship->y;
    msg[4] = ship->dir;
    msg[5] = ship->damage;

    *last_package_npk += 1;
    uint8_t* pTEV_JAIET = preparePackage(*last_package_npk, 10, msg, &content_size, content_size, *is_little_endian);
    write(socket, pTEV_JAIET, content_size);
}

void pkgGAJIENS(uint8_t *msg, uint32_t content_size)
{
    uint8_t *content = getPackageContent(msg, content_size);

    struct Player* player = findPlayerById(players, content[0]);
    if (player->active == 0) {
        return;
    }

    player->active = 0;
    struct Ship* ship = getNextShip(*count_active_ships);

    if (content[1] == 1) {
        clearShip(ship);
        ship->x = content[2];
        ship->y = content[3];
        ship->dir = content[4];
        placeShip(ship);
    }
    else if (content[1] == 2) {
        // Attack
    }
    else if (content[1] == 3) {
        // Use Power-Up
    }

    *count_active_player += 1;
    *count_active_ships += 1;

    if (*count_active_player >= *players_count) {
        *count_active_player = 0;
    }
}
