#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>

#define MAX_PLAYERS 10
#define PORT 12345
#define SHARED_MEMORY_SIZE 1024 * 1024
#define MAX_PLAYER_NAME_LEN 32
#define SERVER_DELTA_TIME 200 // Milliseconds

// Structure to store player info
struct Player {
    unsigned char id;
    unsigned char team_id;
    unsigned char is_ready;
    unsigned char name_len;
    char name[MAX_PLAYER_NAME_LEN];
};

char *shared_memory = NULL;             // Stores whole shared data
unsigned int *game_time = NULL;         // Time passed on server
char *to_exit = NULL;                   // Check if server must be stopped
unsigned char *player_count = NULL;     // Current number of players
unsigned char *player_next_id = NULL;   // Next player unique ID
char *next_team_id = NULL;              // team ID for the next player
unsigned char *game_state = NULL;       // Current game state
struct Player *players = NULL;          // Stores all players data

void get_shared_memory();
void gameloop();
void start_network();
void process_client(int id, int socket);
int addPlayer(char *name);
struct Player* findPlayerById(int id);
char readPackage(int socket);
char removeSeparator(char *msg, ssize_t *msg_size);
void unescapeMsg(char *msg, ssize_t *msg_size);
uint8_t getPackageType(char *msg);
uint16_t getPackageContentSize(char *msg);
uint32_t getPackageNPK(char *msg);

// Packages
void pkgLabdien(char *msg);

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
    game_time = (unsigned int*) shared_memory;
    is_little_endian = (char*) (shared_memory + sizeof(int));
    *is_little_endian = is_little_endian_system();
    to_exit = (char*) (shared_memory + sizeof(int) + sizeof(char));
    player_count = (unsigned char*) (shared_memory + sizeof(int) + sizeof(char) * 2);
    player_next_id = (unsigned char*) (shared_memory + sizeof(int) + sizeof(char) * 3);
    *player_next_id = 1;
    next_team_id = (char*) (shared_memory + sizeof(int) + sizeof(char) * 4);
    *next_team_id = 1;
    game_state = (unsigned char*) (shared_memory + sizeof(int) + sizeof(char) * 5);
    players = (struct Player*) (shared_memory + sizeof(int) + sizeof(char) * 6);
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
        *game_time = (unsigned int) time;
        usleep(SERVER_DELTA_TIME * 1000);
    }
}

void start_network()
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

        // Get name from player
        if(readPackage(client_socket) == -1) {
            continue;
        }

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

void process_client(int id, int socket)
{
    printf("%s connected. ID=%d, SOCKET=%d\n", findPlayerById(id)->name, id, socket);

    char in[100];
    char out[100];

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
int addPlayer(char *name)
{
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
    size_t size = 1024;
    char input[size];
    ssize_t nread = read(socket, input, size);

    if (nread == -1 || nread == 0) {
        return -1;
    }

    if (removeSeparator(input, &nread) == -1) {
        return -1;
    }

    unescapeMsg(input, &nread);
    uint8_t package_type = getPackageType(input);

    switch (package_type) {
        case 0:
            pkgLabdien(input);
            break;
    }

    return 0;
}

char removeSeparator(char *msg, ssize_t *msg_size)
{
    int msg_len = *msg_size;

    if (
        msg[0] != 0 ||
        msg[1] != 0 ||
        msg[msg_len - 1] != 0 ||
        msg[msg_len - 2] != 0
    ) {
        return -1;
    }

    msg_len -= 4;
    char no_separator[msg_len];

    for (int i = 0; i < msg_len; i++) {
        no_separator[i] = msg[i + 2];
    }

    *msg_size = msg_len;
    for (int i = 0; i < msg_len; i++) {
        msg[i] = no_separator[i];
    }

    return 0;
}

void unescapeMsg(char *msg, ssize_t *msg_size)
{
    char unescaped[*msg_size];
    int msg_len = *msg_size;
    int unescaped_it = 0;

    for (int i = 0; i < msg_len; i++) {
        if (i + 1 < msg_len) {
            if ((msg[i] == 1 && msg[i + 1] == 1)) {
                i++;
                unescaped[unescaped_it++] = 0;
                continue;
            }
            else if ((msg[i] == 1 && msg[i + 1] == 2)) {
                i++;
                unescaped[unescaped_it++] = 1;
                continue;
            }
        }

        unescaped[unescaped_it++] = msg[i];
    }

    *msg_size = unescaped_it;
    for (int i = 0; i < unescaped_it; i++) {
        msg[i] = unescaped[i];
    }
}

uint8_t getPackageType(char *msg)
{
    return *((uint8_t*) (msg + sizeof(uint16_t) + sizeof(uint32_t)));
}

uint16_t getPackageContentSize(char *msg)
{
    uint16_t res = *((uint16_t*) (msg + sizeof(uint32_t)));
    return (*is_little_endian) ? ntohs(res) : res;
}

uint32_t getPackageNPK(char *msg)
{
    uint32_t res = *((uint32_t*) msg);
    return (*is_little_endian) ? ntohl(res) : res;
}

void pkgLabdien(char *msg)
{
    char *name = (char*) (msg + sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint32_t));
    addPlayer(name);
}
