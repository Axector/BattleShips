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

// Structure for ship placing
struct ShipToPlace {
    uint8_t type;
    uint8_t x;
    uint8_t y;
    uint8_t dir;
    uint8_t team_id;
    uint8_t damage;
    uint8_t placed;
};

// Attack position
struct AttackPosition {
    uint8_t x;
    uint8_t y;
};

/// Variables
char *shared_memory = NULL;

// Related to server
int *server_socket = NULL;              // Socket to communicate with server
uint32_t *last_package_npk = NULL;      // NPK for packages

// Flags
char *to_exit = NULL;                   // To close client if player decided to exit
char *need_redisplay = NULL;            // When battlfield needs redisplay
char *is_little_endian = NULL;          // To know if current system is little-endian or not
char *to_get_ships = NULL;              // Disposable flag to get ships info from the server for the first time

// This player data
uint8_t *this_ID = NULL;                // This player ID
uint8_t *this_teamID = NULL;            // This plaeyer
uint8_t *player_name_len = NULL;        // This player name length
uint8_t *player_name = NULL;            // This player name

// To store players and ships
uint8_t *players_count = NULL;          // Amount of players
struct Player *players = NULL;          // Players information
struct Ship *ships = NULL;              // Ships information

// Game process related
int quartal_map[4][4];                                  // Small map to move through the battlefield
uint8_t *menu_state = NULL;                             // To switch between different views
uint8_t *plane = NULL;                                  // To now current plane of the map
struct ShipToPlace *ships_to_place = NULL;              // To hande currently active ship
uint8_t *battlefield_x = NULL;                          // Battlefield x size
uint8_t *battlefield_y = NULL;                          // Battlefield y sizeof
uint8_t *battlefield = NULL;                            // Battlefield itself
uint8_t *game_state = NULL;                             // To move through the game
uint8_t *action_state = NULL;                           // To switch between dffernet actions of the ship
struct Ship *current_ship = NULL;                       // To store current ship info
uint8_t *current_ship_speed = NULL;                     // To know current active ship speed points
uint16_t *current_ship_range = NULL;                    // To know curent active ship range points
uint8_t *current_ship_is_dir = NULL;                    // To kwno if current active ship is attacking in one direction
struct AttackPosition *current_attack_position = NULL;  // To hande attakc mode of the ship
uint8_t *winner_team = NULL;                            // Winner team id

/// Functions in thit file
void getSharedMemory();
void gameloop();
int clientConnect();
void processPackage(uint8_t *msg);

// Visual part
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

// Auxiliary functions
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
    // Reserve and share memory between all needed variables
    getSharedMemory();

    // Check if current system is little-endian
    *is_little_endian = isLittleEndianSystem();

    *server_socket = clientConnect();
    glMain(argc, argv);

    // Separate thread for visual part
    int pid = 0;
    pid = fork();
    if (pid != 0) {
        while (1) {
            glutMainLoopEvent();

            //To reinitialize display with state updates
            if (*need_redisplay == 1) {
                *need_redisplay = 0;
                glutPostRedisplay();
            }
        }
    }

    // Wait for player to input name
    while(*game_state == 0) {
        if (*to_exit == 1) {
            exit(0);
        }
    }

    // Send LABDIEN package with this player name
    uint32_t content_size = *player_name_len;
    uint8_t* pLABDIEN = preparePackage(*last_package_npk, 0, player_name, &content_size, MAX_PLAYER_NAME_LEN, *is_little_endian);
    write(*server_socket, pLABDIEN, content_size);

    // Recieve an answer from server with this player id and team id
    uint8_t input[MAX_PACKAGE_SIZE];
    uint32_t nread = read(*server_socket, input, MAX_PACKAGE_SIZE);
    if(unpackPackage(input, nread, *last_package_npk, *is_little_endian) == -1) {
        closeFunc();
    }
    processPackage(input);

    // Start gameloop
    gameloop();
    exit(0);
}

void glMain(int argc, char *argv[])
{
    glutInit(&argc, argv);
    // All openGl binded functions and settings
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
    // display functions, everything that's here will be rendered on screen. Acordingly to game state change
    glClearColor(0.5, 0.5, 0.5, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    // Name input view
    if(*game_state == 0){
        inputField();
    // Loading screen
    } else if (*game_state == 1 || *game_state == 3) {
        loadingScreen();
    // LOBBY
    } else if (*game_state == 2) {
        lobby();
    // Ship placement and game state
    } else if (*game_state == 4 || *game_state == 6) {
        addShipsToPlace();
        createPlane();
    // The end of the game
    } else if (*game_state == 8) {
        showWinner();
    }
    glutSwapBuffers();
}


// function that wont allow window resizing
void resize(int width, int height)
{
    glutReshapeWindow(1400, 800);
}

// For special keyboard buttons
void specialKeyboard(int key, int x, int y)
{
    // key manipulations with quartal map (switch between planes of the map)
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
    // Ship placement
    } else if (*game_state == 4 && *menu_state == 0) {
        // Get current ship that is visible on screen to be moved
        struct ShipToPlace* ship = findShipToPlace(ships_to_place, current_ship->type, *this_teamID);
        if (ship == NULL) {
            return;
        }

        // Get direction increments
        char dx = (ship->dir == 1) ? 1 : (ship->dir == 3) ? -1 : 0;
        char dy = (ship->dir == 0) ? -1 : (ship->dir == 2) ? 1 : 0;

        removeShipsToPlace();
        if (key == GLUT_KEY_UP) {
            // Ship cannot be moved on some objects on the battlefield
            for (int i = 0; i < 6 - ship->type; i++) {
                uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy) - 1) * (*battlefield_x)];
                if (object_type >= 10 && object_type <= 19) {
                    return;
                }
            }
            // Check if ship is not out of battlfield border
            if ((ship->y - ((ship->dir == 0) ? 6 - ship->type - 1 : 0)) <= 0 + 1) {
                return;
            }

            // Change plane depending on ship location
            if (*plane / 4 > 0 && ship->y <= (*plane / 4) * PLANE_SIZE ) {
                *plane -= 4;
            }

            ship->y -= 1;
        } else if (key == GLUT_KEY_DOWN) {
            // Ship cannot be moved on some objects on the battlefield
            for (int i = 0; i < 6 - ship->type; i++) {
                uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy) + 1) * (*battlefield_x)];
                if (object_type >= 10 && object_type <= 19) {
                    return;
                }
            }

            // Check if ship is not out of battlfield border
            if ((ship->y + ((ship->dir == 2) ? 6 - ship->type - 1 : 0)) >= *battlefield_y - 1) {
                return;
            }

            // Change plane depending on ship location
            if (*plane / 4 < 4 && ship->y >= ((*plane / 4) + 1) * PLANE_SIZE - 1) {
                *plane += 4;
            }

            ship->y += 1;
        } else if (key == GLUT_KEY_LEFT) {
            // Ship cannot be moved on some objects on the battlefield
            for (int i = 0; i < 6 - ship->type; i++) {
                uint8_t object_type = battlefield[((ship->x + i * dx) - 1) + (ship->y + i * dy) * (*battlefield_x)];
                if (object_type >= 10 && object_type <= 19) {
                    return;
                }
            }

            // Check if ship is not out of available battlfield side border
            if (
                ((*this_teamID == 1 && (ship->x - ((ship->dir == 3) ? 6 - ship->type - 1 : 0)) <= 0 + 1) ||
                (*this_teamID == 2 && (ship->x - ((ship->dir == 3) ? 6 - ship->type - 1 : 0)) <= (*battlefield_x / 2) + 1))
            ) {
                return;
            }

            // Change plane depending on ship location
            if (*plane % 4 > 0 && ship->x <= (*plane % 4) * PLANE_SIZE) {
                *plane -= 1;
            }

            ship->x -= 1;
        } else if (key == GLUT_KEY_RIGHT) {
            // Ship cannot be moved on some objects on the battlefield
            for (int i = 0; i < 6 - ship->type; i++) {
                uint8_t object_type = battlefield[((ship->x + i * dx) + 1) + (ship->y + i * dy) * (*battlefield_x)];
                if (object_type >= 10 && object_type <= 19) {
                    return;
                }
            }

            // Check if ship is not out of available battlfield side border
            if (
                ((*this_teamID == 1 && (ship->x + ((ship->dir == 1) ? 6 - ship->type - 1 : 0)) >= (*battlefield_x / 2)) ||
                (*this_teamID == 2 && (ship->x + ((ship->dir == 1) ? 6 - ship->type - 1 : 0)) >= *battlefield_x - 1))
            ) {
                return;
            }

            // Change plane depending on ship location
            if (*plane % 4 < 4 && ship->x >= ((*plane % 4) + 1) * PLANE_SIZE - 1) {
                *plane += 1;
            }

            ship->x += 1;
        }
    // Game state
    } else if (*game_state == 6 && *menu_state == 0) {
        // Get current ship that is visible on screen to be moved
        struct ShipToPlace* ship = findShipToPlace(ships_to_place, current_ship->type, *this_teamID);
        if(ship == NULL){
            return;
        }

        // Get ship direction increments
        char dx = (ship->dir == 1) ? 1 : (ship->dir == 3) ? -1 : 0;
        char dy = (ship->dir == 0) ? -1 : (ship->dir == 2) ? 1 : 0;

        // Movement action of the ship
        if (*action_state == 0) {
            // Update visibility of current ship
            removeShipsToPlace();
            if (key == GLUT_KEY_UP) {
                // Ship cannot be moved on some objects on the battlefield
                for (int i = 0; i < 6 - ship->type; i++) {
                    uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy) - 1) * (*battlefield_x)];
                    if (object_type >= 10 && object_type <= 19) {
                        return;
                    }
                }

                // Check if ship is not out of battlfield border
                if ((ship->y - ((ship->dir == 0) ? 6 - ship->type - 1 : 0)) <= 0 + 1) {
                    return;
                }

                // Move ship if it has enough speed points
                if (current_ship->y >= ship->y) {
                    if (*current_ship_speed > 0) {
                        // Change plane depending on ship location
                        if (*plane / 4 > 0 && ship->y <= (*plane / 4) * PLANE_SIZE) {
                            *plane -= 4;
                        }
                        ship->y -= 1;
                        *current_ship_speed -= 1;
                    }
                } else if (current_ship->y <= ship->y) {
                    // Change plane depending on ship location
                    if (*plane / 4 > 0 && ship->y <= (*plane / 4) * PLANE_SIZE) {
                        *plane -= 4;
                    }
                    ship->y -= 1;
                    *current_ship_speed += 1;
                }
            } else if (key == GLUT_KEY_DOWN) {
                // Ship cannot be moved on some objects on the battlefield
                for (int i = 0; i < 6 - ship->type; i++) {
                    uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy) + 1) * (*battlefield_x)];
                    if (object_type >= 10 && object_type <= 19) {
                        return;
                    }
                }

                // Check if ship is not out of battlfield border
                if ((ship->y + ((ship->dir == 2) ? 6 - ship->type - 1 : 0)) >= *battlefield_y - 1) {
                    return;
                }

                // Move ship if it has enough speed points
                if (current_ship->y <= ship->y) {
                    if (*current_ship_speed > 0) {
                        // Change plane depending on ship location
                        if (*plane / 4 < 4 && ship->y >= ((*plane / 4) + 1) * PLANE_SIZE - 1) {
                            *plane += 4;
                        }
                        ship->y += 1;
                        *current_ship_speed -= 1;
                    }
                } else if (current_ship->y >= ship->y) {
                    // Change plane depending on ship location
                    if (*plane / 4 < 4 && ship->y >= ((*plane / 4) + 1) * PLANE_SIZE - 1) {
                        *plane += 4;
                    }
                    ship->y += 1;
                    *current_ship_speed += 1;
                }
            } else if (key == GLUT_KEY_LEFT) {
                // Ship cannot be moved on some objects on the battlefield
                for (int i = 0; i < 6 - ship->type; i++) {
                    uint8_t object_type = battlefield[((ship->x + i * dx) - 1) + (ship->y + i * dy) * (*battlefield_x)];
                    if (object_type >= 10 && object_type <= 19) {
                        return;
                    }
                }

                // Check if ship is not out of battlfield border
                if ((ship->x - ((ship->dir == 3) ? 6 - ship->type - 1 : 0)) <= 0 + 1) {
                    return;
                }

                // Move ship if it has enough speed points
                if (current_ship->x >= ship->x) {
                    if (*current_ship_speed > 0) {
                        // Change plane depending on ship location
                        if (*plane % 4 > 0 && ship->x <= (*plane % 4) * PLANE_SIZE) {
                            *plane -= 1;
                        }
                        ship->x -= 1;
                        *current_ship_speed -= 1;
                    }
                } else if (current_ship->x <= ship->x) {
                    // Change plane depending on ship location
                    if (*plane % 4 > 0 && ship->x <= (*plane % 4) * PLANE_SIZE) {
                        *plane -= 1;
                    }
                    ship->x -= 1;
                    *current_ship_speed += 1;
                }
            } else if (key == GLUT_KEY_RIGHT) {
                // Ship cannot be moved on some objects on the battlefield
                for (int i = 0; i < 6 - ship->type; i++) {
                    uint8_t object_type = battlefield[((ship->x + i * dx) + 1) + (ship->y + i * dy) * (*battlefield_x)];
                    if (object_type >= 10 && object_type <= 19) {
                        return;
                    }
                }

                // Check if ship is not out of battlfield border
                if ((ship->x + ((ship->dir == 1) ? 6 - ship->type - 1 : 0)) >= *battlefield_x - 1) {
                    return;
                }

                // Move ship if it has enough speed points
                if (current_ship->x <= ship->x) {
                    if (*current_ship_speed > 0) {
                        // Change plane depending on ship location
                        if (*plane % 4 < 4 && ship->x >= ((*plane % 4) + 1) * PLANE_SIZE - 1) {
                            *plane += 1;
                        }
                        ship->x += 1;
                        *current_ship_speed -= 1;
                    }
                } else if (current_ship->x >= ship->x) {
                    // Change plane depending on ship location
                    if (*plane % 4 < 4 && ship->x >= ((*plane % 4) + 1) * PLANE_SIZE - 1) {
                        *plane += 1;
                    }
                    ship->x += 1;
                    *current_ship_speed += 1;
                }
            }
        // Attack action of the ship
        } else if (*action_state == 1) {
            if (key == GLUT_KEY_UP) {
                // Ship of 3rd type has torpedo, so it just changes direction of the shot
                if (current_ship->type == 3) {
                    current_attack_position->y = ship->y - ((ship->dir == 0) ? 6 - ship->type - 1 : 0) - 1;
                    current_attack_position->x = ship->x;
                    return;
                }

                // Check if attack marker is inside of the battlefield
                if (current_attack_position->y <= 0 + 1) {
                    return;
                }

                // Move attack marker if ship has enough range points
                if (current_ship->y >= current_attack_position->y) {
                    if (*current_ship_range > 0 || current_ship->type == 1) {
                        // Change plane depending on ship location
                        if (*plane / 4 > 0 && current_attack_position->y <= (*plane / 4) * PLANE_SIZE) {
                            *plane -= 4;
                        }
                        current_attack_position->y -= 1;
                        *current_ship_range -= 1;
                    }
                } else if (current_ship->y <= current_attack_position->y) {
                    // Change plane depending on ship location
                    if (*plane / 4 > 0 && current_attack_position->y <= (*plane / 4) * PLANE_SIZE) {
                        *plane -= 4;
                    }
                    current_attack_position->y -= 1;
                    *current_ship_range += 1;
                }
            }
            if (key == GLUT_KEY_DOWN) {
                // Ship of 3rd type has torpedo, so it just changes direction of the shot
                if (current_ship->type == 3) {
                    current_attack_position->y = ship->y + ((ship->dir == 2) ? 6 - ship->type - 1 : 0) + 1;
                    current_attack_position->x = ship->x;
                    return;
                }

                // Check if attack marker is inside of the battlefield
                if (current_attack_position->y >= *battlefield_y - 1) {
                    return;
                }

                // Move attack marker if ship has enough range points
                if (current_ship->y <= current_attack_position->y) {
                    if (*current_ship_range > 0 || current_ship->type == 1) {
                        // Change plane depending on ship location
                        if (*plane / 4 < 4 && current_attack_position->y >= ((*plane / 4) + 1) * PLANE_SIZE - 1) {
                            *plane += 4;
                        }
                        current_attack_position->y += 1;
                        *current_ship_range -= 1;
                    }
                } else if (current_ship->y >= current_attack_position->y) {
                    // Change plane depending on ship location
                    if (*plane / 4 < 4 && current_attack_position->y >= ((*plane / 4) + 1) * PLANE_SIZE - 1) {
                        *plane += 4;
                    }
                    current_attack_position->y += 1;
                    *current_ship_range += 1;
                }
            }
            if (key == GLUT_KEY_LEFT) {
                // Ship of 3rd type has torpedo, so it just changes direction of the shot
                if (current_ship->type == 3) {
                    current_attack_position->x = ship->x - ((ship->dir == 3) ? 6 - ship->type - 1 : 0) - 1;
                    current_attack_position->y = ship->y;
                    return;
                }

                // Check if attack marker is inside of the battlefield
                if (current_attack_position->x <= 0 + 1) {
                    return;
                }

                // Move attack marker if ship has enough range points
                if (current_ship->x >= current_attack_position->x) {
                    if (*current_ship_range > 0 || current_ship->type == 1) {
                        // Change plane depending on ship location
                        if (*plane % 4 > 0 && current_attack_position->x <= (*plane % 4) * PLANE_SIZE) {
                            *plane -= 1;
                        }
                        current_attack_position->x -= 1;
                        *current_ship_range -= 1;
                    }
                } else if (current_ship->x <= current_attack_position->x) {
                    // Change plane depending on ship location
                    if (*plane % 4 > 0 && current_attack_position->x <= (*plane % 4) * PLANE_SIZE) {
                        *plane -= 1;
                    }
                    current_attack_position->x -= 1;
                    *current_ship_range += 1;
                }
            }
            if (key == GLUT_KEY_RIGHT) {
                // Ship of 3rd type has torpedo, so it just changes direction of the shot
                if (current_ship->type == 3) {
                    current_attack_position->x = ship->x + ((ship->dir == 1) ? 6 - ship->type - 1 : 0) + 1;
                    current_attack_position->y = ship->y;
                    return;
                }

                // Check if attack marker is inside of the battlefield
                if (current_attack_position->x >= *battlefield_x - 1) {
                    return;
                }

                // Move attack marker if ship has enough range points
                if (current_ship->x <= current_attack_position->x) {
                    if (*current_ship_range > 0 || current_ship->type == 1) {
                        // Change plane depending on ship location
                        if (*plane % 4 < 4 && current_attack_position->x >= ((*plane % 4) + 1) * PLANE_SIZE - 1) {
                            *plane += 1;
                        }
                        current_attack_position->x += 1;
                        *current_ship_range -= 1;
                    }
                } else if (current_ship->x >= current_attack_position->x) {
                    // Change plane depending on ship location
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
    // Exit on Escape
    if(key == 27){
        exit(0);
    }
    // Change state for accessing menus on Tab
    if (*game_state == 4 || *game_state == 6) {
        if(key == 9){
            *menu_state += 1;
            if(*menu_state >= 2){
                *menu_state = 0;
            }
        }
    }
    // Name input game state
    if(*game_state == 0){
        if(key == 13){
            // Name cannot be empty
            if (player_name[0] == 0) {
                return;
            }

            // Switch to next game state when name is submitted
            *game_state = 1;
        } else if(key == 8){
            // Backspace - removing last letter from name
            if (*player_name_len > 0) {
                player_name[*player_name_len - 1] = 0;
                *player_name_len -= 1;
            }
        }else{
            // Allowed symbols for name
            if (
                *player_name_len < MAX_PLAYER_NAME_LEN &&
                ((key >= 'A' && key <= 'Z') ||
                (key >= 'a' && key <= 'z') ||
                (key >= '0' && key <= '9') ||
                (key == '_' || key == '-'))
            ) {
                // auto lowercase
                player_name[*player_name_len] = tolower(key);
                *player_name_len += 1;
            }
        }
    // LOBBY
    } else if (*game_state == 2) {
        // submiting that player is ready
        if(key == 'r') {
            uint8_t msg[2];
            msg[0] = *this_ID;
            // Find current player in list of players from server and get an oposite is_ready state from it
            msg[1] = (findPlayerById(players, *this_ID)->is_ready == 1) ? 0 : 1;
            uint32_t content_size = 2;
            // Prepare and send package with ready or not ready state
            *last_package_npk += 2;
            uint8_t* pENTER = preparePackage(*last_package_npk, 4, msg, &content_size, content_size, *is_little_endian);
            write(*server_socket, pENTER, content_size); // writing to server
        }
    // Ships placement
    } else if(*game_state == 4) {
        // Get current ship that is visible on screen to be moved
        struct ShipToPlace* ship = findShipToPlace(ships_to_place, current_ship->type, *this_teamID);
        if (ship == NULL) {
            return;
        }
        // Ship rotation to the left
        if (key == ',' && ship->type != 5) {
            // Get direction increments
            uint8_t dir = (ship->dir == 0) ? 3 : ship->dir - 1;
            char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
            char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;

            // Ship cannot be rotated on some objects on the battlefield
            for (int i = 0; i < 6 - ship->type; i++) {
                uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy)) * (*battlefield_x)];
                if (object_type >= 10 && object_type <= 19) {
                    return;
                }
            }

            // Ship boundaries on rotation by comparing with available battlefield side borders
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

            // Update current ship rotation
            removeShipsToPlace();
            ship->dir = (ship->dir == 0) ? 3 : ship->dir - 1;
        // Ship rotation to the right
        } else if(key == '.' && ship->type != 5) {
            // Get direction increments
            uint8_t dir = (ship->dir == 3) ? 0 : ship->dir + 1;
            char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
            char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;

            // Ship cannot be rotated on some objects on the battlefield
            for (int i = 0; i < 6 - ship->type; i++) {
                uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy)) * (*battlefield_x)];
                if (object_type >= 10 && object_type <= 19) {
                    return;
                }
            }

            // Ship boundaries on rotation by comparing with available battlefield side borders
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

            // Update current ship rotation
            removeShipsToPlace();
            ship->dir = (ship->dir == 3) ? 0 : ship->dir + 1;
        } else if(key == 13) {
            char dx = (ship->dir == 1) ? 1 : (ship->dir == 3) ? -1 : 0;
            char dy = (ship->dir == 0) ? -1 : (ship->dir == 2) ? 1 : 0;

            // Check if player is not trying to put ship on his team another ship
            for (int i = 0; i < 6 - ship->type; i++) {
                if (isItOurTeamShip((ship->x + i * dx), (ship->y + i * dy)) == 1) {
                    return;
                }
            }
            // Prepare and send package with new position of the ship
            pkgES_LIEKU(ship->type, ship->x, ship->y, ship->dir);
        }
    // Game state
    } else if (*game_state == 6) {
        // Get current ship that is visible on screen to be moved
        struct ShipToPlace* ship = findShipToPlace(ships_to_place, current_ship->type, *this_teamID);
        if (ship == NULL) {
            return;
        }

        if (key == 13) {
            // MOVEMENT
            if (*action_state == 0) {
                char dx = (ship->dir == 1) ? 1 : (ship->dir == 3) ? -1 : 0;
                char dy = (ship->dir == 0) ? -1 : (ship->dir == 2) ? 1 : 0;

                // Check if player is not trying to put ship on his team another ship
                for (int i = 0; i < 6 - ship->type; i++) {
                    if (isItOurTeamShip((ship->x + i * dx), (ship->y + i * dy)) == 1) {
                        return;
                    }
                }
                // Prepare and send package with new position of the ship
                pkgGAJIENS(1, ship->x, ship->y, ship->dir);
            }
            // ATTACK
            else if (*action_state == 1) {
                uint8_t current_x = current_attack_position->x;
                uint8_t current_y = current_attack_position->y;
                // Check the torpedo attack position depending on the direction to which it was sent
                if (ship->type == 3) {
                    if (current_x < current_ship->x) {
                        while (current_x >= 0 + 1) {
                            // If it faced some obstacles, stop and send current position except allies
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
                            // If it faced some obstacles, stop and send current position except allies
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
                            // If it faced some obstacles, stop and send current position except allies
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
                            // If it faced some obstacles, stop and send current position except allies
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

                // If it faced some obstacle that is not an enemy ship then it is a miss
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

                // Prepare and send coordinates of the attack to the server
                pkgGAJIENS(2, current_x, current_y, 0);
            }
        // Switch to ship movement action
        } else if (*action_state != 0 && current_ship->type != 1 && current_ship->damage <= 0 && key == '1') {
            *action_state = 0;
            setPlaneToShip(ship);
            // Undo attack action
            current_attack_position->x = 0;
            current_attack_position->y = 0;
        // Switch to ship attack action
        } else if (*action_state != 1 && current_ship->type != 5 && key == '2') {
            *action_state = 1;
            // Undo movement action moves
            ship->x = current_ship->x;
            ship->y = current_ship->y;
            ship->dir = current_ship->dir;
            getShipData(ship->type, current_ship_speed, current_ship_range, current_ship_is_dir);
            setPlaneToShip(ship);
            // Set starting position of the attack
            if (ship->type == 3) {
                current_attack_position->y = ship->y - ((ship->dir == 0) ? 6 - ship->type - 1 : 0) - 1;
                current_attack_position->x = ship->x;
            }
            else {
                current_attack_position->x = ship->x;
                current_attack_position->y = ship->y;
            }
        // If it is movement action selected
        } else if (*action_state == 0) {
            // Rotation to the left
            if (key == ',' && ship->type != 5) {
                // Get direction increments
                uint8_t dir = (ship->dir == 0) ? 3 : ship->dir - 1;
                char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
                char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;

                // Ship cannot be rotated on some objects on the battlefield
                for (int i = 0; i < 6 - ship->type; i++) {
                    uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy)) * (*battlefield_x)];
                    if (object_type >= 10 && object_type <= 19) {
                        return;
                    }
                }

                // Ship boundaries on rotation by comparing with battlefield borders
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

                // Calculate ship speed points depending on rotation direction
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

                // Update current ship rotation
                removeShipsToPlace();
                ship->dir = (ship->dir == 0) ? 3 : ship->dir - 1;
            // Rotation to the right
            } else if(key == '.' && ship->type != 5) {
                // Get direction increments
                uint8_t dir = (ship->dir == 3) ? 0 : ship->dir + 1;
                char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
                char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;

                // Ship cannot be rotated on some objects on the battlefield
                for (int i = 0; i < 6 - ship->type; i++) {
                    uint8_t object_type = battlefield[(ship->x + i * dx) + ((ship->y + i * dy)) * (*battlefield_x)];
                    if (object_type >= 10 && object_type <= 19) {
                        return;
                    }
                }

                // Ship boundaries on rotation by comparing with battlefield borders
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

                // Calculate ship speed points depending on rotation direction
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

                // Update current ship rotation
                removeShipsToPlace();
                ship->dir = (ship->dir == 3) ? 0 : ship->dir + 1;
            }
        }
    }

    *need_redisplay = 1;
}

// Function to set close flag
void closeFunc()
{
    *to_exit = 1;
    exit(0);
}

// Function to print given text with given coordinates on screen
void printText(char *text, double x, double y)
{
    // Set text starting position
    glRasterPos3f(x, y, 0);
    // Printing every symbol of the text with built in openGL
    while(*text){
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, *text++);
    }
    glFlush();
}

// Function to print given number with given coordinates on screen
void printNumber(uint8_t number, double x, double y)
{
    // Number must be greater than 0
    if (number < 0) {
        return;
    }

    // If number is 0 printing only 1 element
    glRasterPos3f(x, y, 0);
    if (number == 0) {
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, '0');
    }

    uint8_t size = 0;
    int new_number = 0;
    // Get reversed number to print it in correct order
    for (int i = number; i > 0; i /= 10) {
        size++;
        new_number *= 10;
        new_number += i % 10;
    }

    // Printing every digit of the number
    for (int i = 0; i < size; i++){
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, new_number % 10 + '0');
        new_number /= 10;
    }
}

// Loading screen contents
void loadingScreen()
{
    glColor3f(0.0f, 0.0f, 0.0f);
    printText("Loading...", 8.0f, 2.0f);
}

// LOBBY contents
void lobby()
{
    // Buttons and suggestions
    glColor3f(0.3f, 0.3f, 0.3f);
    printText("Press 'ESC' to exit", 0.2, 9.5);
    printText("press 'R' to Ready up", 4.2, 1.2);
    glColor3f(0.0f, 0.0f, 0.0f);
    glRasterPos3f(4.0, 9.0, 0);
    // Title
    printText("Lobby", 4.75, 9.2);
    lineLoop(0.8, 9.2, 8.8, 1.7, 0, 0, 0);
    lineLoop(1, 5, 8.5, 2, 0, 0, 0);
    lineLoop(5, 9, 8.5, 2, 0, 0, 0);

    // Print players from each team and their ready state
    char *ready_state = "Ready";
    int team1 = 0;
    int team2 = 0;
    for(int i = 0; i < MAX_PLAYERS; i++){
        glColor3f(0.0f, 0.0f, 0.0f);
        if (players[i].id == 0) {
            continue;
        }

        // printing the first team players
        if(players[i].team_id == 1){
            glRasterPos3f(1.1, 8.0-(0.3*team1), 0);
            // printing player name
            for(int a = 0; a < strlen(players[i].name); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
            }
            // checing ready state and changing it
            if (players[i].is_ready == 1) {
                glColor3f(0.0f, 0.9f, 0.0f);
                ready_state = "Ready";
            } else {
                glColor3f(0.9f, 0.0f, 0.0f);
                ready_state = "Not Ready";
            }

            // Printing ready state on screen
            glRasterPos3f(4.2+(players[i].is_ready == 1 ? 0.3 : 0.0), 8.0-(0.3*team1), 0);
            for(int a = 0; a < strlen(ready_state); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, ready_state[a]);
            }
            glFlush();
            team1++;
        }
        // printing the second team
        else if(players[i].team_id == 2){
            glRasterPos3f(5.1, 8.0-(0.3*team2), 0);
            // printing player name
            for(int a = 0; a < strlen(players[i].name); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
            }
            // checing ready state and changing it
            if (players[i].is_ready == 1) {
                glColor3f(0.0f, 0.9f, 0.0f);
                ready_state = "Ready";
            } else {
                glColor3f(0.9f, 0.0f, 0.0f);
                ready_state = "Not Ready";
            }

            // Printing ready state on screen
            glRasterPos3f(8.2+(players[i].is_ready == 1 ? 0.3 : 0.0), 8.0-(0.3*team2), 0);
            for(int a = 0; a < strlen(ready_state); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, ready_state[a]);
            }
            glFlush();
            team2++;
        }

    }
}

// Printing connected users by dividing them in teams
void printConnectedUsers()
{
    // for spectator (ghost) printing all users
    if (*this_teamID == 0) {
        uint8_t player_iter = 0;
        // Print half of the players in the first border
        for (int i = 0; i < MAX_PLAYERS/2; i++) {
            if (players[i].id == 0) {
                continue;
            }

            glColor3f(0.0f, 0.0f, 0.0f);
            glRasterPos3f(5.7, 8.3-(0.3*player_iter++), 0);
            for(int a = 0; a < strlen(players[i].name) && a < MAX_PLAYER_NAME_LEN; a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
            }
        }
        player_iter = 0;
        // Print half of the players in the second border
        for (int i = MAX_PLAYERS/2; i < MAX_PLAYERS; i++) {
            if (players[i].id == 0 || players[i].team_id == *this_teamID) {
                continue;
            }

            glColor3f(0.0f, 0.0f, 0.0f);
            glRasterPos3f(5.7, 5.8-(0.3*player_iter++), 0);
            for(int a = 0; a < strlen(players[i].name) && a < MAX_PLAYER_NAME_LEN; a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
            }
        }
        glFlush();
        return;
    }

    uint8_t player_iter = 0;
    // printing only users from your team
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].id == 0 || players[i].team_id != *this_teamID) {
            continue;
        }

        glColor3f(0.0f, 0.8f, 0.0f);
        glRasterPos3f(5.7, 8.3-(0.3*player_iter++), 0);
        for(int a = 0; a < strlen(players[i].name) && a < MAX_PLAYER_NAME_LEN; a++){
            glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
        }
    }
    player_iter = 0;
    // printing users from enemy team
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].id == 0 || players[i].team_id == *this_teamID) {
            continue;
        }

        glColor3f(0.8f, 0.0f, 0.0f);
        glRasterPos3f(5.7, 5.8-(0.3*player_iter++), 0);
        for(int a = 0; a < strlen(players[i].name) && a < MAX_PLAYER_NAME_LEN; a++){
            glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
        }
    }
    glFlush();
}

// input in first screen. Gets changes in player_name and prints every symbol
void inputField()
{
    // Buttons and sugestions
    glColor3f(0.3f, 0.3f, 0.3f);
    printText("press 'ENTER' to submit", 4.2, 6);
    glColor3f(0.0f, 0.0f, 0.0f);
    printText("Enter your name", 4.5, 9);
    glColor3f(0.3f, 0.3f, 0.3f);
    printText("Press 'ESC' to exit", 0.2, 9.5);

    // Border
    lineLoop(2, 8, 8.1, 7.1, 0, 0, 0);

    // Print inputted name
    glRasterPos3f(5 - (glutBitmapLength(GLUT_BITMAP_TIMES_ROMAN_24, player_name) / 280.0), 7.5, 0);
    for(int i = 0; i < MAX_PLAYER_NAME_LEN || player_name[i] != '\0'; i++){
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, player_name[i]);
    }
    glFlush();
}

// printing main battlefield plane by drawing 65*65 lines to make 64*64 squares
void outlineGrid()
{
    glColor3f(0.0f, 0.0f, 0.0f);
    // Horizontal lines
    for(int i = 0; i < 65; i ++){
        glBegin(GL_LINES);
            glVertex2f(0.425, 8.62-(0.12*i));
            glVertex2f(0.0817*PLANE_SIZE, 8.62-(0.12*i));
        glEnd();
    }
    // Vertical lines
    for(int i = 0; i < 65; i ++){
        glBegin(GL_LINES);
            glVertex2f(0.425+(0.075*i), 8.62);
            glVertex2f(0.425+(0.075*i), 0.94);
        glEnd();
    }
    glFlush();
}

// filled cube used for every filled unit on the battlefield
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


// basic line loop (drawing empty squares)
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

// mini map
void quartalMap()
{
    // Draw mini map squares
    for (int y = 0; y < 4;  y++) {
        for (int x = 0; x < 4; x++) {
            if (*game_state == 4) {
                // During ship placing state half of the map is disabled
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
            // filing square by currently picked plane (plane picking in keyboard)
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
    // if menu state is 1, draw outline
    if(*menu_state == 1){
        lineLoop(0.46, 0.94, 9.65, 8.75, 0, 0, 0.4);
    }
}

// Last game view
void showWinner()
{
    // Suggestion
    glColor3f(0.3f, 0.3f, 0.3f);
    printText("Press 'ESC' to exit", 0.2, 9.5);
    // if team won draw victory, else defeat
    if (*winner_team == *this_teamID) {
        glColor3f(0.0f, 0.7f, 0.0f);
        printText("VICTORY", 4.5f, 5.0f);
    }
    else {
        glColor3f(0.7f, 0.0f, 0.0f);
        printText("DEFEAT", 4.5f, 5.0f);
    }
}

// HUD
void printHUD()
{
    // Borders
    lineLoop(0.3, 5.4, 9.7, 0.3, 0, 0, 0);
    lineLoop(5.5, 9.7, 9.7, 0.3, 0, 0, 0);
    lineLoop(5.6, 9.6, 8.7, 6.2, 0, 0, 0);
    lineLoop(5.6, 9.6, 6.2, 3.7, 0, 0, 0);
    lineLoop(5.6, 9.6, 3.0, 1.1, 0, 0, 0);
    if (*game_state == 6 && *this_teamID != 0) {
        if (current_ship->type != 0) {
            // Information about currently selected actions
            // Movement
            if (*action_state == 0) {
                // Print how much speed points left
                glColor3f(0.0f, 0.0f, 0.0f);
                printText("Moves left: ", 6.4f, 9.05f);
                printNumber(*current_ship_speed, 7.4f, 9.05f);
                lineLoop(5.56, 5.94, 9.45, 8.85, 0.4, 0, 0);
            }
            // Attack
            else if (*action_state == 1) {
                // 1st type ship can shoot everywhere
                if(current_ship->type == 1){
                    glColor3f(0.0f, 0.0f, 0.0f);
                    printText("Range left: Infinite", 6.4f, 9.05f);
                }else {
                    // Print how much range points left
                    glColor3f(0.0f, 0.0f, 0.0f);
                    printText("Range left: ", 6.4f, 9.05f);
                    printNumber(*current_ship_range, 7.4f, 9.05f);
                }
                lineLoop(5.96, 6.34, 9.45, 8.85, 0.4, 0, 0);
            }
        }
        // Visualize buttons to switch between actions of the ship
        lineLoop(5.6, 5.9, 9.4, 8.9, 0, 0, 0);
        lineLoop(6.0, 6.3, 9.4, 8.9, 0, 0, 0);
        printText("1", 5.7, 9.05);
        printText("2", 6.1, 9.05);
        // calculating how much ally and enemy ships are not drowned
        uint8_t allyShips = 0;
        uint8_t enemyShips = 0;
        for (int i = 0; i != MAX_SHIPS; i++) {
           if (
               ships[i].team_id == *this_teamID ||
               ships[i].team_id == *this_teamID + 2
           ) {
               allyShips += 1;
           } else if (
               ships[i].team_id == ((*this_teamID == 1) ? 2 : 1) ||
               ships[i].team_id == ((*this_teamID == 1) ? 4 : 3)
           ) {
               enemyShips += 1;
           }
        }
        // printing how much ally and enemy ships are not drowned
        char team1[] = "Ally Team: X";
        char team2[] = "Enemy Team: X";
        team1[11] = allyShips + '0';
        team2[12] = enemyShips + '0';
        printText(team1, 5.6, 3.2);
        printText(team2, 8.4, 3.2);
    }

    glColor3f(0.0f, 0.8f, 0.0f);
    glRasterPos3f(1.8f, 9.0f, 0);
    // printing this player name
    for(int i = 0; i < strlen((char*)player_name) && i < MAX_PLAYER_NAME_LEN; i++){
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, player_name[i]);
    }

    // Battlefield overlay
    if(*menu_state == 0){
        lineLoop(0.35, 5.306, 8.75, 0.8, 0, 0, 0.4);
    }

    // Some text and suggestions
    glColor3f(0.3f, 0.3f, 0.3f);
    printText("Chat comming soon!", 5.8, 2.5);
    printText("Press 'ESC' to exit", 8.2, 0.5);
}

// Draw battlefield part of selected plane
void createPlane()
{
    // Print user list
    printConnectedUsers();
    // Show mini map
    quartalMap();
    // Show HUD
    printHUD();
    // Title
    if(*game_state == 4){
        glColor3f(0.0f, 0.0f, 0.4f);
        printText("Place your ships!", 2.2, 9.35);
    } else if (*game_state == 6){
        glColor3f(0.4f, 0.0f, 0.0f);
        printText("All Out WAR", 2.5, 9.35);
    }

    // Calculate x and y coefficient depending on current plane
    uint8_t coef_x = *plane % 4;
    uint8_t coef_y = *plane / 4;
    // Going thorugh battlefield and drawing its objects
    for(int x = 0; x < PLANE_SIZE; x++) {
        for (int y = 0; y < PLANE_SIZE; y++) {
            // Get current object
            uint8_t battlefield_object = battlefield[(x+PLANE_SIZE*coef_x)+(y+PLANE_SIZE*coef_y)*BATTLEFIELD_X_MAX];
            // Attack marker
            if (
                (current_attack_position->x != 0 && current_attack_position->y != 0) &&
                (current_attack_position->x == (x+PLANE_SIZE*coef_x) && current_attack_position->y == (y+PLANE_SIZE*coef_y))
            ) {
                filledCube(x, y, 1.0f, 0.0f, 0.0f);
            // Island object
            } else if (battlefield_object == (enum BattlefieldObj) Island) {
                filledCube(x, y, 0.3f, 0.2f, 0.2f);
            // Rocks object
            } else if (battlefield_object == (enum BattlefieldObj) Rocks) {
                filledCube(x, y, 0.15f, 0.15f, 0.15f);
            // Big fish object
            } else if (battlefield_object == (enum BattlefieldObj) Fish) {
                filledCube(x, y, 0.1f, 0.2f, 0.7f);
            // Hit object
            } else if (battlefield_object == (enum BattlefieldObj) Hit) {
                filledCube(x, y, 0.8f, 0.0f, 0.0f);
            // Miss object
            } else if (battlefield_object == (enum BattlefieldObj) HitNot) {
                filledCube(x, y, 0.4f, 0.4f, 0.6f);
            // Ships depending on team
            } else if (battlefield_object >= 1 && battlefield_object <= 5) {
                // Current active ship
                if (isItPlacingShip((x+PLANE_SIZE*coef_x), (y+PLANE_SIZE*coef_y))) {
                    filledCube(x, y, 0.0f, 0.5f, 0.0f);
                } else {
                    char is_our = isItOurTeamShip((x+PLANE_SIZE*coef_x), (y+PLANE_SIZE*coef_y));
                    // Ship from our team
                    if (is_our == 1) {
                        filledCube(x, y, 0.0f, 0.0f, 0.3f);
                    // Ship that appeared after collision
                    } else if (is_our == 2) {
                        filledCube(x, y, 0.8f, 0.5f, 0.5f);
                    }
                }
            // Other objects
            } else if (battlefield_object != 0) {
                filledCube(x, y, 1.0f, 1.0f, 1.0f);
            }
        }
    }
    glFlush();
    // Grid on battlefield
    outlineGrid();

    // Bottom information
    glColor3f(0.3f, 0.3f, 0.3f);
    printText("Movement:Arrow keys | Rotation:'<''>'| Menu:'TAB'", 0.42, 0.5);
}

// Reserve and share memory between all needed variables
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

    // Share memory between all needed variables
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

// gameloop to receive and process packages from server
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

// Client connection to the server
int clientConnect()
{
    // Create an address structure for client
    struct sockaddr_in remote_address;
    remote_address.sin_family = AF_INET;
    remote_address.sin_port = htons(PORT);
    inet_pton(AF_INET, HOST, &remote_address.sin_addr);

    // Create client socket
    int my_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (my_socket < 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // trying to connect to the server
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

// Process package sent by the server
void processPackage(uint8_t *msg)
{
    // Update package NPK
    *last_package_npk = getPackageNPK(msg, *is_little_endian);
    if (*last_package_npk >= UINT32_MAX) {
        *last_package_npk = 0;
    }

    // Process package depending on its type
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

// Find ship to be active by type and team id
struct ShipToPlace* findShipToPlace(struct ShipToPlace* ships_arr, uint8_t type, uint8_t team_id)
{
    for (int i = 0; i < MAX_SHIPS/2; i++) {
        if (ships_arr[i].type == type && ships_arr[i].team_id == team_id) {
            return &ships_arr[i];
        }
    }
    return NULL;
}

// Add all active ships to battlefield
void addShipsToPlace()
{
    for(int i = 0; i < MAX_SHIPS/2; i++){
        if(ships_to_place[i].placed == 0){
            continue;
        }
        // Get direction increments
        uint8_t dir = ships_to_place[i].dir;
        char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
        char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;
        // Add each part of the ship
        for(int a = 0; a < 6 - ships_to_place[i].type; a++){
            if (battlefield[(ships_to_place[i].x + a*dx) + (ships_to_place[i].y + a*dy) * BATTLEFIELD_X_MAX] != (enum BattlefieldObj) Hit) {
                battlefield[(ships_to_place[i].x + a*dx) + (ships_to_place[i].y + a*dy) * BATTLEFIELD_X_MAX] = ships_to_place[i].type;
            }
        }
    }
}

// remove active ship from battlefield
void removeShipsToPlace()
{
    for(int i = 0; i < MAX_SHIPS/2; i++){
        if(ships_to_place[i].placed == 0){
            continue;
        }
        // Get direction increments
        uint8_t dir = ships_to_place[i].dir;
        char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
        char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;
        // Remove each part of the ship
        for(int a = 0; a < 6 - ships_to_place[i].type; a++){
            if (battlefield[(ships_to_place[i].x + a*dx) + (ships_to_place[i].y + a*dy) * BATTLEFIELD_X_MAX] == ships_to_place[i].type) {
                battlefield[(ships_to_place[i].x + a*dx) + (ships_to_place[i].y + a*dy) * BATTLEFIELD_X_MAX] = 0;
            }
        }
    }
}

// Check if current active ship is in given coordinates
char isItPlacingShip(uint8_t x, uint8_t y)
{
    struct ShipToPlace* ship = findShipToPlace(ships_to_place, current_ship->type, *this_teamID);
    if (ship == NULL) {
        return 0;
    }
    // Get direction increments
    uint8_t dir = ship->dir;
    char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
    char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;
    // Check each part of the ship
    for (int i = 0; i < 6 - ship->type; i++) {
        if (ship->x + i*dx == x && ship->y + i*dy == y) {
            return 1;
        }
    }
    return 0;
}

// Check if ship in given coordinates is an ally
char isItOurTeamShip(uint8_t x, uint8_t y)
{
    // Ghost are allies
    if (*this_teamID == 0) {
        return 1;
    }

    // Check each ship in list
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (ships[i].type == current_ship->type && ships[i].team_id == current_ship->team_id) {
            continue;
        }
        // Get direction increments
        uint8_t dir = ships[i].dir;
        char dx = (dir == 1) ? 1 : (dir == 3) ? -1 : 0;
        char dy = (dir == 0) ? -1 : (dir == 2) ? 1 : 0;
        // Check each part of the ship
        for (int a = 0; a < 6 - ships[i].type; a++) {
            if ((ships[i].x + a*dx) == x && (ships[i].y + a*dy) == y) {
                // Ship from our team
                if (
                    ships[i].team_id == *this_teamID ||
                    ships[i].team_id == *this_teamID + 2
                ) {
                    return 1;
                }
                // Ship that appeared after collision
                else if (ships[i].team_id == ((*this_teamID == 1) ? 4 : 3)) {
                    return 2;
                }
            }
        }
    }
    return 0;
}

// Change plane to make current active ship visible on screen
void setPlaneToShip(struct ShipToPlace* ship_to_place)
{
    for (int x = 0; x < 4; x++) {
        for (int y = 0; y < 4; y++) {
            // Check each plane bounds
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

// Process ACK package
void pkgACK(uint8_t *msg, uint32_t content_size)
{
    uint8_t *content = getPackageContent(msg, content_size);

    // Save new id and team id for this player
    *this_ID = content[0];
    *this_teamID = content[1];

    // If id is 0, exit game (error)
    if (*this_ID == 0) {
        closeFunc();
    }

    // If team id is 0, then it is a spectator
    if (*this_teamID == 0) {
        *game_state = 4;
    }

    printf("ID: %d\nTEAM: %d\n", *this_ID, *this_teamID);
}

// Package LOBBY
void pkgLOBBY(uint8_t *msg, uint32_t content_size)
{
    // Change game state after Loading
    if (*game_state == 1) {
        *game_state = 2;
    }
    uint8_t *content = getPackageContent(msg, content_size);

    // Save received information about all players from server
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

// Package START_SETUP
void pkgSTART_SETUP(uint8_t *msg, uint32_t content_size)
{
    uint8_t *content = getPackageContent(msg, content_size);
    // Save battlfield sizes and set starting plane for each team
    *battlefield_x = content[0];
    *battlefield_y = content[1];
    *plane = (*this_teamID == 1) ? 0 : 3;

    // Move to the next game state
    *game_state = 3;
}

// Package STATE with all currently needed info for ship placement and game part itself
void pkgSTATE(uint8_t *msg, uint32_t content_size)
{
    uint8_t *content = getPackageContent(msg, content_size);

    uint32_t content_iter = 0;
    // Save battlefield sizes
    *battlefield_x = content[content_iter++];
    *battlefield_y = content[content_iter++];
    uint32_t battlefield_size = (*battlefield_x) * (*battlefield_y);

    int k = 0;
    // Battlefield itself with border from Rocks
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

    // Save information about ships
    for (int i = 0; i < sizeof(struct Ship) * MAX_SHIPS; i++) {
        *(((char*)ships) + i) = content[content_iter++];
    }

    // Amount of players
    *players_count = content[content_iter++];
    // Save players data
    for (int i = 0; i < sizeof(struct Player) * MAX_SHIPS; i++) {
        *(((char*)players) + i) = content[content_iter++];
    }
}

// Process package TEV_JALIEK
void pkgTEV_JALIEK(uint8_t *msg, uint32_t content_size)
{
    // Change game state if START_SETUP pakcage was not received and set plane
    if (*game_state == 2 || *game_state == 3) {
        *plane = (*this_teamID == 1) ? 0 : 3;
        *game_state = 4;
    }

    uint8_t *content = getPackageContent(msg, content_size);

    // if there was active ship make it inactive
    for (int i = 0; i < MAX_SHIPS / 2; i++) {
        if (ships_to_place[i].type != content[1]) {
            ships_to_place[i].placed = 0;
        }
    }

    // if there was current ship, but should not be any longer
    if (*this_ID != content[0]) {
        if (current_ship->type != 0) {
            current_ship->type = 0;
        }
        return;
    }

    // Only one time get information about ships of this player team from server
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

    // Do not add ship as active is it is active already or if it does not exist
    struct ShipToPlace* ship = findShipToPlace(ships_to_place, content[1], *this_teamID);
    if (ship == NULL || ship->placed == 1) {
        return;
    }

    // Set starting ship position and make it currently active
    ship->x = (*this_teamID == 1) ? (PLANE_SIZE / 2) : *battlefield_x - (PLANE_SIZE / 2);
    ship->y = PLANE_SIZE / 2;
    ship->placed = 1;
    *plane = (*this_teamID == 1) ? 0 : 3;
    current_ship->type = ship->type;
    current_ship->team_id = *this_teamID;
}

// Prepare package ES_LIEKU
void pkgES_LIEKU(uint8_t type, uint8_t x, uint8_t y, uint8_t dir)
{
    uint32_t content_size = 5;
    uint8_t msg[content_size];
    msg[0] = *this_ID;
    msg[1] = type;
    msg[2] = x;
    msg[3] = y;
    msg[4] = dir;

    // Prepare package with new ship position and send it to the server
    *last_package_npk += 2;
    uint8_t* pES_LIEKU = preparePackage(*last_package_npk, 8, msg, &content_size, content_size, *is_little_endian);
    write(*server_socket, pES_LIEKU, content_size);
}

// Process package START_GAME
void pkgSTART_GAME(uint8_t *msg, uint32_t content_size)
{
    // save battlfield sizes one more time and change game state
    uint8_t *content = getPackageContent(msg, content_size);
    *battlefield_x = content[0];
    *battlefield_y = content[1];

    *game_state = 6;
}

void pkgTEV_JAIET(uint8_t *msg, uint32_t content_size)
{
    // Change game state if START_GAME package was not received
    if (*game_state == 4) {
        *game_state = 6;
    }
    uint8_t *content = getPackageContent(msg, content_size);


    // if there was active ship make it inactive
    for (int i = 0; i < MAX_SHIPS / 2; i++) {
        if (ships_to_place[i].type != content[1]) {
            ships_to_place[i].placed = 0;
        }
    }

    // if there was current ship, but should not be any longer
    if (*this_ID != content[0]) {
        if (current_ship->type != 0) {
            current_ship->type = 0;
            current_attack_position->x = 0;
            current_attack_position->y = 0;
        }
        return;
    }

    // Get ship that must be active next
    struct Ship* ship = findShipByIdAndTeamId(ships, content[1], *this_teamID);
    if (ship == NULL) {
        // Try to get ship that became visible for enemy team
        ship = findShipByIdAndTeamId(ships, content[1], *this_teamID + 2);
        if (ship == NULL) {
            return;
        }
    }

    // Check not to make ship active when it is already
    if (current_ship->type == ship->type && current_ship->team_id == ship->team_id) {
        return;
    }

    // Take current active ship to handle its actions
    struct ShipToPlace* ship_to_place = findShipToPlace(ships_to_place, ship->type, *this_teamID);
    if (ship_to_place == NULL) {
        return;
    }

    // If ship is damaged or it is ship of type 1 it cannot move
    if (ship->damage > 0 || ship->type == 1) {
        *action_state = 1;

        // Ship of type 3 has torpedo, so it has different attack starting positions
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
    // Make ship active and differently visible on the battlefield
    ship_to_place->placed = 1;

    // Set plane to active ship
    setPlaneToShip(ship_to_place);

    // Update data of the current ship from server
    current_ship->type = ship->type;
    current_ship->x = ship->x;
    current_ship->y = ship->y;
    current_ship->dir = ship->dir;
    current_ship->team_id = ship->team_id;
    current_ship->damage = ship->damage;
    // Get predefiened information about ship
    getShipData(ship->type, current_ship_speed, current_ship_range, current_ship_is_dir);
}

// Prepare package GAJIENS
void pkgGAJIENS(uint8_t action, uint8_t x, uint8_t y, uint8_t thing)
{
    uint32_t content_size = 5;
    uint8_t msg[content_size];
    msg[0] = *this_ID;
    msg[1] = action;
    msg[2] = x;
    msg[3] = y;
    msg[4] = thing;

    // Send this player id and action type with coordinates of action and some additional value
    *last_package_npk += 2;
    uint8_t* pES_LIEKU = preparePackage(*last_package_npk, 11, msg, &content_size, content_size, *is_little_endian);
    write(*server_socket, pES_LIEKU, content_size);
}

// Process package END_GAME
void pkgEND_GAME(uint8_t *msg, uint32_t content_size)
{
    // Save winner team id and switch to the last game view
    uint8_t *content = getPackageContent(msg, content_size);
    *winner_team = content[1];
    *game_state = 8;
}
