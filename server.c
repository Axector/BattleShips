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
#include <math.h>
#include<time.h>
#include "utils.h"

#define SERVER_DELTA_TIME 250.0f // Milliseconds

/// Variables
char *shared_memory = NULL;             // Stores whole shared data

uint32_t *server_time = NULL;           // Time passed on server
uint8_t *player_next_id = NULL;         // Next player unique ID
uint32_t *last_package_npk = NULL;      // NPK for packages

char *to_exit = NULL;                   // Check if server must be stopped
char *is_ready_all = NULL;              // Check if all players are ready to start
char *send_to_client = NULL;            // To send updates to the client each delta time
char *is_little_endian = NULL;          // To know if current system is little-endian or not
unsigned char *to_exit_client = NULL;   // Array needed to close disconnected clients

uint8_t *players_count = NULL;          // Current number of players
struct Player *players = NULL;          // Stores all players data
struct Ship *ships = NULL;              // Stores all ships data

uint8_t *count_active_player = NULL;    // Count players to know which one must be next active
uint8_t *count_active_ships_1 = NULL;   // Count ships of the 1 team to know which one must be next active
uint8_t *count_active_ships_2 = NULL;   // Count ships of the 2 team to know which one must be next active
uint8_t *battlefield_x = NULL;          // Battlefield X size
uint8_t *battlefield_y = NULL;          // Battlefield Y size
uint8_t *battlefield = NULL;            // Whole battlefield
uint8_t *game_state = NULL;             // Current game state
uint8_t *winner_team = NULL;            // Winner team id


/// Functions in this file
void getSharedMemory();
void setDefaults();
void gameloop();
void startNetwork();
void processClient(uint8_t id, int socket);

void bigRock(uint8_t x, uint8_t y);
void smallIsland(uint8_t x, uint8_t y);
void mediumIsland(uint8_t x, uint8_t y);
void fillBattlefield();
void addPlayer(uint8_t *name, uint16_t name_len, uint8_t *id, uint8_t *team_id);
void removePlayer(uint8_t id);
void placeObjectOnBattlefield(uint8_t id, uint8_t x, uint8_t y);
uint8_t getBattlefieldObject(uint8_t x, uint8_t y);
void placeShip(struct Ship* ship);
void clearShip(struct Ship* ship);
struct Player* getNextPlayer(uint8_t n);
struct Ship* getNextShip(uint8_t team_id);
struct Ship* getShipByCoord(uint8_t x, uint8_t y, uint8_t not_team_id);
void dealDamage(struct Ship* ship, uint8_t x, uint8_t y);
void processPackage(uint8_t *msg, int socket);

// Package types
void pkgLABDIEN(uint8_t *msg, uint32_t content_size, int socket);       // 0
void pkgREADY(uint8_t *msg, uint32_t content_size);                     // 4
void pkgSTART_ANY(uint8_t type, int socket);                            // 5 / 9
void pkgSTATE(int socket);                                              // 6
void pkgTEV_JALIEK(int socket);                                         // 7
void pkgES_LIEKU(uint8_t *msg, uint32_t content_size, int socket);      // 8
void pkgTEV_JAIET(int socket);                                          // 10
void pkgGAJIENS(uint8_t *msg, uint32_t content_size, int socket);       // 11


int main ()
{
    // Reserve and share memory between all needed variables
    getSharedMemory();
    // Set some default values
    setDefaults();

    int pid = 0;
    pid = fork();
    if (pid == 0) {
        // Start network and wait for players
        startNetwork();
    } else {
        // Start gameloop ticks
        gameloop();
    }

    return 0;
}

void getSharedMemory()
{
    // All needed memory
    shared_memory = mmap(
        NULL,
        SHARED_MEMORY_SIZE,
        PROT_READ|PROT_WRITE,
        MAP_SHARED|MAP_ANONYMOUS,
        -1,
        0
    );

    // Memory that is needed to store values if client want to exit
    to_exit_client = mmap(
        NULL,
        512,
        PROT_READ|PROT_WRITE,
        MAP_SHARED|MAP_ANONYMOUS,
        -1,
        0
    );

    // Share memory between all needed variables
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
    count_active_ships_1 = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    count_active_ships_2 = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    ships = (struct Ship*) (shared_memory + shared_size); shared_size += sizeof(struct Ship) * MAX_SHIPS;
    count_active_player = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    battlefield_x = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    battlefield_y = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    battlefield = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t) * BATTLEFIELD_X_MAX * BATTLEFIELD_Y_MAX;
    game_state = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    winner_team = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
}

// Set some default values
void setDefaults()
{
    *is_little_endian = isLittleEndianSystem();
    *player_next_id = 1;
    *last_package_npk = 1;
    *battlefield_x = BATTLEFIELD_X_MAX;
    *battlefield_y = BATTLEFIELD_Y_MAX;

    // Predefine possible ships
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

        // If all players left, return server to base settings
        if (*game_state != 0 && *players_count == 0) {
            *game_state = 0;

            *count_active_player = 0;
            *count_active_ships_1 = 0;
            *count_active_ships_2 = 0;

            // Clear battlefield
            for (int i = 0; i < BATTLEFIELD_X_MAX * BATTLEFIELD_Y_MAX; i++) {
                battlefield[i] = 0;
            }
        }

        // Calculate server time
        time += SERVER_DELTA_TIME / 1000;
        *server_time = (uint32_t) time;
        usleep(SERVER_DELTA_TIME * 1000);

        // To send packages from server each SERVER_DELTA_TIME
        *send_to_client = 1;

        // Start timer if all players are ready
        if (*is_ready_all == 1) {
            // Set timer for 3 seconds
            if (timer_time >= 6) {
                *is_ready_all = 0;
                timer_time = 0;
                *game_state = 1;
            } else {
                timer_time++;
            }
        // If someone became unready, stop the timer
        } else if (timer_time > 0 && *is_ready_all == 0) {
            timer_time = 0;
        }
    }
}

void startNetwork()
{
    // Create server socket
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("Socket failure");
        *to_exit = 1;
        exit(EXIT_FAILURE);
    }

    // Set server address
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    // Bind server socket to the address
    if (bind(
        server,
        (struct sockaddr*) &server_address,
        sizeof(server_address)
    ) < 0) {
        perror("bind failure");
        *to_exit = 1;
        exit(EXIT_FAILURE);
    }

    // Start listening to accept clients
    if (listen(server, MAX_PLAYERS) < 0) {
        perror("listen failure");
        *to_exit = 1;
        exit(EXIT_FAILURE);
    }
    printf("Server is listening...\n\n");

    struct sockaddr_in client_address;
    socklen_t client_address_size = sizeof(client_address);

    // Start loop to accept new clients
    while (1) {
        // Create client socket
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

        // Get LABDIEN package from the client
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
                // Start to work with client
                processClient(new_client_id, client_socket);
            } else {
                wait(NULL);
                // Remove client when it exits
                removePlayer(new_client_id);
            }

            exit(0);
        } else {
            close(client_socket);
        }
    }
}

void processClient(uint8_t id, int socket)
{
    struct Player *this_player = findPlayerById(players, id);
    // Player can be "Ghost" if he joined when game already started
    if (this_player != NULL) {
        printf("%s connected. ID=%d, teamID=%d\n", this_player->name, id, this_player->team_id);
    } else {
        printf("Ghost connected.\n");
    }

    to_exit_client[id] = 0;

    // Separate threads for receiving and sending packages
    int pid = 0;
    pid = fork();

    while (1) {
        // Close connection if client decided to exit
        if (to_exit_client[id] == 1) {
            to_exit_client[id] = 0;
            exit(0);
        }

        // Read, unpack and process packages
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

        // Skip loop when it is not time to send the package
        if (*send_to_client == 0) {
            continue;
        }

        *send_to_client = 0;

        // Send packages depending on the game state
        // LOBBY
        if (*game_state == 0) {
            uint32_t current_players_len = (sizeof(struct Player) - sizeof(uint8_t)) * MAX_PLAYERS + sizeof(uint8_t);
            uint8_t current_players[current_players_len];

            // Send all players information
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

            // preapare and send package with the specified type
            *last_package_npk += 1;
            uint8_t *message = preparePackage(*last_package_npk, 3, current_players, &current_players_len, current_players_len, *is_little_endian);
            write(socket, message, current_players_len);
        }
        // START_SETUP
        else if (*game_state == 1) {
            // Make first player active and send battlefield sizes for the first time
            pkgSTART_ANY(5, socket);
            getNextPlayer(*count_active_player)->active = 1;
            fillBattlefield();
            // Move to the next game state
            *game_state = 2;
        }
        // STATE [SETUP]
        else if (*game_state == 2) {
            // Send current setup state and ship with a player id to place the ship
            pkgTEV_JALIEK(socket);
            pkgSTATE(socket);
        }
        // START_GAME
        else if (*game_state == 3) {
            // Make first player active and send battlefield sizes for another time
            pkgSTART_ANY(9, socket);
            getNextPlayer(*count_active_player)->active = 1;
            *game_state = 4;
        }
        // STATE [GAME]
        else if (*game_state == 4) {
            // Send current game state and ship with a player id to make a move with that ship
            pkgTEV_JAIET(socket);
            pkgSTATE(socket);
        }
        // END_GAME
        else if (*game_state == 5) {
            uint32_t content_size = 2;
            uint8_t msg[content_size];
            msg[0] = 0;
            msg[1] = *winner_team;
            // Send a package which means that one team won
            *last_package_npk += 1;
            uint8_t* pEND_GAME = preparePackage(*last_package_npk, 12, msg, &content_size, content_size, *is_little_endian);
            write(socket, pEND_GAME, content_size);
        }
    }
}

// Check if some objects want to be placed on teams bases
char onBase(uint8_t x, uint8_t y)
{
    if (
        (x > PLANE_SIZE / 2 - 10 &&
        x < PLANE_SIZE / 2 + 10 &&
        y > PLANE_SIZE / 2 - 10 &&
        y < PLANE_SIZE / 2 + 10)
        ||
        (x > (PLANE_SIZE * 3 + PLANE_SIZE / 2) - 10 &&
        x < (PLANE_SIZE * 3 + PLANE_SIZE / 2) + 10 &&
        y > PLANE_SIZE / 2 - 10 &&
        y < PLANE_SIZE / 2 + 10)
    ) {
        return 1;
    }
    return 0;
}

// Place big rock on the battlefield
void bigRock(uint8_t x, uint8_t y)
{
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (onBase(x + i, y + j)) {
                continue;
            }

            if (
                (i == 0 && j == 0) ||
                (i == 2 && j == 0) ||
                (i == 2 && j == 2) ||
                (i == 0 && j == 2)
            ) {
                continue;
            }
            placeObjectOnBattlefield((enum BattlefieldObj) Rocks, x + i, y + j);
        }
    }
}

// Place small round island on the battlefield
void smallIsland(uint8_t x, uint8_t y)
{
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) {
            if (onBase(x + i, y + j)) {
                continue;
            }

            if (
                (i == 0 && j == 0) ||
                (i == 5 && j == 0) ||
                (i == 5 && j == 5) ||
                (i == 0 && j == 5) ||
                (x + i >= *battlefield_x) ||
                (y + j >= *battlefield_y)
            ) {
                continue;
            }
            placeObjectOnBattlefield((enum BattlefieldObj) Island, x + i, y + j);
        }
    }
}

// Place medium round island on the battlefield
void mediumIsland(uint8_t x, uint8_t y)
{
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            if (onBase(x + i, y + j)) {
                continue;
            }

            if (
                (i == 0 && j == 0) ||
                (i == 0 && j == 1) ||
                (i == 1 && j == 0) ||
                (i == 9 && j == 0) ||
                (i == 8 && j == 0) ||
                (i == 9 && j == 1) ||
                (i == 8 && j == 9) ||
                (i == 9 && j == 9) ||
                (i == 9 && j == 8) ||
                (i == 0 && j == 8) ||
                (i == 0 && j == 9) ||
                (i == 1 && j == 9) ||
                (x + i >= *battlefield_x) ||
                (y + j >= *battlefield_y)
            ) {
                continue;
            }

            placeObjectOnBattlefield((enum BattlefieldObj) Island, x + i, y + j);
        }
    }
}

// Randomly fill the battlefield with some objects
void fillBattlefield()
{
    srand(time(0));
    for (int x = 0; x < *battlefield_x; x++) {
        for (int y = 0; y < *battlefield_y; y++) {
            if (onBase(x, y)) {
                continue;
            }

            float random_if = rand() % 10000 / 100.0f;
            // Place Fish randomly on battlfield (0.05%)
            if (random_if < 0.05f) {
                placeObjectOnBattlefield((enum BattlefieldObj) Fish, x, y);
            }
            // Place Rocks randomly on battlfield (0.3%)
            else if (random_if < 0.35f) {
                placeObjectOnBattlefield((enum BattlefieldObj) Rocks, x, y);
            }
            // Place bigRock randomly on battlfield (0.05%)
            else if (random_if < 0.4f) {
                bigRock(x, y);
            }
            // Place smallIsland randomly on battlfield (0.03%)
            else if (random_if < 0.43f) {
                smallIsland(x, y);
            }
            // Place mediumIsland randomly on battlfield (0.01%)
            else if (random_if < 0.44f) {
                mediumIsland(x, y);
            }
        }
    }
}

// Adds player with a passed name
void addPlayer(uint8_t *name, uint16_t name_len, uint8_t *id, uint8_t *team_id)
{
    if (findPlayerById(players, *player_next_id) != NULL) {
        return;
    }

    // Add ghost player if game already started or there are maximum players already
    if (*game_state >= 2 || *players_count >= MAX_PLAYERS) {
        *id = *player_next_id;
        *player_next_id += 1;
        return;
    }

    // Get a place for a new player
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

// Remove player when he exits
void removePlayer(uint8_t id)
{
    struct Player* player = findPlayerById(players, id);

    // Player can be a ghost
    if (player == NULL) {
        printf("Ghost left.\n");
        return;
    }

    printf("%s left the game.\n", player->name);
    *players_count -= 1;

    // Remove all player data for another player
    player->id = 0;
    player->team_id = 0;
    player->is_ready = 0;
    for (int i = 0; i < player->name_len; i++) {
        player->name[i] = 0;
    }
    player->name_len = 0;
}

// Place an object on the battlefield
void placeObjectOnBattlefield(uint8_t id, uint8_t x, uint8_t y)
{
    if (x < 0 || x >= *battlefield_x || y < 0 || y >= *battlefield_y) {
        return;
    }

    battlefield[x + y * BATTLEFIELD_X_MAX] = id;
}

// Check object on the battlefield by its coordinates
uint8_t getBattlefieldObject(uint8_t x, uint8_t y)
{
    return battlefield[x + y * BATTLEFIELD_X_MAX];
}

// Place ship on the battlefield
void placeShip(struct Ship* ship)
{
    uint8_t x = 0;
    uint8_t y = 0;
    // Get direction increments
    char dx = (ship->dir == 1) ? 1 : (ship->dir == 3) ? -1 : 0;
    char dy = (ship->dir == 0) ? -1 : (ship->dir == 2) ? 1 : 0;

    // Place each part of the ship
    for (int i = 0; i < 6 - ship->type; i++) {
        x = ship->x + i * dx;
        y = ship->y + i * dy;

        // Check if there is collision with an enemy ship
        // If it is, then deal damage to both ships and make them immovable
        struct Ship* enemy_ship = getShipByCoord(x, y, ship->team_id);
        if (*game_state == 4 && enemy_ship != NULL) {
            placeObjectOnBattlefield((enum BattlefieldObj) Hit, x, y);
            dealDamage(ship, x, y);
            // Change ship id to make it visible to the enemy team
            ship->team_id = (ship->team_id == 1 || ship->team_id == 3) ? 3 : (ship->team_id == 0) ? 0 : 4;
            dealDamage(enemy_ship, x, y);
            enemy_ship->team_id = (enemy_ship->team_id == 1 || enemy_ship->team_id == 3) ? 3 : (enemy_ship->team_id == 0) ? 0 : 4;
        }
        // Otherwise place ship part normally on the battlefield
        else {
            placeObjectOnBattlefield(ship->type, x, y);
        }
    }
}

// Clear ship from battlefield
void clearShip(struct Ship* ship)
{
    uint8_t dx = (ship->dir == 1) ? 1 : (ship->dir == 3) ? -1 : 0;
    uint8_t dy = (ship->dir == 0) ? -1 : (ship->dir == 2) ? 1 : 0;

    for (int i = 0; i < 6 - ship->type; i++) {
        placeObjectOnBattlefield(0, ship->x + i * dx, ship->y + i * dy);
    }
}

// Get next player to become active
struct Player* getNextPlayer(uint8_t n)
{
    // Every next player must be from another team
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

// Get next ship to give it to player for the move
struct Ship* getNextShip(uint8_t team_id)
{
    uint8_t count_ships = 0;
    // Every next ship must be from another team
    uint8_t n = (team_id == 1) ? *count_active_ships_1 : *count_active_ships_2;
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (ships[i].team_id == team_id || ships[i].team_id == team_id + 2) {
            if (count_ships == n) {
                return &ships[i];
            }
            count_ships++;
        }
    }

    if (team_id == 1) {
        *count_active_ships_1 = 0;
    } else {
        *count_active_ships_2 = 0;
    }

    for (int i = 0; i < MAX_SHIPS; i++) {
        if (ships[i].team_id == team_id || ships[i].team_id == team_id + 2) {
            return &ships[i];
        }
    }

    // It can be, that there are no ships left for one of teams
    return NULL;
}

// Check if there is an enemy ship in given coordinates
struct Ship* getShipByCoord(uint8_t x, uint8_t y, uint8_t not_team_id)
{
    // Check each ships' part if it is in given coordinates
    for (int i = 0; i < MAX_SHIPS; i++) {
        uint8_t dir = ships[i].dir;
        // Get direction increment
        char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
        char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;
        for (int a = 0; a < 6 - ships[i].type; a++) {
            if (
                (ships[i].x + a*dx) == x &&
                (ships[i].y + a*dy) == y &&
                ships[i].team_id != 0 &&
                ships[i].team_id != not_team_id
            ) {
                return &ships[i];
            }
        }
    }
    return NULL;
}

// Deal damage to the ship
void dealDamage(struct Ship* ship, uint8_t x, uint8_t y)
{
    uint8_t dir = ship->dir;
    // Get direction increment
    char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
    char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;

    // Check to which part we should apply the damage
    for (int a = 0; a < 6 - ship->type; a++) {
        if ((ship->x + a*dx) == x && (ship->y + a*dy) == y) {
            // We are using bitmask to deal the damage to a specific part
            ship->damage |= 1 << a;
            break;
        }
    }

    // If damage is maximal for the current ship, it must be marked as drowned
    if (ship->damage >= (uint8_t) pow(2, 6 - ship->type) - 1) {
        ship->team_id = 0;
    }
}

// Process package, received from client
void processPackage(uint8_t *msg, int socket)
{
    // Update NPK for the next package
    uint32_t npk = getPackageNPK(msg, *is_little_endian);
    if (npk != 0) {
        *last_package_npk = npk;
    }
    if (*last_package_npk >= UINT32_MAX) {
        *last_package_npk = 0;
    }

    // Process package by a specific message type
    uint8_t msg_type = getPackageType(msg);
    if (msg_type == 0) {
        pkgLABDIEN(msg, getPackageContentSize(msg, *is_little_endian), socket);
    } else if (msg_type == 4) {
        pkgREADY(msg, getPackageContentSize(msg, *is_little_endian));
    } else if (msg_type == 8) {
        pkgES_LIEKU(msg, getPackageContentSize(msg, *is_little_endian), socket);
    } else if (msg_type == 11) {
        pkgGAJIENS(msg, getPackageContentSize(msg, *is_little_endian), socket);
    }
}

// Process package LABDIEN
void pkgLABDIEN(uint8_t *msg, uint32_t content_size, int socket)
{
    uint8_t *name = getPackageContent(msg, content_size);
    uint8_t player_id = 0;
    uint8_t player_team_id = 0;

    // Save player data
    addPlayer(name, content_size, &player_id, &player_team_id);

    uint8_t playerData[2];
    playerData[0] = player_id;
    playerData[1] = player_team_id;
    uint32_t playerDataLen = 2;

    // Send acknowledgment to the client that it is connected with his new id and team id
    *last_package_npk += 2;
    uint8_t *pkgACK = preparePackage(*last_package_npk, 1, playerData, &playerDataLen, playerDataLen, *is_little_endian);
    write(socket, pkgACK, playerDataLen);
}

// Process package READY
void pkgREADY(uint8_t *msg, uint32_t content_size)
{
    uint8_t *content = getPackageContent(msg, content_size);

    // Change player is_ready value
    struct Player *player = findPlayerById(players, content[0]);
    player->is_ready = content[1];

    // This package is for both ready and unready states
    printf("%s %s ready\n", player->name, (player->is_ready == 1) ? "is" : "is not");

    // Check if all players are ready or not
    for (int i = 0; i < *players_count; i++) {
        if (players[i].is_ready == 0) {
            *is_ready_all = 0;
            return;
        }
    }
    *is_ready_all = 1;
}

// Prepare package which means a step to the new game state on the client
void pkgSTART_ANY(uint8_t type, int socket)
{
    uint32_t content_size = 2;
    uint8_t msg[content_size];
    msg[0] = *battlefield_x;
    msg[1] = *battlefield_y;

    // Send sizes of the battlefield
    *last_package_npk += 2;
    uint8_t* pSTART_ANY = preparePackage(*last_package_npk, type, msg, &content_size, content_size, *is_little_endian);
    write(socket, pSTART_ANY, content_size);
}

// Prepare package with current state
void pkgSTATE(int socket)
{
    // Calculate package size
    uint32_t battlefield_size = BATTLEFIELD_X_MAX * BATTLEFIELD_Y_MAX;
    uint32_t full_content_size =
        sizeof(uint8_t) * 2 +
        sizeof(uint8_t) * battlefield_size +
        sizeof(struct Ship) * MAX_SHIPS + sizeof(uint8_t) +
        sizeof(struct Player) * MAX_PLAYERS + sizeof(uint8_t);

    uint8_t content[full_content_size];
    uint32_t content_size = 0;

    // Sizes of the battlefield
    content[content_size++] = BATTLEFIELD_X_MAX;
    content[content_size++] = BATTLEFIELD_Y_MAX;
    // Battlefield itself
    for (int i = 0; i < battlefield_size; i++) {
        content[content_size++] = battlefield[i];
    }
    // Amount of ships
    content[content_size++] = MAX_SHIPS;
    // Ships information
    for (int i = 0; i < sizeof(struct Ship) * MAX_SHIPS; i++) {
        content[content_size++] = *(((char*)ships) + i);
    }
    // Amount of players
    content[content_size++] = *players_count;
    // Players information
    for (int i = 0; i < sizeof(struct Player) * MAX_SHIPS; i++) {
        content[content_size++] = *(((char*)players) + i);
    }

    *last_package_npk += 1;
    uint8_t *message = preparePackage(*last_package_npk, 6, content, &full_content_size, full_content_size, *is_little_endian);
    write(socket, message, full_content_size);
}

// Preapare and send package to force one of players to place a ship
void pkgTEV_JALIEK(int socket)
{
    uint32_t content_size = 2;
    uint8_t msg[content_size];

    // Place all possible ships, then move to the next game state
    if (*count_active_ships_1 + *count_active_ships_2 >= MAX_SHIPS) {
        *count_active_player = 0;
        *count_active_ships_1 = 0;
        *count_active_ships_2 = 0;

        // Send last package of this type to update move of the last player
        msg[0] = 0;
        msg[1] = 0;
        *last_package_npk += 1;
        uint8_t* pTEV_JALIEK = preparePackage(*last_package_npk, 7, msg, &content_size, content_size, *is_little_endian);
        write(socket, pTEV_JALIEK, content_size);

        *game_state = 3;
        return;
    }

    // Get next player to be active
    struct Player* player = getNextPlayer(*count_active_player);
    if (player == NULL) {
        // Try to get another player
        *count_active_player += 1;
        if (*count_active_player >= *players_count) {
            *count_active_player = 0;
        }
        return;
    }
    // Get next ship to place
    struct Ship* ship = getNextShip(player->team_id);

    msg[0] = player->id;
    msg[1] = ship->type;
    player->active = 1;

    // Send player id and ship type to place it on battlefield
    *last_package_npk += 1;
    uint8_t* pTEV_JALIEK = preparePackage(*last_package_npk, 7, msg, &content_size, content_size, *is_little_endian);
    write(socket, pTEV_JALIEK, content_size);
}

// Process package ES_LIEKU with new coordinates for ships
void pkgES_LIEKU(uint8_t *msg, uint32_t content_size, int socket)
{
    uint8_t *content = getPackageContent(msg, content_size);

    // Find current active player to make it inactive
    struct Player* player = findPlayerById(players, content[0]);
    if (player->active == 0) {
        return;
    }

    player->active = 0;
    // Update ships coordinates and rotation direction
    struct Ship* ship = findShipByIdAndTeamId(ships, content[1], player->team_id);
    ship->x = content[2];
    ship->y = content[3];
    ship->dir = content[4];
    // Place ship on the battlefield
    placeShip(ship);

    // Update counters to get next ship and player
    if (*count_active_player % 2 == 0) {
        *count_active_ships_1 += 1;
    } else {
        *count_active_ships_2 += 1;
    }

    *count_active_player += 1;
    if (*count_active_player >= *players_count) {
        *count_active_player = 0;
    }

    // Send another TEV_JALIEK package after this one
    pkgTEV_JALIEK(socket);
}

// Preapare and send TEV_JAIET package to force on of players to make a move with a given ship
void pkgTEV_JAIET(int socket)
{
    // Get next player to make him active
    struct Player* player = getNextPlayer(*count_active_player);
    if (player == NULL) {
        // Try to get another player
        *count_active_player += 1;
        if (*count_active_player >= *players_count) {
            *count_active_player = 0;
        }
        return;
    }

    uint32_t content_size = 6;
    uint8_t msg[content_size];

    // Get next ship to make move with
    struct Ship* ship = getNextShip(player->team_id);
    // If there is no ship for the current team, then oposite team wins
    if (ship == NULL) {
        msg[0] = 0;
        msg[1] = 0;
        msg[2] = 0;
        msg[3] = 0;
        msg[4] = 0;
        msg[5] = 0;

        // Send last package of this type to update last active player move
        *last_package_npk += 1;
        uint8_t* pTEV_JAIET = preparePackage(*last_package_npk, 10, msg, &content_size, content_size, *is_little_endian);
        write(socket, pTEV_JAIET, content_size);

        // Change game state to the last one
        *game_state = 5;
        *winner_team = (player->team_id == 1) ? 2 : 1;
        return;
    }

    msg[0] = player->id;
    msg[1] = ship->type;
    msg[2] = ship->x;
    msg[3] = ship->y;
    msg[4] = ship->dir;
    msg[5] = ship->damage;
    player->active = 1;

    // Send player id and all needed information about the ship
    *last_package_npk += 1;
    uint8_t* pTEV_JAIET = preparePackage(*last_package_npk, 10, msg, &content_size, content_size, *is_little_endian);
    write(socket, pTEV_JAIET, content_size);
}

// Process package GAJIENS to handle player move on server
void pkgGAJIENS(uint8_t *msg, uint32_t content_size, int socket)
{
    uint8_t *content = getPackageContent(msg, content_size);

    // Find player that was active to make it inactive
    struct Player* player = findPlayerById(players, content[0]);
    if (player->active == 0) {
        return;
    }

    player->active = 0;
    // Get ship that needs updates after player's move
    struct Ship* ship = getNextShip(content[0]);

    // There are two action types: movement and attack
    uint8_t action_type = content[1];
    uint8_t x = content[2];
    uint8_t y = content[3];
    // MOVEMENT
    if (action_type == 1) {
        // Clear the old ship
        clearShip(ship);
        // Updates coordinates of ship
        ship->x = x;
        ship->y = y;
        ship->dir = content[4];
        // Place ship with new coordinates
        placeShip(ship);
    // ATTACK
    } else if (action_type == 2) {
        // Check if an enemy of current ship was attacked
        uint8_t object_type = getBattlefieldObject(x, y);
        if (object_type >= 1 && object_type <= 5) {
            struct Ship* enemy_ship = getShipByCoord(x, y, ship->team_id);
            // Deal damage to an enemy
            if (enemy_ship != NULL) {
                placeObjectOnBattlefield((enum BattlefieldObj) Hit, x, y);
                dealDamage(enemy_ship, x, y);
            }
        // Miss
        } else {
            placeObjectOnBattlefield((enum BattlefieldObj) HitNot, x, y);
        }
    }

    // Update counters for next moves
    if (*count_active_player % 2 == 0) {
        *count_active_ships_1 += 1;
    } else {
        *count_active_ships_2 += 1;
    }

    *count_active_player += 1;
    if (*count_active_player >= *players_count) {
        *count_active_player = 0;
    }

    if (*count_active_ships_1 >= MAX_SHIPS / 2) {
        *count_active_ships_1 = 0;
    }
    if (*count_active_ships_2 >= MAX_SHIPS / 2) {
        *count_active_ships_2 = 0;
    }

    // Send another TEV_JAIET package after this one
    pkgTEV_JAIET(socket);
}
