#ifndef UTILS_H
#define UTILS_H

#define PORT 12345
#define MAX_PACKAGE_SIZE 1024 * 140
#define SHARED_MEMORY_SIZE 1024 * 1024
#define MAX_PLAYER_NAME_LEN 32
#define MAX_PLAYERS 10
#define MAX_SHIPS 10
#define BATTLEFIELD_X_MAX 255
#define BATTLEFIELD_Y_MAX 255

struct Ship {
    uint8_t type;
    uint8_t x;
    uint8_t y;
    uint8_t dir;
    uint8_t team_id;
    uint8_t damage;
};

struct Player {
    uint8_t id;
    unsigned char team_id;
    unsigned char is_ready;
    unsigned char name_len;
    char name[MAX_PLAYER_NAME_LEN];
    uint8_t active;
};

enum BattlefieldObj {
    Island = 10,
    Rocks = 11,
    Fish = 12,
    HitNot = 13,
    Hit = 14,
    MinePowerUp = 20,
    Mine = 21,
    RocketPowerUp = 22,
    Rocket = 23
};

/////////////////////////////////////// TEMP ///////////////////////////////////

void printArray(uint8_t *array, uint32_t size);

////////////////////////////////////////////////////////////////////////////////

char isLittleEndianSystem();
struct Player* findPlayerById(struct Player* players, uint8_t id);
struct Ship* findShipByIdAndTeamId(struct Ship* ships, uint8_t type, uint8_t team_id);
void getShipData(uint8_t type, uint8_t *speed, uint16_t *range, uint8_t *is_dir);

uint8_t* preparePackage(uint32_t npk, uint8_t type, uint8_t *content, uint32_t *content_size, uint32_t content_max_size, char is_little_endian);
void escapePackage(uint8_t *msg, uint32_t *msg_size);
char removePackageSeparator(uint8_t *msg, uint32_t *msg_size);
void unescapePackage(uint8_t *msg, uint32_t *msg_size);
char unpackPackage(uint8_t *msg, uint32_t msg_size, uint32_t npk, char is_little_endian);

uint32_t getPackageNPK(uint8_t *msg, char is_little_endian);
uint8_t getPackageType(uint8_t *msg);
uint32_t getPackageContentSize(uint8_t *msg, char is_little_endian);
uint8_t* getPackageContent(uint8_t *msg, uint32_t content_size);
uint8_t calculatePackageChecksum(uint8_t *msg, size_t msg_size);
uint8_t getPackageChecksum(uint8_t *msg, size_t msg_size);

#endif
