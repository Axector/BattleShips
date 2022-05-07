#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "utils.h"

/////////////////////////////////////// TEMP ///////////////////////////////////

void printArray(uint8_t *array, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        printf("%x ", array[i]);
    }
    printf("\n");
}

//////////////////////////////////// Utils /////////////////////////////////////

char isLittleEndianSystem()
{
    volatile uint32_t i = 0x01234567;
    return (*((uint8_t*)(&i))) == 0x67;
}

struct Player* findPlayerById(struct Player* players, uint8_t id)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].id == id) {
            return &players[i];
        }
    }
    return NULL;
}

struct Ship* findShipByIdAndTeamId(struct Ship* ships, uint8_t type, uint8_t team_id)
{
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (ships[i].type == type && ships[i].team_id == team_id) {
            return &ships[i];
        }
    }
    return NULL;
}

void getShipData(uint8_t type, uint8_t *speed, uint8_t *range, uint8_t *is_dir)
{
    if (type == 1) {
        *speed = 0;
        *range = BATTLEFIELD_X_MAX;
        *is_dir = 0;
    }
    else if (type == 2) {
        *speed = 1;
        *range = 25;
        *is_dir = 0;
    }
    else if (type == 3) {
        *speed = 3;
        *range = BATTLEFIELD_X_MAX;
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

uint8_t* preparePackage(uint32_t npk, uint8_t type, uint8_t *content, uint32_t *content_size, uint32_t content_max_size, char is_little_endian)
{
    struct CurrentPackage {
        uint16_t separator;
        uint32_t npk;
        uint32_t size;
        uint8_t type;
        uint8_t content[content_max_size];
        uint8_t checksum;
        uint16_t separator_end;
    };

    uint8_t *message = malloc(MAX_PACKAGE_SIZE);
    memset(message, 0, MAX_PACKAGE_SIZE);
    struct CurrentPackage* msg = (struct CurrentPackage*) (message - 2);
    msg->separator = 0;
    msg->npk = (is_little_endian) ? htonl(npk) : npk;
    msg->size = (is_little_endian) ? htonl(*content_size) : *content_size;
    msg->type = type;
    for (int i = 0; i < *content_size; i++) {
        msg->content[i] = content[i];
    }
    msg->checksum = 0;
    msg->separator_end = 0;

    uint32_t msg_size = (
        sizeof(uint16_t) +
        sizeof(uint32_t) +
        sizeof(uint32_t) +
        sizeof(uint8_t) +
        sizeof(uint8_t) * (content_max_size) +
        sizeof(uint8_t) +
        sizeof(uint16_t)
    );

    msg->checksum = calculatePackageChecksum(message, msg_size - 2);

    escapePackage(message, &msg_size);

    printf("%d\n", msg_size);

    uint8_t *package = malloc(msg_size);
    for (int i = 0; i < msg_size; i++) {
        package[i] = message[i];
    }
    free(message);
    *content_size = msg_size;
    return package;
}

void escapePackage(uint8_t *msg, uint32_t *msg_size)
{
    uint32_t msg_len = *msg_size;
    uint8_t escaped_msg[msg_len * 2];
    int escaped_msg_size = 0;

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

        escaped_msg[escaped_msg_size++] = msg[i];
    }

    *msg_size = escaped_msg_size;
    for (int i = 0; i < escaped_msg_size; i++) {
        msg[i] = escaped_msg[i];
    }
}

char removePackageSeparator(uint8_t *msg, uint32_t *msg_size)
{
    uint32_t msg_len = *msg_size;

    if (
        msg[0] != 0 ||
        msg[1] != 0 ||
        msg[msg_len - 1] != 0 ||
        msg[msg_len - 2] != 0
    ) {
        return -1;
    }

    msg_len -= 4;

    *msg_size = msg_len;
    for (int i = 0; i < msg_len; i++) {
        msg[i] = msg[i + 2];
    }

    return 0;
}

void unescapePackage(uint8_t *msg, uint32_t *msg_size)
{
    uint32_t msg_len = *msg_size;
    uint8_t unescaped[msg_len];
    int unescaped_len = 0;

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

        unescaped[unescaped_len++] = msg[i];
    }

    *msg_size = unescaped_len;
    for (int i = 0; i < unescaped_len; i++) {
        msg[i] = unescaped[i];
    }
}

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

uint32_t getPackageNPK(uint8_t *msg, char is_little_endian)
{
    uint32_t res = *((uint32_t*) msg);
    return (is_little_endian) ? ntohl(res) : res;
}

uint8_t getPackageType(uint8_t *msg)
{
    return *((uint8_t*) (msg + sizeof(uint32_t) * 2));
}

uint32_t getPackageContentSize(uint8_t *msg, char is_little_endian)
{
    uint32_t res = *((uint32_t*) (msg + sizeof(uint32_t)));
    return (is_little_endian) ? ntohl(res) : res;
}

uint8_t* getPackageContent(uint8_t *msg, uint32_t content_size)
{
    int msg_beginning = sizeof(uint32_t) * 2 + sizeof(uint8_t);
    for (int i = 0; i < content_size; i++) {
        msg[i] = msg[i + msg_beginning];
    }
    return msg;
}

uint8_t calculatePackageChecksum(uint8_t *msg, size_t msg_size)
{
    uint8_t checksum = 0;
    for (int i = 0; i < msg_size - 1; i++) {
        checksum ^= msg[i];
    }

    return checksum;
}

uint8_t getPackageChecksum(uint8_t *msg, size_t msg_size)
{
    return msg[msg_size - 1];
}
