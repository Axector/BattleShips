#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <GL/glew.h>
#include <GL/freeglut.h>
#include <sys/mman.h>
#include <stdint.h>
#include "utils.h"

#define HOST "127.0.0.1" /* localhost */

struct ShipToPlace {
    uint8_t type;
    uint8_t x;
    uint8_t y;
    uint8_t dir;
    uint8_t team_id;
    uint8_t damage;
    uint8_t placed;
};

struct AttackPosition {
    uint8_t x;
    uint8_t y;
};

/// Variables
int quartal_map[4][4];

char *shared_memory = NULL;

int *server_socket = NULL;
uint32_t *last_package_npk = NULL;      // NPK for packages

char *to_exit = NULL;
char *need_redisplay = NULL;
char *is_little_endian = NULL;
char *to_get_ships = NULL;

uint8_t *this_ID = NULL;
uint8_t *this_teamID = NULL;
uint8_t *player_name_len = NULL;
uint8_t *player_name = NULL;

uint8_t *players_count = NULL;
struct Player *players = NULL;
struct Ship *ships = NULL;

uint8_t *menu_state = NULL;
uint8_t *plane = NULL;
struct ShipToPlace *ships_to_place = NULL;
uint8_t *battlefield_x = NULL;
uint8_t *battlefield_y = NULL;
uint8_t *battlefield = NULL;
uint8_t *game_state = NULL;
uint8_t *action_state = NULL;
struct Ship *current_ship = NULL;
uint8_t *current_ship_speed = NULL;
uint16_t *current_ship_range = NULL;
uint8_t *current_ship_is_dir = NULL;
struct AttackPosition *current_attack_position = NULL;
uint8_t *winner_team = NULL;

/// Functions in thit file
void getSharedMemory();
void gameloop();
int clientConnect();
void processPackage(uint8_t *msg);

void glMain(int argc, char *argv[]);
void display();
void resize(int width, int height);
void specialKeyboard(int key, int x, int y);
void keyboard(unsigned char key, int x, int y);
void closeFunc();
void printText(char *text, double x, double y);
void printNumber(uint8_t number, double x, double y);
void loadingScreen();
void lobby();
void printConnectedUsers();
void inputField();
void outlineGrid();
void filledCube(int x, int y, double r, double g, double b);
void lineLoop(float x1, float x2, float y1, float y2, float r, float g, float b);
void quartalMap();
void showWinner();
void printHUD();
void createPlane();

struct ShipToPlace* findShipToPlace(struct ShipToPlace* ships, uint8_t type, uint8_t team_id);
void addShipsToPlace();
void removeShipsToPlace();
char checkEnemyShip(uint8_t x, uint8_t y);
char isItPlacingShip(uint8_t x, uint8_t y);
char isItOurTeamShip(uint8_t x, uint8_t y);
void setPlaneToShip(struct ShipToPlace* ship_to_place);

// Package types
void pkgACK(uint8_t *msg, uint32_t content_size);                       // 1
void pkgLOBBY(uint8_t *msg, uint32_t content_size);                     // 3
void pkgSTART_SETUP(uint8_t *msg, uint32_t content_size);               // 5
void pkgSTATE(uint8_t *msg, uint32_t content_size);                     // 6
void pkgTEV_JALIEK(uint8_t *msg, uint32_t content_size);                // 7
void pkgES_LIEKU(uint8_t type, uint8_t x, uint8_t y, uint8_t dir);      // 8
void pkgSTART_GAME(uint8_t *msg, uint32_t content_size);                // 9
void pkgTEV_JAIET(uint8_t *msg, uint32_t content_size);                 // 10
void pkgGAJIENS(uint8_t action, uint8_t x, uint8_t y, uint8_t thing);   // 11
void pkgEND_GAME(uint8_t *msg, uint32_t content_size);                  // 12

int main(int argc, char *argv[])
{
    quartal_map[0][0] = 1;
    getSharedMemory();

    // Check if current system is little-endian
    *is_little_endian = isLittleEndianSystem();

    *server_socket = clientConnect();
    glMain(argc, argv);

    int pid = 0;
    pid = fork();
    if (pid != 0) {
        while (1) {
            glutMainLoopEvent();

            if (*need_redisplay == 1) {
                *need_redisplay = 0;
                glutPostRedisplay();
            }
        }
    }

    while(*game_state == 0) {
        if (*to_exit == 1) {
            exit(0);
        }
    }

    uint32_t content_size = *player_name_len;
    uint8_t* pLABDIEN = preparePackage(*last_package_npk, 0, player_name, &content_size, MAX_PLAYER_NAME_LEN, *is_little_endian);
    write(*server_socket, pLABDIEN, content_size);

    uint8_t input[MAX_PACKAGE_SIZE];
    uint32_t nread = read(*server_socket, input, MAX_PACKAGE_SIZE);
    if(unpackPackage(input, nread, *last_package_npk, *is_little_endian) == -1) {
        closeFunc();
    }
    processPackage(input);

    gameloop();
    exit(0);
}

void glMain(int argc, char *argv[])
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(1400, 800);
    glutInitWindowPosition(100, 100);
    glutCreateWindow("PROJECT");
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(specialKeyboard);
    glutCloseFunc(closeFunc);
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_CONTINUE_EXECUTION);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 10.0, 0.0, 10.0, -1.0, 1.0);
    glutDisplayFunc(display);
    glutReshapeFunc(resize);
}

void display()
{
    glClearColor(0.5, 0.5, 0.5, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    if(*game_state == 0){
        inputField();
    } else if (*game_state == 1 || *game_state == 3) {
        loadingScreen();
    } else if (*game_state == 2) {
        lobby();
    } else if (*game_state == 4 || *game_state == 6) {
        addShipsToPlace();
        createPlane();
    } else if (*game_state == 8) {
        showWinner();
    }
    glutSwapBuffers();
}

void resize(int width, int height)
{
    glutReshapeWindow(1400, 800);
    //glutPositionWindow(100, 100);
}

void specialKeyboard(int key, int x, int y)
{
    if((*game_state == 4 || *game_state == 6) && *menu_state == 1){
        if (key == GLUT_KEY_UP) {
            if (*plane - 4 < 0) {
                return;
            }

            *plane -= 4;
        }
        if (key == GLUT_KEY_DOWN) {
            if (*plane + 4 >= 16) {
                return;
            }

            *plane += 4;
        }
        if (key == GLUT_KEY_LEFT) {
            if (
                (*plane % 4 - 1 < 0) ||
                (*game_state == 4 && *this_teamID == 1 && *plane % 4 - 1 < 0) ||
                (*game_state == 4 && *this_teamID == 2 && *plane % 4 - 1 < 2)
            ) {
                return;
            }

            *plane -= 1;
        }
        if (key == GLUT_KEY_RIGHT) {
            if (
                (*plane % 4 + 1 >= 4) ||
                (*game_state == 4 && *this_teamID == 1 && *plane % 4 + 1 >= 2) ||
                (*game_state == 4 && *this_teamID == 2 && *plane % 4 + 1 >= 4)
            ) {
                return;
            }

            *plane += 1;
        }
    } else if (*game_state == 4 && *menu_state == 0) {
        struct ShipToPlace* ship = findShipToPlace(ships_to_place, current_ship->type, *this_teamID);
        if (ship == NULL) {
            return;
        }

        char dx = (ship->dir == 1) ? 1 : (ship->dir == 3) ? -1 : 0;
        char dy = (ship->dir == 0) ? -1 : (ship->dir == 2) ? 1 : 0;

        removeShipsToPlace();
        if (key == GLUT_KEY_UP) {
            for (int i = 0; i < 6 - ship->type; i++) {
                uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy) - 1) * (*battlefield_x)];
                if (object_type >= 10 && object_type <= 19) {
                    return;
                }
            }

            if ((ship->y - ((ship->dir == 0) ? 6 - ship->type - 1 : 0)) <= 0 + 1) {
                return;
            }

            if (*plane / 4 > 0 && ship->y <= (*plane / 4) * PLANE_SIZE ) {
                *plane -= 4;
            }

            ship->y -= 1;
        } else if (key == GLUT_KEY_DOWN) {
            for (int i = 0; i < 6 - ship->type; i++) {
                uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy) + 1) * (*battlefield_x)];
                if (object_type >= 10 && object_type <= 19) {
                    return;
                }
            }

            if ((ship->y + ((ship->dir == 2) ? 6 - ship->type - 1 : 0)) >= *battlefield_y - 1) {
                return;
            }

            if (*plane / 4 < 4 && ship->y >= ((*plane / 4) + 1) * PLANE_SIZE - 1) {
                *plane += 4;
            }

            ship->y += 1;
        } else if (key == GLUT_KEY_LEFT) {
            for (int i = 0; i < 6 - ship->type; i++) {
                uint8_t object_type = battlefield[((ship->x + i * dx) - 1) + (ship->y + i * dy) * (*battlefield_x)];
                if (object_type >= 10 && object_type <= 19) {
                    return;
                }
            }

            if (
                ((*this_teamID == 1 && (ship->x - ((ship->dir == 3) ? 6 - ship->type - 1 : 0)) <= 0 + 1) ||
                (*this_teamID == 2 && (ship->x - ((ship->dir == 3) ? 6 - ship->type - 1 : 0)) <= (*battlefield_x / 2) + 1))
            ) {
                return;
            }

            if (*plane % 4 > 0 && ship->x <= (*plane % 4) * PLANE_SIZE) {
                *plane -= 1;
            }

            ship->x -= 1;
        } else if (key == GLUT_KEY_RIGHT) {
            for (int i = 0; i < 6 - ship->type; i++) {
                uint8_t object_type = battlefield[((ship->x + i * dx) + 1) + (ship->y + i * dy) * (*battlefield_x)];
                if (object_type >= 10 && object_type <= 19) {
                    return;
                }
            }

            if (
                ((*this_teamID == 1 && (ship->x + ((ship->dir == 1) ? 6 - ship->type - 1 : 0)) >= (*battlefield_x / 2)) ||
                (*this_teamID == 2 && (ship->x + ((ship->dir == 1) ? 6 - ship->type - 1 : 0)) >= *battlefield_x - 1))
            ) {
                return;
            }

            if (*plane % 4 < 4 && ship->x >= ((*plane % 4) + 1) * PLANE_SIZE - 1) {
                *plane += 1;
            }

            ship->x += 1;
        }
    } else if (*game_state == 6 && *menu_state == 0) {
        struct ShipToPlace* ship = findShipToPlace(ships_to_place, current_ship->type, *this_teamID);
        if(ship == NULL){
            return;
        }

        char dx = (ship->dir == 1) ? 1 : (ship->dir == 3) ? -1 : 0;
        char dy = (ship->dir == 0) ? -1 : (ship->dir == 2) ? 1 : 0;

        if (*action_state == 0) {
            removeShipsToPlace();
            if (key == GLUT_KEY_UP) {
                for (int i = 0; i < 6 - ship->type; i++) {
                    uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy) - 1) * (*battlefield_x)];
                    if (object_type >= 10 && object_type <= 19) {
                        return;
                    }
                }

                if ((ship->y - ((ship->dir == 0) ? 6 - ship->type - 1 : 0)) <= 0 + 1) {
                    return;
                }

                if (current_ship->y >= ship->y) {
                    if (*current_ship_speed > 0) {
                        if (*plane / 4 > 0 && ship->y <= (*plane / 4) * PLANE_SIZE) {
                            *plane -= 4;
                        }
                        ship->y -= 1;
                        *current_ship_speed -= 1;
                    }
                } else if (current_ship->y <= ship->y) {
                    if (*plane / 4 > 0 && ship->y <= (*plane / 4) * PLANE_SIZE) {
                        *plane -= 4;
                    }
                    ship->y -= 1;
                    *current_ship_speed += 1;
                }
            } else if (key == GLUT_KEY_DOWN) {
                for (int i = 0; i < 6 - ship->type; i++) {
                    uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy) + 1) * (*battlefield_x)];
                    if (object_type >= 10 && object_type <= 19) {
                        return;
                    }
                }

                if ((ship->y + ((ship->dir == 2) ? 6 - ship->type - 1 : 0)) >= *battlefield_y - 1) {
                    return;
                }

                if (current_ship->y <= ship->y) {
                    if (*current_ship_speed > 0) {
                        if (*plane / 4 < 4 && ship->y >= ((*plane / 4) + 1) * PLANE_SIZE - 1) {
                            *plane += 4;
                        }
                        ship->y += 1;
                        *current_ship_speed -= 1;
                    }
                } else if (current_ship->y >= ship->y) {
                    if (*plane / 4 < 4 && ship->y >= ((*plane / 4) + 1) * PLANE_SIZE - 1) {
                        *plane += 4;
                    }
                    ship->y += 1;
                    *current_ship_speed += 1;
                }
            } else if (key == GLUT_KEY_LEFT) {
                for (int i = 0; i < 6 - ship->type; i++) {
                    uint8_t object_type = battlefield[((ship->x + i * dx) - 1) + (ship->y + i * dy) * (*battlefield_x)];
                    if (object_type >= 10 && object_type <= 19) {
                        return;
                    }
                }

                if ((ship->x - ((ship->dir == 3) ? 6 - ship->type - 1 : 0)) <= 0 + 1) {
                    return;
                }

                if (current_ship->x >= ship->x) {
                    if (*current_ship_speed > 0) {
                        if (*plane % 4 > 0 && ship->x <= (*plane % 4) * PLANE_SIZE) {
                            *plane -= 1;
                        }
                        ship->x -= 1;
                        *current_ship_speed -= 1;
                    }
                } else if (current_ship->x <= ship->x) {
                    if (*plane % 4 > 0 && ship->x <= (*plane % 4) * PLANE_SIZE) {
                        *plane -= 1;
                    }
                    ship->x -= 1;
                    *current_ship_speed += 1;
                }
            } else if (key == GLUT_KEY_RIGHT) {
                for (int i = 0; i < 6 - ship->type; i++) {
                    uint8_t object_type = battlefield[((ship->x + i * dx) + 1) + (ship->y + i * dy) * (*battlefield_x)];
                    if (object_type >= 10 && object_type <= 19) {
                        return;
                    }
                }

                if ((ship->x + ((ship->dir == 1) ? 6 - ship->type - 1 : 0)) >= *battlefield_x - 1) {
                    return;
                }

                if (current_ship->x <= ship->x) {
                    if (*current_ship_speed > 0) {
                        if (*plane % 4 < 4 && ship->x >= ((*plane % 4) + 1) * PLANE_SIZE - 1) {
                            *plane += 1;
                        }
                        ship->x += 1;
                        *current_ship_speed -= 1;
                    }
                } else if (current_ship->x >= ship->x) {
                    if (*plane % 4 < 4 && ship->x >= ((*plane % 4) + 1) * PLANE_SIZE - 1) {
                        *plane += 1;
                    }
                    ship->x += 1;
                    *current_ship_speed += 1;
                }
            }
        } else if (*action_state == 1) {
            if (key == GLUT_KEY_UP) {
                if (current_ship->type == 3) {
                    current_attack_position->y = ship->y - ((ship->dir == 0) ? 6 - ship->type - 1 : 0) - 1;
                    current_attack_position->x = ship->x;
                    return;
                }

                if (current_attack_position->y <= 0 + 1) {
                    return;
                }

                if (current_ship->y >= current_attack_position->y) {
                    if (*current_ship_range > 0 || current_ship->type == 1) {
                        if (*plane / 4 > 0 && current_attack_position->y <= (*plane / 4) * PLANE_SIZE) {
                            *plane -= 4;
                        }
                        current_attack_position->y -= 1;
                        *current_ship_range -= 1;
                    }
                } else if (current_ship->y <= current_attack_position->y) {
                    if (*plane / 4 > 0 && current_attack_position->y <= (*plane / 4) * PLANE_SIZE) {
                        *plane -= 4;
                    }
                    current_attack_position->y -= 1;
                    *current_ship_range += 1;
                }
            }
            if (key == GLUT_KEY_DOWN) {
                if (current_ship->type == 3) {
                    current_attack_position->y = ship->y + ((ship->dir == 2) ? 6 - ship->type - 1 : 0) + 1;
                    current_attack_position->x = ship->x;
                    return;
                }

                if (current_attack_position->y >= *battlefield_y - 1) {
                    return;
                }

                if (current_ship->y <= current_attack_position->y) {
                    if (*current_ship_range > 0 || current_ship->type == 1) {
                        if (*plane / 4 < 4 && current_attack_position->y >= ((*plane / 4) + 1) * PLANE_SIZE - 1) {
                            *plane += 4;
                        }
                        current_attack_position->y += 1;
                        *current_ship_range -= 1;
                    }
                } else if (current_ship->y >= current_attack_position->y) {
                    if (*plane / 4 < 4 && current_attack_position->y >= ((*plane / 4) + 1) * PLANE_SIZE - 1) {
                        *plane += 4;
                    }
                    current_attack_position->y += 1;
                    *current_ship_range += 1;
                }
            }
            if (key == GLUT_KEY_LEFT) {
                if (current_ship->type == 3) {
                    current_attack_position->x = ship->x - ((ship->dir == 3) ? 6 - ship->type - 1 : 0) - 1;
                    current_attack_position->y = ship->y;
                    return;
                }

                if (current_attack_position->x <= 0 + 1) {
                    return;
                }

                if (current_ship->x >= current_attack_position->x) {
                    if (*current_ship_range > 0 || current_ship->type == 1) {
                        if (*plane % 4 > 0 && current_attack_position->x <= (*plane % 4) * PLANE_SIZE) {
                            *plane -= 1;
                        }
                        current_attack_position->x -= 1;
                        *current_ship_range -= 1;
                    }
                } else if (current_ship->x <= current_attack_position->x) {
                    if (*plane % 4 > 0 && current_attack_position->x <= (*plane % 4) * PLANE_SIZE) {
                        *plane -= 1;
                    }
                    current_attack_position->x -= 1;
                    *current_ship_range += 1;
                }
            }
            if (key == GLUT_KEY_RIGHT) {
                if (current_ship->type == 3) {
                    current_attack_position->x = ship->x + ((ship->dir == 1) ? 6 - ship->type - 1 : 0) + 1;
                    current_attack_position->y = ship->y;
                    return;
                }

                if (current_attack_position->x >= *battlefield_x - 1) {
                    return;
                }

                if (current_ship->x <= current_attack_position->x) {
                    if (*current_ship_range > 0 || current_ship->type == 1) {
                        if (*plane % 4 < 4 && current_attack_position->x >= ((*plane % 4) + 1) * PLANE_SIZE - 1) {
                            *plane += 1;
                        }
                        current_attack_position->x += 1;
                        *current_ship_range -= 1;
                    }
                } else if (current_ship->x >= current_attack_position->x) {
                    if (*plane % 4 < 4 && current_attack_position->x >= ((*plane % 4) + 1) * PLANE_SIZE - 1) {
                        *plane += 1;
                    }
                    current_attack_position->x += 1;
                    *current_ship_range += 1;
                }
            }
        }
    }

    *need_redisplay = 1;
}

void keyboard(unsigned char key, int x, int y)
{
    if(key == 27){
        exit(0);
    }
    if(*game_state == 0){
        if(key == 13){
            if (player_name[0] == 0) {
                return;
            }

            *game_state = 1;
        } else if(key == 8){
            if (*player_name_len > 0) {
                player_name[*player_name_len - 1] = 0;
                *player_name_len -= 1;
            }
        }else{
            if (
                *player_name_len < MAX_PLAYER_NAME_LEN &&
                ((key >= 'A' && key <= 'Z') ||
                (key >= 'a' && key <= 'z') ||
                (key >= '0' && key <= '9') ||
                (key == '_' || key == '-'))
            ) {
                player_name[*player_name_len] = tolower(key);
                *player_name_len += 1;
            }
        }
    } else if (*game_state == 2) {
        if(key == 'r') {
            uint8_t msg[2];
            msg[0] = *this_ID;
            msg[1] = (findPlayerById(players, *this_ID)->is_ready == 1) ? 0 : 1;
            uint32_t content_size = 2;
            *last_package_npk += 2;
            uint8_t* pENTER = preparePackage(*last_package_npk, 4, msg, &content_size, content_size, *is_little_endian);
            write(*server_socket, pENTER, content_size);
        }
    } else if(*game_state == 4) {
        struct ShipToPlace* ship = findShipToPlace(ships_to_place, current_ship->type, *this_teamID);
        if (ship == NULL) {
            return;
        }

        if (key == ',' && ship->type != 5) {
            uint8_t dir = (ship->dir == 0) ? 3 : ship->dir - 1;
            char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
            char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;

            for (int i = 0; i < 6 - ship->type; i++) {
                uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy)) * (*battlefield_x)];
                if (object_type >= 10 && object_type <= 19) {
                    return;
                }
            }

            if ((
                ship->dir == 0 &&
                ((*this_teamID == 1 && ship->x - (6 - ship->type - 1) <= 0) ||
                (*this_teamID == 2 && ship->x - (6 - ship->type - 1) <= (*battlefield_x / 2)))
            ) || (
                ship->dir == 1 &&
                ship->y - (6 - ship->type - 1) <= 0
            ) || (
                ship->dir == 2 &&
                ((*this_teamID == 1 && ship->x + (6 - ship->type - 1) > (*battlefield_x / 2)) ||
                (*this_teamID == 2 && ship->x + (6 - ship->type - 1) > *battlefield_x - 1))
            ) || (
                ship->dir == 3 &&
                ship->y + (6 - ship->type - 1) > *battlefield_y - 1
            )) {
                return;
            }

            removeShipsToPlace();
            ship->dir = (ship->dir == 0) ? 3 : ship->dir - 1;
        } else if(key == '.' && ship->type != 5) {
            uint8_t dir = (ship->dir == 3) ? 0 : ship->dir + 1;
            char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
            char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;

            for (int i = 0; i < 6 - ship->type; i++) {
                uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy)) * (*battlefield_x)];
                if (object_type >= 10 && object_type <= 19) {
                    return;
                }
            }

            if ((
                ship->dir == 2 &&
                ((*this_teamID == 1 && ship->x - (6 - ship->type - 1) <= 0) ||
                (*this_teamID == 2 && ship->x - (6 - ship->type - 1) <= (*battlefield_x / 2)))
            ) || (
                ship->dir == 3 &&
                ship->y - (6 - ship->type - 1) <= 0
            ) || (
                ship->dir == 0 &&
                ((*this_teamID == 1 && ship->x + (6 - ship->type - 1) > (*battlefield_x / 2)) ||
                (*this_teamID == 2 && ship->x + (6 - ship->type - 1) > *battlefield_x - 1))
            ) || (
                ship->dir == 1 &&
                ship->y + (6 - ship->type - 1) > *battlefield_y - 1
            )) {
                return;
            }

            removeShipsToPlace();
            ship->dir = (ship->dir == 3) ? 0 : ship->dir + 1;
        } else if(key == 13) {
            char dx = (ship->dir == 1) ? 1 : (ship->dir == 3) ? -1 : 0;
            char dy = (ship->dir == 0) ? -1 : (ship->dir == 2) ? 1 : 0;

            for (int i = 0; i < 6 - ship->type; i++) {
                if (isItOurTeamShip((ship->x + i * dx), (ship->y + i * dy)) == 1) {
                    return;
                }
            }
            pkgES_LIEKU(ship->type, ship->x, ship->y, ship->dir);
        }
    } else if (*game_state == 6) {
        struct ShipToPlace* ship = findShipToPlace(ships_to_place, current_ship->type, *this_teamID);
        if (ship == NULL) {
            return;
        }

        if (key == 13) {
            // MOVEMENT
            if (*action_state == 0) {
                char dx = (ship->dir == 1) ? 1 : (ship->dir == 3) ? -1 : 0;
                char dy = (ship->dir == 0) ? -1 : (ship->dir == 2) ? 1 : 0;

                for (int i = 0; i < 6 - ship->type; i++) {
                    if (isItOurTeamShip((ship->x + i * dx), (ship->y + i * dy)) == 1) {
                        return;
                    }
                }
                pkgGAJIENS(1, ship->x, ship->y, ship->dir);
            }
            // ATTACK
            else if (*action_state == 1) {
                uint8_t current_x = current_attack_position->x;
                uint8_t current_y = current_attack_position->y;
                current_attack_position->x = 0;
                current_attack_position->y = 0;

                if (ship->type == 3) {
                    if (current_x < current_ship->x) {
                        while (current_x >= 0 + 1) {
                            uint8_t battlefield_object = battlefield[current_x + current_y * BATTLEFIELD_X_MAX];
                            if (battlefield_object > 0 && battlefield_object <= 5) {
                                char is_our = isItOurTeamShip(current_x, current_y);
                                if (is_our == 0 || is_our == 2) {
                                    break;
                                }
                            }
                            if (battlefield_object > 5) {
                                break;
                            }
                            current_x -= 1;
                        }
                    } else if (current_x > current_ship->x) {
                        while (current_x < *battlefield_x) {
                            uint8_t battlefield_object = battlefield[current_x + current_y * BATTLEFIELD_X_MAX];
                            if (battlefield_object > 0 && battlefield_object <= 5) {
                                char is_our = isItOurTeamShip(current_x, current_y);
                                if (is_our == 0 || is_our == 2) {
                                    break;
                                }
                            }
                            if (battlefield_object > 5) {
                                break;
                            }
                            current_x += 1;
                        }
                    } else if (current_y < current_ship->y) {
                        while (current_y >= 0 + 1) {
                            uint8_t battlefield_object = battlefield[current_x + current_y * BATTLEFIELD_X_MAX];
                            if (battlefield_object > 0 && battlefield_object <= 5) {
                                char is_our = isItOurTeamShip(current_x, current_y);
                                if (is_our == 0 || is_our == 2) {
                                    break;
                                }
                            }
                            if (battlefield_object > 5) {
                                break;
                            }
                            current_y -= 1;
                        }
                    } else if (current_y > current_ship->y) {
                        while (current_y <= *battlefield_y - 1) {
                            uint8_t battlefield_object = battlefield[current_x + current_y * BATTLEFIELD_X_MAX];
                            if (battlefield_object > 0 && battlefield_object <= 5) {
                                char is_our = isItOurTeamShip(current_x, current_y);
                                if (is_our == 0 || is_our == 2) {
                                    break;
                                }
                            }
                            if (battlefield_object > 5) {
                                break;
                            }
                            current_y += 1;
                        }
                    }
                }

                uint8_t object_type = battlefield[current_x + current_y * (*battlefield_x)];
                if (
                    isItOurTeamShip(current_x, current_y) == 1 ||
                    (object_type >= 10 && object_type <= 19)
                ) {
                    if (ship->type == 3) {
                        if (current_x < ship->x) {
                            current_x += 1;
                        } else if (current_x > ship->x) {
                            current_x -= 1;
                        } else if (current_y < ship->y) {
                            current_y += 1;
                        } else if (current_y > ship->y) {
                            current_y -= 1;
                        }
                    }
                    else {
                        return;
                    }
                }
                pkgGAJIENS(2, current_x, current_y, 0);
            }
        } else if (*action_state != 0 && current_ship->type != 1 && current_ship->damage <= 0 && key == '1') {
            *action_state = 0;
            setPlaneToShip(ship);
            current_attack_position->x = 0;
            current_attack_position->y = 0;
        } else if (*action_state != 1 && current_ship->type != 5 && key == '2') {
            *action_state = 1;
            ship->x = current_ship->x;
            ship->y = current_ship->y;
            ship->dir = current_ship->dir;
            getShipData(ship->type, current_ship_speed, current_ship_range, current_ship_is_dir);
            setPlaneToShip(ship);
            if (ship->type == 3) {
                current_attack_position->y = ship->y - ((ship->dir == 0) ? 6 - ship->type - 1 : 0) - 1;
                current_attack_position->x = ship->x;
            }
            else {
                current_attack_position->x = ship->x;
                current_attack_position->y = ship->y;
            }
        } else if (*action_state == 0) {
            if (key == ',' && ship->type != 5) {
                uint8_t dir = (ship->dir == 0) ? 3 : ship->dir - 1;
                char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
                char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;

                for (int i = 0; i < 6 - ship->type; i++) {
                    uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy)) * (*battlefield_x)];
                    if (object_type >= 10 && object_type <= 19) {
                        return;
                    }
                }

                if ((
                    ship->dir == 0 &&
                    ship->x - (6 - ship->type - 1) <= 0
                ) || (
                    ship->dir == 1 &&
                    ship->y - (6 - ship->type - 1) <= 0
                ) || (
                    ship->dir == 2 &&
                    ship->x + (6 - ship->type - 1) > *battlefield_x - 1
                ) || (
                    ship->dir == 3 &&
                    ship->y + (6 - ship->type - 1) > *battlefield_y - 1
                )) {
                    return;
                }

                if (current_ship->dir == 0) {
                    if (ship->dir == 0 || ship->dir == 3) {
                        if (*current_ship_speed <= 0) {
                            return;
                        }
                        *current_ship_speed -= 1;
                    } else if (ship->dir == 2 || ship->dir == 1) {
                        *current_ship_speed += 1;
                    }
                } else if (current_ship->dir == 1) {
                    if (ship->dir == 1 || ship->dir == 0) {
                        if (*current_ship_speed <= 0) {
                            return;
                        }
                        *current_ship_speed -= 1;
                    } else if (ship->dir == 3 || ship->dir == 2) {
                        *current_ship_speed += 1;
                    }
                } else if (current_ship->dir == 2) {
                    if (ship->dir == 2 || ship->dir == 1) {
                        if (*current_ship_speed <= 0) {
                            return;
                        }
                        *current_ship_speed -= 1;
                    } else if (ship->dir == 0 || ship->dir == 3) {
                        *current_ship_speed += 1;
                    }
                } else if (current_ship->dir == 3) {
                    if (ship->dir == 3 || ship->dir == 2) {
                        if (*current_ship_speed <= 0) {
                            return;
                        }
                        *current_ship_speed -= 1;
                    } else if (ship->dir == 1 || ship->dir == 0) {
                        *current_ship_speed += 1;
                    }
                }

                removeShipsToPlace();
                ship->dir = (ship->dir == 0) ? 3 : ship->dir - 1;
            } else if(key == '.' && ship->type != 5) {
                uint8_t dir = (ship->dir == 3) ? 0 : ship->dir + 1;
                char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
                char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;

                for (int i = 0; i < 6 - ship->type; i++) {
                    uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy)) * (*battlefield_x)];
                    if (object_type >= 10 && object_type <= 19) {
                        return;
                    }
                }

                if ((
                    ship->dir == 0 &&
                    ship->x + (6 - ship->type - 1) > *battlefield_x - 1
                ) || (
                    ship->dir == 1 &&
                    ship->y + (6 - ship->type - 1) > *battlefield_y - 1
                ) || (
                    ship->dir == 2 &&
                    ship->x - (6 - ship->type - 1) <= 0
                ) || (
                    ship->dir == 3 &&
                    ship->y - (6 - ship->type - 1) <= 0
                )) {
                    return;
                }

                if (current_ship->dir == 0) {
                    if (ship->dir == 0 || ship->dir == 1) {
                        if (*current_ship_speed <= 0) {
                            return;
                        }
                        *current_ship_speed -= 1;
                    } else if (ship->dir == 2 || ship->dir == 3) {
                        *current_ship_speed += 1;
                    }
                } else if (current_ship->dir == 1) {
                    if (ship->dir == 1 || ship->dir == 2) {
                        if (*current_ship_speed <= 0) {
                            return;
                        }
                        *current_ship_speed -= 1;
                    } else if (ship->dir == 3 || ship->dir == 0) {
                        *current_ship_speed += 1;
                    }
                } else if (current_ship->dir == 2) {
                    if (ship->dir == 2 || ship->dir == 3) {
                        if (*current_ship_speed <= 0) {
                            return;
                        }
                        *current_ship_speed -= 1;
                    } else if (ship->dir == 0 || ship->dir == 1) {
                        *current_ship_speed += 1;
                    }
                } else if (current_ship->dir == 3) {
                    if (ship->dir == 3 || ship->dir == 0) {
                        if (*current_ship_speed <= 0) {
                            return;
                        }
                        *current_ship_speed -= 1;
                    } else if (ship->dir == 1 || ship->dir == 2) {
                        *current_ship_speed += 1;
                    }
                }

                removeShipsToPlace();
                ship->dir = (ship->dir == 3) ? 0 : ship->dir + 1;
            }
        }
    }

    if (*game_state == 4 || *game_state == 6) {
        if(key == 9){
            *menu_state += 1;
            if(*menu_state >= 2){
                *menu_state = 0;
            }

            // TODO other menu states
        }
    }

    *need_redisplay = 1;
}

void closeFunc()
{
    *to_exit = 1;
    exit(0);
}

void printText(char *text, double x, double y)
{
    glRasterPos3f(x, y, 0);
    while(*text){
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, *text++);
    }
    glFlush();
}

void printNumber(uint8_t number, double x, double y)
{
    if (number < 0) {
        return;
    }

    glRasterPos3f(x, y, 0);
    if (number == 0) {
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, '0');
    }

    uint8_t size = 0;
    int new_number = 0;
    for (int i = number; i > 0; i /= 10) {
        size++;
        new_number *= 10;
        new_number += i % 10;
    }

    for (int i = 0; i < size; i++){
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, new_number % 10 + '0');
        new_number /= 10;
    }
}

void loadingScreen()
{
    glColor3f(0.0f, 0.0f, 0.0f);
    printText("Loading...", 8.0f, 2.0f);
}

void lobby()
{
    glColor3f(0.3f, 0.3f, 0.3f);
    printText("Press 'ESC' to exit", 0.2, 9.5);
    printText("press 'R' to Ready up", 4.2, 1.2);
    glColor3f(0.0f, 0.0f, 0.0f);
    glRasterPos3f(4.0, 9.0, 0);
    printText("Lobby", 4.75, 9.2);
    lineLoop(0.8, 9.2, 8.8, 1.7, 0, 0, 0);
    lineLoop(1, 5, 8.5, 2, 0, 0, 0);
    lineLoop(5, 9, 8.5, 2, 0, 0, 0);
    char *ready_state = "Ready";
    int team1 = 0;
    int team2 = 0;
    for(int i = 0; i < MAX_PLAYERS; i++){
        glColor3f(0.0f, 0.0f, 0.0f);
        if (players[i].id == 0) {
            continue;
        }
        if(players[i].team_id == 1){
            glRasterPos3f(1.1, 8.0-(0.3*team1), 0);
            for(int a = 0; a < strlen(players[i].name); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
            }

            if (players[i].is_ready == 1) {
                glColor3f(0.0f, 1.0f, 0.0f);
                ready_state = "Ready";
            } else {
                glColor3f(1.0f, 0.0f, 0.0f);
                ready_state = "Not Ready";
            }

            glRasterPos3f(4.2+(players[i].is_ready == 1 ? 0.3 : 0.0), 8.0-(0.3*team1), 0);
            for(int a = 0; a < strlen(ready_state); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, ready_state[a]);
            }
            glFlush();
            team1++;
        }
        else if(players[i].team_id == 2){
            glRasterPos3f(5.1, 8.0-(0.3*team2), 0);
            for(int a = 0; a < strlen(players[i].name); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
            }

            if (players[i].is_ready == 1) {
                glColor3f(0.0f, 1.0f, 0.0f);
                ready_state = "Ready";
            } else {
                glColor3f(1.0f, 0.0f, 0.0f);
                ready_state = "Not Ready";
            }

            glRasterPos3f(8.2+(players[i].is_ready == 1 ? 0.3 : 0.0), 8.0-(0.3*team2), 0);
            for(int a = 0; a < strlen(ready_state); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, ready_state[a]);
            }
            glFlush();
            team2++;
        }

    }
}

void printConnectedUsers()
{
    if (*this_teamID == 0) {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i].id == 0) {
                continue;
            }

            glColor3f(0.0f, 0.0f, 0.0f);
            glRasterPos3f(5.7, 8.3-(0.3*i), 0);
            for(int a = 0; a < strlen(players[i].name); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
            }
        }
        glFlush();
        return;
    }

    uint8_t player_iter = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].id == 0 || players[i].team_id != *this_teamID) {
            continue;
        }

        glColor3f(0.0f, 1.0f, 0.0f);
        glRasterPos3f(5.7, 8.3-(0.3*player_iter++), 0);
        for(int a = 0; a < strlen(players[i].name); a++){
            glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
        }
    }
    player_iter = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].id == 0 || players[i].team_id == *this_teamID) {
            continue;
        }

        glColor3f(1.0f, 0.0f, 0.0f);
        glRasterPos3f(5.7, 5.8-(0.3*player_iter++), 0);
        for(int a = 0; a < strlen(players[i].name); a++){
            glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
        }
    }
    glFlush();
}

void inputField()
{
    glColor3f(0.3f, 0.3f, 0.3f);
    printText("press 'ENTER' to submit", 4.2, 6);
    glColor3f(0.0f, 0.0f, 0.0f);
    printText("Enter your name", 4.5, 9);

    glColor3f(0.3f, 0.3f, 0.3f);
    printText("Press 'ESC' to exit", 0.2, 9.5);

    lineLoop(2, 8, 8.1, 7.1, 0, 0, 0);
    glRasterPos3f(5 - (glutBitmapLength(GLUT_BITMAP_TIMES_ROMAN_24, player_name) / 280.0), 7.5, 0);
    for(int i = 0; i < MAX_PLAYER_NAME_LEN || player_name[i] != '\0'; i++){
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, player_name[i]);
    }
    glFlush();
}

void outlineGrid()
{
    glColor3f(0.0f, 0.0f, 0.0f);
    for(int i = 0; i < 65; i ++){
        glBegin(GL_LINES);
            glVertex2f(0.425, 8.62-(0.12*i));
            glVertex2f(0.0817*PLANE_SIZE, 8.62-(0.12*i));
        glEnd();
    }
    for(int i = 0; i < 65; i ++){
        glBegin(GL_LINES);
            glVertex2f(0.425+(0.075*i), 8.62);
            glVertex2f(0.425+(0.075*i), 0.94);
        glEnd();
    }
    glFlush();
}

void filledCube(int x, int y, double r, double g, double b)
{
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
        glVertex3f(0.5+(x*0.075), 8.62-(y*0.12), 0.0);
        glVertex3f(0.425+(x*0.075), 8.62-(y*0.12), 0.0);
        glVertex3f(0.425+(x*0.075), 8.50-(y*0.12), 0.0);
        glVertex3f(0.5+(x*0.075), 8.50-(y*0.12), 0.0);
    glEnd();
    glFlush();
}

void lineLoop(float x1, float x2, float y1, float y2, float r, float g, float b){
    glColor3f(r, g, b);
    glBegin(GL_LINE_LOOP);
        glVertex3f(x1, y1, 0.0);
        glVertex3f(x2, y1, 0.0);
        glVertex3f(x2, y2, 0.0);
        glVertex3f(x1, y2, 0.0);
    glEnd();
    glFlush();
}

void quartalMap()
{
    for (int y = 0; y < 4;  y++) {
        for (int x = 0; x < 4; x++) {
            if (*game_state == 4) {
                if (*this_teamID == 1 && x >= 2) {
                    glColor3f(0.4f, 0.4f, 0.4f);
                    glBegin(GL_QUADS);
                    glVertex3f(0.5+(x*0.1), 9.6-(y*0.2), 0.0);
                    glVertex3f(0.6+(x*0.1), 9.6-(y*0.2), 0.0);
                    glVertex3f(0.6+(x*0.1), 9.4-(y*0.2), 0.0);
                    glVertex3f(0.5+(x*0.1), 9.4-(y*0.2), 0.0);
                    glEnd();
                    glFlush();
                } else if (*this_teamID == 2 && x < 2) {
                    glColor3f(0.4f, 0.4f, 0.4f);
                    glBegin(GL_QUADS);
                    glVertex3f(0.5+(x*0.1), 9.6-(y*0.2), 0.0);
                    glVertex3f(0.6+(x*0.1), 9.6-(y*0.2), 0.0);
                    glVertex3f(0.6+(x*0.1), 9.4-(y*0.2), 0.0);
                    glVertex3f(0.5+(x*0.1), 9.4-(y*0.2), 0.0);
                    glEnd();
                    glFlush();
                }
            }
            glColor3f(0.0f, 0.0f, 0.0f);
            if (y * 4 + x == *plane) {
                quartal_map[y][x] = 1;
                glBegin(GL_QUADS);
                    glVertex3f(0.5+(x*0.1), 9.6-(y*0.2), 0.0);
                    glVertex3f(0.6+(x*0.1), 9.6-(y*0.2), 0.0);
                    glVertex3f(0.6+(x*0.1), 9.4-(y*0.2), 0.0);
                    glVertex3f(0.5+(x*0.1), 9.4-(y*0.2), 0.0);
                glEnd();
                glFlush();
            }
            glBegin(GL_LINE_LOOP);
                glVertex3f(0.5+(x*0.1), 9.6-(y*0.2), 0.0);
                glVertex3f(0.6+(x*0.1), 9.6-(y*0.2), 0.0);
                glVertex3f(0.6+(x*0.1), 9.4-(y*0.2), 0.0);
                glVertex3f(0.5+(x*0.1), 9.4-(y*0.2), 0.0);
            glEnd();
            glFlush();
        }
    }
    if(*menu_state == 1){
        lineLoop(0.46, 0.94, 9.65, 8.75, 0, 0, 0.4);
    }
}

void showWinner()
{
    if (*winner_team == *this_teamID) {
        glColor3f(0.0f, 0.7f, 0.0f);
        printText("VICTORY", 4.5f, 5.0f);
    }
    else {
        glColor3f(0.7f, 0.0f, 0.0f);
        printText("DEFEAT", 4.5f, 5.0f);
    }
}

void printHUD()
{
    lineLoop(0.3, 5.4, 9.7, 0.3, 0, 0, 0);
    lineLoop(5.5, 9.7, 9.7, 0.3, 0, 0, 0);
    lineLoop(5.6, 9.6, 8.7, 6.2, 0, 0, 0);
    lineLoop(5.6, 9.6, 6.2, 3.7, 0, 0, 0);
    lineLoop(5.6, 9.6, 3.0, 1.1, 0, 0, 0);
    if (*game_state == 6) {
        if (current_ship->type != 0) {
            if (*action_state == 0) {
                glColor3f(0.0f, 0.0f, 0.0f);
                printNumber(*current_ship_speed, 6.9f, 9.05f);
                lineLoop(5.56, 5.94, 9.45, 8.85, 0.4, 0, 0);
            }
            else if (*action_state == 1) {
                glColor3f(0.0f, 0.0f, 0.0f);
                printNumber(*current_ship_range, 6.9f, 9.05f);
                lineLoop(5.96, 6.34, 9.45, 8.85, 0.4, 0, 0);
            }else if (*action_state == 2) {
                lineLoop(6.36, 6.74, 9.45, 8.85, 0.4, 0, 0);
            }
        }
        lineLoop(5.6, 5.9, 9.4, 8.9, 0, 0, 0);
        lineLoop(6.0, 6.3, 9.4, 8.9, 0, 0, 0);
        lineLoop(6.4, 6.7, 9.4, 8.9, 0, 0, 0);
        printText("1", 5.7, 9.05);
        printText("2", 6.1, 9.05);
        printText("3", 6.5, 9.05);

        int allyShips = 0;
        int enemyShips = 0;
        for(int i = 0; i != MAX_SHIPS; i++){
           if(*this_teamID == ships[i].team_id){
               allyShips = allyShips + 1;
           }else{
               enemyShips = enemyShips + 1;
           }
        }
        char team1[] = "Ally Team: X";
        char team2[] = "Enemy Team: X";
        team1[11] = allyShips + '0';
        team2[12] = enemyShips + '0';
        printText(team1, 5.6, 3.2);
        printText(team2, 8.4, 3.2);
    }

    glColor3f(0.0f, 1.0f, 0.0f);
    printText((char*)player_name, 5.2f - (glutBitmapLength(GLUT_BITMAP_TIMES_ROMAN_24, player_name) / 140.0), 9.05f);

    if(*menu_state == 0){
        lineLoop(0.35, 5.306, 8.75, 0.8, 0, 0, 0.4);
    }

    glColor3f(0.3f, 0.3f, 0.3f);
    printText("Press 'ESC' to exit", 8.2, 0.5);
}

void createPlane()
{
    printConnectedUsers();
    quartalMap();
    printHUD();
    if(*game_state == 4){
        glColor3f(0.0f, 0.0f, 0.4f);
        printText("Place your ships!", 2.2, 9.35);
    } else if (*game_state == 6){
        glColor3f(0.4f, 0.0f, 0.0f);
        printText("All Out WAR", 2.5, 9.35);
    }

    uint8_t koef_x = *plane % 4;
    uint8_t koef_y = *plane / 4;
    for(int x = 0; x < PLANE_SIZE; x++) {
        for (int y = 0; y < PLANE_SIZE; y++) {
            uint8_t battlefield_object = battlefield[(x+PLANE_SIZE*koef_x)+(y+PLANE_SIZE*koef_y)*BATTLEFIELD_X_MAX];
            if (
                (current_attack_position->x != 0 && current_attack_position->y != 0) &&
                (current_attack_position->x == (x+PLANE_SIZE*koef_x) && current_attack_position->y == (y+PLANE_SIZE*koef_y))
            ) {
                filledCube(x, y, 1.0f, 0.0f, 0.0f);
            } else if (battlefield_object == (enum BattlefieldObj) Island) {
                filledCube(x, y, 0.3f, 0.2f, 0.2f);
            } else if (battlefield_object == (enum BattlefieldObj) Rocks) {
                filledCube(x, y, 0.15f, 0.15f, 0.15f);
            } else if (battlefield_object == (enum BattlefieldObj) Fish) {
                filledCube(x, y, 0.1f, 0.2f, 0.7f);
            } else if (battlefield_object == (enum BattlefieldObj) Hit) {
                filledCube(x, y, 0.8f, 0.0f, 0.0f);
            } else if (battlefield_object == (enum BattlefieldObj) HitNot) {
                filledCube(x, y, 0.4f, 0.4f, 0.6f);
            } else if (battlefield_object >= 1 && battlefield_object <= 5) {
                if (isItPlacingShip((x+PLANE_SIZE*koef_x), (y+PLANE_SIZE*koef_y))) {
                    filledCube(x, y, 0.0f, 0.5f, 0.0f);
                } else {
                    char is_our = isItOurTeamShip((x+PLANE_SIZE*koef_x), (y+PLANE_SIZE*koef_y));
                    if (is_our == 1) {
                        filledCube(x, y, 0.0f, 0.0f, 0.3f);
                    } else if (is_our == 2) {
                        filledCube(x, y, 0.8f, 0.5f, 0.5f);
                    }
                }
            } else if (battlefield_object != 0) {
                if (*game_state == 6) {
                    filledCube(x, y, 1.0f, 1.0f, 1.0f);
                }
            }
        }
    }
    glFlush();
    outlineGrid();

    glColor3f(0.3f, 0.3f, 0.3f);
    printText("Movement:Arrow keys | Rotation:'<''>'| Menu:'TAB'", 0.42, 0.5);
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

    uint32_t shared_size = 0;
    server_socket = (int*) (shared_memory + shared_size); shared_size += sizeof(int);
    last_package_npk = (uint32_t*) (shared_memory + shared_size); shared_size += sizeof(uint32_t);
    *last_package_npk = 0;
    to_exit = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    need_redisplay = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    is_little_endian = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    to_get_ships = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    this_ID = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    this_teamID = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    player_name_len = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    player_name = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t) * MAX_PLAYER_NAME_LEN;
    players_count = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    players = (struct Player*) (shared_memory + shared_size); shared_size += sizeof(struct Player) * MAX_PLAYERS;
    ships = (struct Ship*) (shared_memory + shared_size); shared_size += sizeof(struct Ship) * MAX_SHIPS;
    menu_state = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    plane = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    battlefield_x = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    battlefield_y = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    battlefield = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t) * (BATTLEFIELD_X_MAX+1) * (BATTLEFIELD_Y_MAX+1);
    ships_to_place = (struct ShipToPlace*) (shared_memory + shared_size); shared_size += sizeof(struct ShipToPlace) * (MAX_SHIPS / 2);
    game_state = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    action_state = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    current_ship = (struct Ship*) (shared_memory + shared_size); shared_size += sizeof(struct Ship);
    current_ship_speed = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    current_ship_range = (uint16_t*) (shared_memory + shared_size); shared_size += sizeof(uint16_t);
    current_ship_is_dir = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    current_attack_position = (struct AttackPosition*) (shared_memory + shared_size); shared_size += sizeof(struct AttackPosition);
    winner_team = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
}

void gameloop()
{
    while (1) {
        if (*to_exit == 1) {
            exit(0);
        }

        uint8_t input[MAX_PACKAGE_SIZE];
        uint32_t nread = read(*server_socket, input, MAX_PACKAGE_SIZE);
        if(unpackPackage(input, nread, *last_package_npk, *is_little_endian) == -1) {
            continue;
        }
        processPackage(input);
    }
}

int clientConnect()
{
    /* uzbuvejam adresu strukturu */
    struct sockaddr_in remote_address;
    remote_address.sin_family = AF_INET;
    remote_address.sin_port = htons(PORT);
    inet_pton(AF_INET, HOST, &remote_address.sin_addr);

    /* veidojam socket */
    int my_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (my_socket < 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    /* meginam pieslegties serverim */
    if (connect(
        my_socket,
        (struct sockaddr*)&remote_address,
        sizeof(remote_address)
    ) < 0) {
        perror("Connect failed");
        exit(EXIT_FAILURE);
    }

    return my_socket;
}

void processPackage(uint8_t *msg)
{
    *last_package_npk = getPackageNPK(msg, *is_little_endian);
    if (*last_package_npk >= UINT32_MAX) {
        *last_package_npk = 0;
    }

    uint8_t msg_type = getPackageType(msg);
    if (msg_type == 1) {
        pkgACK(msg, getPackageContentSize(msg, *is_little_endian));
    } else if (msg_type == 3) {
        pkgLOBBY(msg, getPackageContentSize(msg, *is_little_endian));
    } else if (msg_type == 5) {
        pkgSTART_SETUP(msg, getPackageContentSize(msg, *is_little_endian));
    } else if (msg_type == 6) {
        pkgSTATE(msg, getPackageContentSize(msg, *is_little_endian));
        addShipsToPlace();
    } else if (msg_type == 7) {
        pkgTEV_JALIEK(msg, getPackageContentSize(msg, *is_little_endian));
    } else if (msg_type == 9) {
        pkgSTART_GAME(msg, getPackageContentSize(msg, *is_little_endian));
    } else if (msg_type == 10) {
        pkgTEV_JAIET(msg, getPackageContentSize(msg, *is_little_endian));
    } else if (msg_type == 12) {
        pkgEND_GAME(msg, getPackageContentSize(msg, *is_little_endian));
    }

    *need_redisplay = 1;
}

struct ShipToPlace* findShipToPlace(struct ShipToPlace* ships_arr, uint8_t type, uint8_t team_id)
{
    for (int i = 0; i < MAX_SHIPS/2; i++) {
        if (ships_arr[i].type == type && ships_arr[i].team_id == team_id) {
            return &ships_arr[i];
        }
    }
    return NULL;
}

void addShipsToPlace()
{
    for(int i = 0; i < MAX_SHIPS/2; i++){
        if(ships_to_place[i].placed == 0){
            continue;
        }
        uint8_t dir = ships_to_place[i].dir;
        char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
        char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;
        for(int a = 0; a < 6 - ships_to_place[i].type; a++){
            if (battlefield[(ships_to_place[i].x + a*dx) + (ships_to_place[i].y + a*dy) * BATTLEFIELD_X_MAX] != (enum BattlefieldObj) Hit) {
                battlefield[(ships_to_place[i].x + a*dx) + (ships_to_place[i].y + a*dy) * BATTLEFIELD_X_MAX] = ships_to_place[i].type;
            }
        }
    }
}

void removeShipsToPlace()
{
    for(int i = 0; i < MAX_SHIPS/2; i++){
        if(ships_to_place[i].placed == 0){
            continue;
        }
        uint8_t dir = ships_to_place[i].dir;
        char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
        char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;
        for(int a = 0; a < 6 - ships_to_place[i].type; a++){
            if (battlefield[(ships_to_place[i].x + a*dx) + (ships_to_place[i].y + a*dy) * BATTLEFIELD_X_MAX] == ships_to_place[i].type) {
                battlefield[(ships_to_place[i].x + a*dx) + (ships_to_place[i].y + a*dy) * BATTLEFIELD_X_MAX] = 0;
            }
        }
    }
}

char isItPlacingShip(uint8_t x, uint8_t y)
{
    struct ShipToPlace* ship = findShipToPlace(ships_to_place, current_ship->type, *this_teamID);
    if (ship == NULL) {
        return 0;
    }
    uint8_t dir = ship->dir;
    char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
    char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;
    for (int i = 0; i < 6 - ship->type; i++) {
        if (ship->x + i*dx == x && ship->y + i*dy == y) {
            return 1;
        }
    }
    return 0;
}

char isItOurTeamShip(uint8_t x, uint8_t y)
{
    if (*this_teamID == 0) {
        return 1;
    }

    for (int i = 0; i < MAX_SHIPS; i++) {
        uint8_t dir = ships[i].dir;
        char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
        char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;
        for (int a = 0; a < 6 - ships[i].type; a++) {
            if ((ships[i].x + a*dx) == x && (ships[i].y + a*dy) == y) {
                if (
                    ships[i].team_id == *this_teamID ||
                    ships[i].team_id == *this_teamID + 2
                ) {
                    return 1;
                }
                else if (ships[i].team_id == ((*this_teamID == 1) ? 4 : 3)) {
                    return 2;
                }
            }
        }

    }
    return 0;
}

void setPlaneToShip(struct ShipToPlace* ship_to_place)
{
    for (int x = 0; x < 4; x++) {
        for (int y = 0; y < 4; y++) {
            if (
                ship_to_place->x >= x*PLANE_SIZE - 1 &&
                ship_to_place->x <= (x+1)*PLANE_SIZE - 1 &&
                ship_to_place->y >= y*PLANE_SIZE - 1 &&
                ship_to_place->y <= (y+1)*PLANE_SIZE - 1
            ) {
                *plane = x + (y * 4);
                return;
            }
        }
    }
}


void pkgACK(uint8_t *msg, uint32_t content_size)
{
    uint8_t *content = getPackageContent(msg, content_size);

    *this_ID = content[0];
    *this_teamID = content[1];

    if (*this_ID == 0) {
        closeFunc();
    }

    if (*this_teamID == 0) {
        *game_state = 4;
    }

    printf("ID: %d\nTEAM: %d\n", *this_ID, *this_teamID);
}

void pkgLOBBY(uint8_t *msg, uint32_t content_size)
{
    if (*game_state == 1) {
        *game_state = 2;
    }
    uint8_t *content = getPackageContent(msg, content_size);

    uint32_t content_iter = 0;
    *players_count = content[content_iter++];
    for (int i = 0; i < MAX_PLAYERS; i++) {
        players[i].id = content[content_iter++];
        players[i].team_id = content[content_iter++];
        players[i].is_ready = content[content_iter++];
        players[i].name_len = content[content_iter++];
        for (int l = 0; l < MAX_PLAYER_NAME_LEN; l++) {
            players[i].name[l] = content[content_iter++];
        }
    }
}

void pkgSTART_SETUP(uint8_t *msg, uint32_t content_size)
{
    uint8_t *content = getPackageContent(msg, content_size);
    *battlefield_x = content[0];
    *battlefield_y = content[1];
    *plane = (*this_teamID == 1) ? 0 : 3;

    *game_state = 3;
}

void pkgSTATE(uint8_t *msg, uint32_t content_size)
{
    uint8_t *content = getPackageContent(msg, content_size);

    uint32_t content_iter = 0;
    *battlefield_x = content[content_iter++];
    *battlefield_y = content[content_iter++];
    uint32_t battlefield_size = (*battlefield_x) * (*battlefield_y);

    int k = 0;
    for (; k < battlefield_size; k++) {
        if (k <= *battlefield_x || k % *battlefield_x == 0) {
            battlefield[k] = (enum BattlefieldObj) Rocks;
            content_iter++;
        } else {
            battlefield[k] = content[content_iter++];
        }
    }
    battlefield[k++] = (enum BattlefieldObj) Rocks;
    for (; k < (sizeof(uint8_t) * (BATTLEFIELD_X_MAX+1) * (BATTLEFIELD_Y_MAX+1)); k++) {
        battlefield[k] = (enum BattlefieldObj) Rocks;
    }
    content_iter++;

    for (int i = 0; i < sizeof(struct Ship) * MAX_SHIPS; i++) {
        *(((char*)ships) + i) = content[content_iter++];
    }

    *players_count = content[content_iter++];
    for (int i = 0; i < sizeof(struct Player) * MAX_SHIPS; i++) {
        *(((char*)players) + i) = content[content_iter++];
    }
}

void pkgTEV_JALIEK(uint8_t *msg, uint32_t content_size)
{
    if (*game_state == 2 || *game_state == 3) {
        *plane = (*this_teamID == 1) ? 0 : 3;
        *game_state = 4;
    }

    uint8_t *content = getPackageContent(msg, content_size);

    for (int i = 0; i < MAX_SHIPS / 2; i++) {
        if (ships_to_place[i].type != content[1]) {
            ships_to_place[i].placed = 0;
        }
    }

    if (*this_ID != content[0]) {
        if (current_ship->type != 0) {
            current_ship->type = 0;
        }
        return;
    }

    if(ships[0].team_id != 0 && *to_get_ships == 0){
        *to_get_ships = 1;
        int ships_to_place_i = 0;
        for (int i = 0; i < MAX_SHIPS; i++) {
            if (ships[i].team_id == *this_teamID) {
                ships_to_place[ships_to_place_i].placed = 0;
                ships_to_place[ships_to_place_i].type = ships[i].type;
                ships_to_place[ships_to_place_i].x = ships[i].x;
                ships_to_place[ships_to_place_i].y = ships[i].y;
                ships_to_place[ships_to_place_i].dir = ships[i].dir;
                ships_to_place[ships_to_place_i].team_id = ships[i].team_id;
                ships_to_place[ships_to_place_i].damage = ships[i].damage;
                ships_to_place_i++;
            }
        }
    }

    struct ShipToPlace* ship = findShipToPlace(ships_to_place, content[1], *this_teamID);
    if (ship == NULL || ship->placed == 1) {
        return;
    }

    ship->x = (*this_teamID == 1) ? (PLANE_SIZE / 2) : *battlefield_x - (PLANE_SIZE / 2);
    ship->y = PLANE_SIZE / 2;
    ship->placed = 1;
    *plane = (*this_teamID == 1) ? 0 : 3;
    current_ship->type = ship->type;
    current_ship->team_id = *this_teamID;
}

void pkgES_LIEKU(uint8_t type, uint8_t x, uint8_t y, uint8_t dir)
{
    uint32_t content_size = 5;
    uint8_t msg[content_size];
    msg[0] = *this_ID;
    msg[1] = type;
    msg[2] = x;
    msg[3] = y;
    msg[4] = dir;

    *last_package_npk += 2;
    uint8_t* pES_LIEKU = preparePackage(*last_package_npk, 8, msg, &content_size, content_size, *is_little_endian);
    write(*server_socket, pES_LIEKU, content_size);
}

void pkgSTART_GAME(uint8_t *msg, uint32_t content_size)
{
    uint8_t *content = getPackageContent(msg, content_size);
    *battlefield_x = content[0];
    *battlefield_y = content[1];

    *game_state = 6;
}

void pkgTEV_JAIET(uint8_t *msg, uint32_t content_size)
{
    if (*game_state == 4) {
        *game_state = 6;
    }
    uint8_t *content = getPackageContent(msg, content_size);

    for (int i = 0; i < MAX_SHIPS / 2; i++) {
        if (ships_to_place[i].type != content[1]) {
            ships_to_place[i].placed = 0;
        }
    }

    if (*this_ID != content[0]) {
        if (current_ship->type != 0) {
            current_ship->type = 0;
            current_attack_position->x = 0;
            current_attack_position->y = 0;
        }
        return;
    }

    struct Ship* ship = findShipByIdAndTeamId(ships, content[1], *this_teamID);
    if (ship == NULL) {
        ship = findShipByIdAndTeamId(ships, content[1], *this_teamID + 2);
        if (ship == NULL) {
            return;
        }
    }

    if (current_ship->type == ship->type && current_ship->team_id == ship->team_id) {
        return;
    }

    struct ShipToPlace* ship_to_place = findShipToPlace(ships_to_place, ship->type, *this_teamID);
    if (ship_to_place == NULL) {
        return;
    }

    if (ship->damage > 0 || ship->type == 1) {
        *action_state = 1;

        if (ship->type == 3) {
            current_attack_position->y = ship->y - ((ship->dir == 0) ? 6 - ship->type - 1 : 0) - 1;
            current_attack_position->x = ship->x;
        }
        else {
            current_attack_position->x = ship->x;
            current_attack_position->y = ship->y;
        }
    }
    else {
        *action_state = 0;
    }

    ship_to_place->placed = 1;

    setPlaneToShip(ship_to_place);

    current_ship->type = ship->type;
    current_ship->x = ship->x;
    current_ship->y = ship->y;
    current_ship->dir = ship->dir;
    current_ship->team_id = ship->team_id;
    current_ship->damage = ship->damage;
    getShipData(ship->type, current_ship_speed, current_ship_range, current_ship_is_dir);
}

void pkgGAJIENS(uint8_t action, uint8_t x, uint8_t y, uint8_t thing)
{
    uint32_t content_size = 5;
    uint8_t msg[content_size];
    msg[0] = *this_ID;
    msg[1] = action;
    msg[2] = x;
    msg[3] = y;
    msg[4] = thing;

    *last_package_npk += 2;
    uint8_t* pES_LIEKU = preparePackage(*last_package_npk, 11, msg, &content_size, content_size, *is_little_endian);
    write(*server_socket, pES_LIEKU, content_size);
}

void pkgEND_GAME(uint8_t *msg, uint32_t content_size)
{
    uint8_t *content = getPackageContent(msg, content_size);
    *winner_team = content[1];
    *game_state = 8;
}
