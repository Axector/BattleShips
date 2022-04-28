#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "utils.h"

char isLittleEndianSystem()
{
    volatile uint32_t i = 0x01234567;
    return (*((uint8_t*)(&i))) == 0x67;
}

char* preaparePackage(uint32_t npk, uint8_t type, char *content, uint32_t *content_size, uint32_t content_max_size, char is_little_endian)
{
    struct CurrentPackage {
        uint16_t separator;
        uint32_t npk;
        uint32_t size;
        uint8_t type;
        char content[content_max_size];
        uint8_t checksum;
        uint16_t separator_end;
    };

    char *message = malloc(MAX_PACKAGE_SIZE);
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
        sizeof(char) * (content_max_size) +
        sizeof(uint8_t) +
        sizeof(uint16_t)
    );

    msg->checksum = calculatePackageChecksum(message, msg_size - 2);

    escapePackage(message, &msg_size);

    char *package = malloc(msg_size);
    for (int i = 0; i < msg_size; i++) {
        package[i] = message[i];
    }
    free(message);

    *content_size = msg_size;
    return package;
}

void escapePackage(char *msg, uint32_t *msg_size)
{
    uint32_t msg_len = *msg_size;
    char escaped_msg[msg_len * 2];
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

char removePackageSeparator(char *msg, uint32_t *msg_size)
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

void unescapePackage(char *msg, uint32_t *msg_size)
{
    uint32_t msg_len = *msg_size;
    char unescaped[msg_len];
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

uint32_t getPackageNPK(char *msg, char is_little_endian)
{
    uint32_t res = *((uint32_t*) msg);
    return (is_little_endian) ? ntohl(res) : res;
}

uint8_t getPackageType(char *msg)
{
    return *((uint8_t*) (msg + sizeof(uint32_t) * 2));
}

uint32_t getPackageContentSize(char *msg, char is_little_endian)
{
    uint32_t res = *((uint32_t*) (msg + sizeof(uint32_t)));
    return (is_little_endian) ? ntohl(res) : res;
}

uint8_t calculatePackageChecksum(char *msg, size_t msg_size)
{
    uint8_t checksum = 0;
    for (int i = 0; i < msg_size - 1; i++) {
        checksum ^= msg[i];
    }

    return checksum;
}

uint8_t getPackageChecksum(char *msg, size_t msg_size)
{
    return (uint8_t) msg[msg_size - 1];
}
