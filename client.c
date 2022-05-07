#include <stdio.h>
#include <stdlib.h>
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
uint8_t *current_ship_range = NULL;
uint8_t *current_ship_is_dir = NULL;

/// Functions in thit file
void glMain(int argc, char *argv[]);
void display();
void specialKeyboard(int key, int x, int y);
void keyboard(unsigned char key, int x, int y);
void closeFunc();
void printText(char *text, double x, double y);
void lobby(char *not_ready, char *ready);
void printConnectedUsers();
void inputField();
void outlineCube();
void filledCube(int x, int y, double r, double g, double b);
void quartalMap();
void createPlane();

void getSharedMemory();
void gameloop();
int clientConnect();
void processPackage(uint8_t *msg);

struct ShipToPlace* findShipToPlace(struct ShipToPlace* ships, uint8_t type, uint8_t team_id);
void addShipsToPlace();
char checkEnemyShip(uint8_t x, uint8_t y);

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

void resize(int width, int height) {
    glutReshapeWindow(1400, 800);
    //glutPositionWindow(100, 100);
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
    }
    else if (*game_state == 2) {
        lobby("Not Ready", "Ready");
    }
    else if (*game_state == 4 || *game_state == 6) {
        createPlane();
    }
    glutSwapBuffers();
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
    }else if(*game_state == 4 && *menu_state == 0){
        struct ShipToPlace* ship = findShipToPlace(ships_to_place, current_ship->type, *this_teamID);
        if (ship == NULL) {
            return;
        }

        if (key == GLUT_KEY_UP) {
            if (
                (ship->y - ((ship->dir == 0) ? 6 - ship->type - 1 : 0)) <= 0
            ) {
                return;
            }
            ship->y -= 1;
        }
        else if (key == GLUT_KEY_DOWN) {
            if (
                (ship->y + ((ship->dir == 2) ? 6 - ship->type - 1 : 0)) >= *battlefield_y
            ) {
                return;
            }
            ship->y += 1;
        }
        else if (key == GLUT_KEY_LEFT) {
            if (
                ((*this_teamID == 1 && (ship->x - ((ship->dir == 3) ? 6 - ship->type - 1 : 0)) <= 0) ||
                (*this_teamID == 2 && (ship->x - ((ship->dir == 3) ? 6 - ship->type - 1 : 0)) <= (*battlefield_x / 2)))
            ) {
                return;
            }
            ship->x -= 1;
        }
        else if (key == GLUT_KEY_RIGHT) {
            if (
                ((*this_teamID == 1 && (ship->x + ((ship->dir == 1) ? 6 - ship->type - 1 : 0)) >= (*battlefield_x / 2)) ||
                (*this_teamID == 2 && (ship->x + ((ship->dir == 1) ? 6 - ship->type - 1 : 0)) >= *battlefield_x))
            ) {
                return;
            }
            ship->x += 1;
        }
    }
    else if(*game_state == 6 && *menu_state == 0){
        if(key == '1'){
            *action_state = 0;
        }
        else if(key == '2'){
            *action_state = 1;
        }
        else if(key == '3'){
            *action_state = 2;
        }

        struct ShipToPlace* ship = findShipToPlace(ships_to_place, current_ship->type, *this_teamID);
        if(ship == NULL){
            return;
        }

        if(*action_state == 0){
            if (key == GLUT_KEY_UP) {
                if ((ship->y - ((ship->dir == 0) ? 6 - ship->type - 1 : 0)) <= 0) {
                    return;
                }

                if (current_ship->y >= ship->y) {
                    if (*current_ship_speed > 0) {
                        ship->y -= 1;
                        *current_ship_speed -= 1;
                    }
                }
                else if (current_ship->y <= ship->y) {
                    ship->y -= 1;
                    *current_ship_speed += 1;
                }
            }else if (key == GLUT_KEY_DOWN) {
                if ((ship->y + ((ship->dir == 2) ? 6 - ship->type - 1 : 0)) >= *battlefield_y) {
                    return;
                }

                if (current_ship->y <= ship->y) {
                    if (*current_ship_speed > 0) {
                        ship->y += 1;
                        *current_ship_speed -= 1;
                    }
                }
                else if (current_ship->y >= ship->y) {
                    ship->y += 1;
                    *current_ship_speed += 1;
                }
            }else if (key == GLUT_KEY_LEFT) {
                if ((ship->x - ((ship->dir == 3) ? 6 - ship->type - 1 : 0)) <= 0) {
                    return;
                }

                if (current_ship->x >= ship->x) {
                    if (*current_ship_speed > 0) {
                        ship->x -= 1;
                        *current_ship_speed -= 1;
                    }
                }
                else if (current_ship->x <= ship->x) {
                    ship->x -= 1;
                    *current_ship_speed += 1;
                }
            }else if (key == GLUT_KEY_RIGHT) {
                if ((ship->x + ((ship->dir == 1) ? 6 - ship->type - 1 : 0)) >= *battlefield_x) {
                    return;
                }

                if (current_ship->x <= ship->x) {
                    if (*current_ship_speed > 0) {
                        ship->x += 1;
                        *current_ship_speed -= 1;
                    }
                }
                else if (current_ship->x >= ship->x) {
                    ship->x += 1;
                    *current_ship_speed += 1;
                }
            }
        }
    }

    *need_redisplay = 1;
}

void keyboard(unsigned char key, int x, int y)
{
    if(*game_state == 0){
        if(key == 13){
            if (player_name[0] == 0) {
                return;
            }

            *game_state = 1;
        }else if(key == 8){
            for(int i = 0;i < 32; i++){
                if(player_name[i] == '\0'){
                    player_name[i-1] = '\0';
                    *player_name_len = i-1;
                    break;
                }
            }
        }else{
            for(int i = 0;i < 32; i++){
                if(player_name[i] == '\0'){
                    player_name[i] = key;
                    *player_name_len = i+1;
                    break;
                }
            }
        }
    }
    else if (*game_state == 2) {
        if(key == 'r') {
            uint8_t msg[2];
            msg[0] = *this_ID;
            msg[1] = (findPlayerById(players, *this_ID)->is_ready == 1) ? 0 : 1;
            uint32_t content_size = 2;
            *last_package_npk += 2;
            uint8_t* pENTER = preparePackage(*last_package_npk, 4, msg, &content_size, content_size, *is_little_endian);
            write(*server_socket, pENTER, content_size);
        }
    }else if(*game_state == 4) {
        struct ShipToPlace* ship = findShipToPlace(ships_to_place, current_ship->type, *this_teamID);
        if (ship == NULL) {
            return;
        }
        if (key == ',') {
            if ((
                ship->dir == 0 &&
                ((*this_teamID == 1 && ship->x - (6 - ship->type - 1) < 0) ||
                (*this_teamID == 2 && ship->x - (6 - ship->type - 1) <= (*battlefield_x / 2)))
            ) || (
                ship->dir == 1 &&
                ship->y - (6 - ship->type - 1) < 0
            ) || (
                ship->dir == 2 &&
                ((*this_teamID == 1 && ship->x + (6 - ship->type - 1) > (*battlefield_x / 2)) ||
                (*this_teamID == 2 && ship->x + (6 - ship->type - 1) > *battlefield_x))
            ) || (
                ship->dir == 3 &&
                ship->y + (6 - ship->type - 1) > *battlefield_y
            )) {
                return;
            }

            ship->dir = (ship->dir == 0) ? 3 : ship->dir - 1;
        }
        else if(key == '.') {
            if ((
                ship->dir == 0 &&
                ((*this_teamID == 1 && ship->x + (6 - ship->type - 1) > (*battlefield_x / 2)) ||
                (*this_teamID == 2 && ship->x + (6 - ship->type - 1) > *battlefield_x))
            ) || (
                ship->dir == 1 &&
                ship->y + (6 - ship->type - 1) > *battlefield_y
            ) || (
                ship->dir == 2 &&
                ((*this_teamID == 1 && ship->x - (6 - ship->type - 1) < 0) ||
                (*this_teamID == 2 && ship->x - (6 - ship->type - 1) <= (*battlefield_x / 2)))
            ) || (
                ship->dir == 3 &&
                ship->y - (6 - ship->type - 1) < 0
            )) {
                return;
            }

            ship->dir = (ship->dir == 3) ? 0 : ship->dir + 1;
        }
        else if(key == 13) {
            pkgES_LIEKU(ship->type, ship->x, ship->y, ship->dir);
        }
    }
    if (*game_state == 4 || *game_state == 6) {
        if(key == 9){
            *menu_state += 1;
            if(*menu_state >= 2){
                *menu_state = 0;
            }
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
    glColor3f(0.0f, 0.0f, 0.0f);
    glRasterPos3f(x, y, 0);
    while(*text){
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, *text++);
    }
    glFlush();
}

void lobby(char *not_ready, char *ready)
{
    glColor3f(0.0f, 0.0f, 0.0f);
    glRasterPos3f(4.0, 9.0, 0);
    printText("Lobby", 4.5, 9);
    for(int i = 0; i < MAX_PLAYERS; i++){
        if (players[i].id == 0) {
            continue;
        }

        glColor3f(0.0f, 0.0f, 0.0f);
        glRasterPos3f(1.0, 8.0-(0.3*i), 0);
        for(int a = 0; a < strlen(players[i].name); a++){
            glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
        }

        if(players[i].is_ready == 1){
            glColor3f(0.0f, 1.0f, 0.0f);
            glRasterPos3f(8.0, 8.0-(0.3*i), 0);
            for(int a = 0; a < strlen(ready); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, ready[a]);
            }
        }else{
            glColor3f(1.0f, 0.0f, 0.0f);
            glRasterPos3f(8.0, 8.0-(0.3*i), 0);
            for(int a = 0; a < strlen(not_ready); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, not_ready[a]);
            }
        }
        glFlush();
    }
}

void printConnectedUsers(){
    for(int i = 0; i < MAX_PLAYERS; i++){
        if (players[i].id == 0) {
            continue;
        }

        glRasterPos3f(6.5, 9.0-(0.3*i), 0);
        if(players[i].team_id == 1){
            glColor3f(0.0f, 0.0f, 1.0f);
            for(int a = 0; a < strlen(players[i].name); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
            }
        }else{
            glColor3f(1.0f, 0.0f, 0.0f);
            for(int a = 0; a < strlen(players[i].name); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
            }
        }
    }
    glFlush();
}

void inputField()
{
    glClear(GL_COLOR_BUFFER_BIT);
    printText("Enter your name", 4.5, 9);
    glColor3f(0.0f, 0.0f, 0.0f);
    glRasterPos3f(3, 8.2, 0);
    for(int i = 0; i < 32 || player_name[i] != '\0'; i++){
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, player_name[i]);
    }
    glFlush();
}

void outlineCube()
{
    glColor3f(0.0f, 0.0f, 0.0f);
    for(int i = 0; i < 65; i ++){
        glBegin(GL_LINES);
            glVertex2f(0.425, 8.62-(0.12*i));
            glVertex2f(0.0817*64, 8.62-(0.12*i));
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

void quartalMap()
{
    glColor3f(0.0f, 0.0f, 0.0f);
    for(int y = 0; y < 4;  y++){
        for(int x = 0; x < 4; x++){
            glBegin(GL_LINE_LOOP);
                glVertex3f(0.5+(x*0.1), 9.6-(y*0.2), 0.0);
                glVertex3f(0.6+(x*0.1), 9.6-(y*0.2), 0.0);
                glVertex3f(0.6+(x*0.1), 9.4-(y*0.2), 0.0);
                glVertex3f(0.5+(x*0.1), 9.4-(y*0.2), 0.0);
            glEnd();
            glFlush();
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
        }
    }
}

void createPlane()
{
    printConnectedUsers();
    outlineCube();
    quartalMap();
    if(*game_state == 4){
        printText("Battlefield", 2.5, 9.5);
    }
    else if (*game_state == 6){
        printText("All Out WAR", 2.5, 9.5);
    }

    uint8_t koef_x = 0;
    uint8_t koef_y = 0;
    if (*plane == 1 || *plane == 5 || *plane == 9 || *plane == 13) {
        koef_x = 1;
    }
    else if (*plane == 2 || *plane == 6 || *plane == 10 || *plane == 14) {
        koef_x = 2;
    }
    else if (*plane == 3 || *plane == 7 || *plane == 11 || *plane == 15) {
        koef_x = 3;
    }

    if (*plane == 4 || *plane == 5 || *plane == 6 || *plane == 7) {
        koef_y = 1;
    }
    else if (*plane == 8 || *plane == 9 || *plane == 10 || *plane == 11) {
        koef_y = 2;
    }
    else if (*plane == 12 || *plane == 13 || *plane == 14 || *plane == 15) {
        koef_y = 3;
    }

    for(int x = 0; x < 64; x++) {
        for (int y = 0; y < 64; y++) {
            uint8_t battlefield_object = battlefield[(x+64*koef_x)+(y+64*koef_y)*BATTLEFIELD_X_MAX];
            if(battlefield_object != 0){
                filledCube(x, y, 0.0, 0.0, 0.0);
            }
        }
    }
    glFlush();
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
    current_ship_range = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    current_ship_is_dir = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
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
    }
    else if (msg_type == 3) {
        pkgLOBBY(msg, getPackageContentSize(msg, *is_little_endian));
    }
    else if (msg_type == 5) {
        pkgSTART_SETUP(msg, getPackageContentSize(msg, *is_little_endian));
    }
    else if (msg_type == 6) {
        pkgSTATE(msg, getPackageContentSize(msg, *is_little_endian));
        addShipsToPlace();
    }
    else if (msg_type == 7) {
        pkgTEV_JALIEK(msg, getPackageContentSize(msg, *is_little_endian));
    }
    else if (msg_type == 9) {
        pkgSTART_GAME(msg, getPackageContentSize(msg, *is_little_endian));
    }
    else if (msg_type == 10) {
        pkgTEV_JAIET(msg, getPackageContentSize(msg, *is_little_endian));
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
            battlefield[(ships_to_place[i].x + a*dx) + (ships_to_place[i].y + a*dy) * BATTLEFIELD_X_MAX] = ships_to_place[i].type;
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

    *game_state = 4;
}

void pkgSTATE(uint8_t *msg, uint32_t content_size)
{
    uint8_t *content = getPackageContent(msg, content_size);

    uint32_t battlefield_size = content[0] * content[1];
    uint32_t content_iter = 2;

    int k = 0;
    for (; k < battlefield_size; k++) {
        battlefield[k] = content[content_iter++];
    }
    for (; k < (sizeof(uint8_t) * (BATTLEFIELD_X_MAX+1) * (BATTLEFIELD_Y_MAX+1)); k++) {
        battlefield[k] = 0;
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
    if (*game_state == 2) {
        *plane = (*this_teamID == 1) ? 0 : 3;
        *game_state = 4;
    }

    uint8_t *content = getPackageContent(msg, content_size);

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
    ship->x = (*this_teamID == 1) ? 32 : *battlefield_x - 32;
    ship->y = 32;
    ship->placed = 1;
    *plane = (*this_teamID == 1) ? 0 : 3;
    current_ship->type = ship->type;
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

    if (*this_ID != content[0]) {
        return;
    }

    struct Ship* ship = findShipByIdAndTeamId(ships, content[1], *this_teamID);
    if (ship == NULL) {
        return;
    }

    if (current_ship->type == ship->type && current_ship->team_id == ship->team_id) {
        return;
    }

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
    uint8_t* pES_LIEKU = preparePackage(*last_package_npk, 8, msg, &content_size, content_size, *is_little_endian);
    write(*server_socket, pES_LIEKU, content_size);
}

void pkgEND_GAME(uint8_t *msg, uint32_t content_size)
{
    uint8_t *content = getPackageContent(msg, content_size);

    // content[0] - winner id
    // content[1] - winner team_id
    // TODO end game somehow
}
