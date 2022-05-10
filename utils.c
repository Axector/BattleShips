#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "utils.h"

//////////////////////////////////// Utils /////////////////////////////////////

// Check if system is little-endian
char isLittleEndianSystem()
{
    volatile uint32_t i = 0x01234567;
    return (*((uint8_t*)(&i))) == 0x67;
}

// Find player by its id from an array of all players
struct Player* findPlayerById(struct Player* players, uint8_t id)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].id == id) {
            return &players[i];
        }
    }
    return NULL;
}

// Find ship by its id and team id from an array of all ships
struct Ship* findShipByIdAndTeamId(struct Ship* ships, uint8_t type, uint8_t team_id)
{
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (ships[i].type == type && ships[i].team_id == team_id) {
            return &ships[i];
        }
    }
    return NULL;
}

// Get predefined ship statistics by its id
void getShipData(uint8_t type, uint8_t *speed, uint16_t *range, uint8_t *is_dir)
{
    if (type == 1) {
        *speed = 0;
        *range = 0;
        *is_dir = 0;
    }
    else if (type == 2) {
        *speed = 1;
        *range = 25;
        *is_dir = 0;
    }
    else if (type == 3) {
        *speed = 3;
        *range = 0;
        *is_dir = 1;
    }
    else if (type == 4) {
        *speed = 2;
        *range = 10;
        *is_dir = 0;
    }
    else if (type == 5) {
        *speed = 5;
        *range = 0;
        *is_dir = 0;
    }
}

////////////////////// Package preparation and unpacking ///////////////////////

// Add npk, package size, type, checksum to the package, apply escaping on it and add separators
uint8_t* preparePackage(uint32_t npk, uint8_t type, uint8_t *content, uint32_t *content_size, uint32_t content_max_size, char is_little_endian)
{
    // Default package structure
    struct CurrentPackage {
        uint16_t separator;
        uint32_t npk;
        uint32_t size;
        uint8_t type;
        uint8_t content[content_max_size];
        uint8_t checksum;
        uint16_t separator_end;
    };

    // Get memory for new package and fill it with needed data
    uint8_t *message = malloc(MAX_PACKAGE_SIZE);
    memset(message, 0, MAX_PACKAGE_SIZE);
    struct CurrentPackage* msg = (struct CurrentPackage*) (message - 2);
    msg->separator = 0;
    // If system is little-endian we should change numbers to be saved as in big-endian system
    msg->npk = (is_little_endian) ? htonl(npk) : npk;
    msg->size = (is_little_endian) ? htonl(*content_size) : *content_size;
    msg->type = type;
    for (int i = 0; i < *content_size; i++) {
        msg->content[i] = content[i];
    }
    msg->checksum = 0;
    msg->separator_end = 0;

    // Calculate message size
    uint32_t msg_size = (
        sizeof(uint16_t) +
        sizeof(uint32_t) +
        sizeof(uint32_t) +
        sizeof(uint8_t) +
        sizeof(uint8_t) * (content_max_size) +
        sizeof(uint8_t) +
        sizeof(uint16_t)
    );

    // Calculate checksum
    msg->checksum = calculatePackageChecksum(message, msg_size - 2);

    // Apply escaping on package, 0 = 1 1, 1 = 1 2
    escapePackage(message, &msg_size);

    // Create new package with correct size and free the old one
    uint8_t *package = malloc(msg_size);
    for (int i = 0; i < msg_size; i++) {
        package[i] = message[i];
    }
    free(message);
    *content_size = msg_size;
    return package;
}

// Apply escaping on package
void escapePackage(uint8_t *msg, uint32_t *msg_size)
{
    uint32_t msg_len = *msg_size;
    uint8_t escaped_msg[msg_len * 2];
    int escaped_msg_size = 0;

    // For each 0 we add 1 1, but for each 1 we add 1 2
    for (int i = 0; i < msg_len; i++) {
        if (i >= 2 && i < msg_len - 2) {
            if (msg[i] == 0) {
                escaped_msg[escaped_msg_size] = 1;
                escaped_msg[escaped_msg_size + 1] = 1;
                escaped_msg_size += 2;
                continue;
            }
            else if (msg[i] == 1) {
                escaped_msg[escaped_msg_size] = 1;
                escaped_msg[escaped_msg_size + 1] = 2;
                escaped_msg_size += 2;
                continue;
            }
        }

        // Do not change other bytes
        escaped_msg[escaped_msg_size++] = msg[i];
    }

    *msg_size = escaped_msg_size;
    for (int i = 0; i < escaped_msg_size; i++) {
        msg[i] = escaped_msg[i];
    }
}

// remove separator while unpacking
char removePackageSeparator(uint8_t *msg, uint32_t *msg_size)
{
    uint32_t msg_len = *msg_size;

    // If separator is incorrect pass the package
    if (
        msg[0] != 0 ||
        msg[1] != 0 ||
        msg[msg_len - 1] != 0 ||
        msg[msg_len - 2] != 0
    ) {
        return -1;
    }

    // The correct separators are 4 zeros
    msg_len -= 4;

    *msg_size = msg_len;
    for (int i = 0; i < msg_len; i++) {
        msg[i] = msg[i + 2];
    }

    return 0;
}

// Unescape package
void unescapePackage(uint8_t *msg, uint32_t *msg_size)
{
    uint32_t msg_len = *msg_size;
    uint8_t unescaped[msg_len];
    int unescaped_len = 0;

    // Every 1 1 combination replace with 0 and every 1 2 combination replace with 1
    for (int i = 0; i < msg_len; i++) {
        if (i + 1 < msg_len) {
            if ((msg[i] == 1 && msg[i + 1] == 1)) {
                i++;
                unescaped[unescaped_len++] = 0;
                continue;
            }
            else if ((msg[i] == 1 && msg[i + 1] == 2)) {
                i++;
                unescaped[unescaped_len++] = 1;
                continue;
            }
        }

        // Do not change other bytes
        unescaped[unescaped_len++] = msg[i];
    }

    *msg_size = unescaped_len;
    for (int i = 0; i < unescaped_len; i++) {
        msg[i] = unescaped[i];
    }
}

// Remove separators, unescape package, check checksum and npk
char unpackPackage(uint8_t *msg, uint32_t msg_size, uint32_t npk, char is_little_endian)
{
    if (removePackageSeparator(msg, &msg_size) == -1) {
        return -1;
    }

    unescapePackage(msg, &msg_size);

    if (getPackageChecksum(msg, msg_size) != calculatePackageChecksum(msg, msg_size)) {
        return -1;
    }

    uint32_t package_npk = getPackageNPK(msg, is_little_endian);
    if (package_npk != 0 && package_npk <= npk) {
        return -1;
    }

    return 0;
}

/////////////////////////////////////// Package INFO ///////////////////////////////////////

// Get npk of package
uint32_t getPackageNPK(uint8_t *msg, char is_little_endian)
{
    uint32_t res = *((uint32_t*) msg);
    return (is_little_endian) ? ntohl(res) : res;
}

// Get package type
uint8_t getPackageType(uint8_t *msg)
{
    return *((uint8_t*) (msg + sizeof(uint32_t) * 2));
}

// Get package content size
uint32_t getPackageContentSize(uint8_t *msg, char is_little_endian)
{
    uint32_t res = *((uint32_t*) (msg + sizeof(uint32_t)));
    return (is_little_endian) ? ntohl(res) : res;
}

// Get package contents
uint8_t* getPackageContent(uint8_t *msg, uint32_t content_size)
{
    int msg_beginning = sizeof(uint32_t) * 2 + sizeof(uint8_t);
    for (int i = 0; i < content_size; i++) {
        msg[i] = msg[i + msg_beginning];
    }
    return msg;
}

// Calculate package checksum
uint8_t calculatePackageChecksum(uint8_t *msg, size_t msg_size)
{
    uint8_t checksum = 0;
    for (int i = 0; i < msg_size - 1; i++) {
        checksum ^= msg[i];
    }

    return checksum;
}

// Get package checksum
uint8_t getPackageChecksum(uint8_t *msg, size_t msg_size)
{
    return msg[msg_size - 1];
}
