#ifndef UTILS_H
#define UTILS_H

#define PORT 12345
#define MAX_PACKAGE_SIZE 1024 * 100
#define SHARED_MEMORY_SIZE 1024 * 1024
#define MAX_PLAYER_NAME_LEN 32
#define MAX_PLAYERS 10

// Structure to store player info
struct Player {
    unsigned char id;
    unsigned char team_id;
    unsigned char is_ready;
    unsigned char name_len;
    char name[MAX_PLAYER_NAME_LEN];
};

/////////////////////////////////////// TEMP ///////////////////////////////////

void printArray(char *array, uint32_t size);

////////////////////////////////////////////////////////////////////////////////

char isLittleEndianSystem();

char* preaparePackage(uint32_t npk, uint8_t type, char *content, uint32_t *content_size, uint32_t content_max_size, char is_little_endian);
void escapePackage(char *msg, uint32_t *msg_size);
char removePackageSeparator(char *msg, uint32_t *msg_size);
void unescapePackage(char *msg, uint32_t *msg_size);

uint32_t getPackageNPK(char *msg, char is_little_endian);
uint8_t getPackageType(char *msg);
uint32_t getPackageContentSize(char *msg, char is_little_endian);
char* getPackageContent(char *msg, uint32_t content_size);
uint8_t calculatePackageChecksum(char *msg, size_t msg_size);
uint8_t getPackageChecksum(char *msg, size_t msg_size);

#endif