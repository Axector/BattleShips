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

char *shared_memory = NULL;
char *plane = NULL;
char *battlefield = NULL; //temporary
char *need_redisplay = NULL;
uint8_t *player_name_len = NULL;
char *playerName = NULL;
char *nameIsReady = NULL;
char *to_exit = NULL;
int *server_socket = NULL;
unsigned char *game_state = NULL;
char *is_little_endian = NULL;
uint8_t *this_ID = NULL;
uint8_t *this_teamID = NULL;
uint32_t *last_package_npk = NULL;      // NPK for packages
uint8_t *players_count = NULL;
struct Player *players = NULL;

struct Player* findPlayerById(uint8_t id)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].id == id) {
            return &players[i];
        }
    }
    return NULL;
}

void specialKeyboard(int key, int x, int y)
{
    /*if(!players_count == 0){
        if(key == 8){ //backspace
            for(int i = 0;i < 32 || playerName[i] != '\0'; i++){
                if(playerName[i+1] == NULL && playerName[i] != NULL){
                    playerName[i] = key;
                    printf('%s', playerName);
                    break;
                }
            }
        }
    }*/
}

void keyboard(unsigned char key, int x, int y)
{
    if(*nameIsReady == 0){
        if(key == 13){
            *nameIsReady = 1;
        }else{
            for(int i = 0;i < 32; i++){
                if(playerName[i] == '\0'){
                    playerName[i] = key;
                    *player_name_len = i+1;
                    break;
                }
            }
        }
    }else{
        if(key == 'r'){
            char msg[2];
            msg[0] = *this_ID;
            msg[1] = (findPlayerById(*this_ID)->is_ready == 1) ? 0 : 1;
            uint32_t content_size = 2;
            *last_package_npk += 1;
            char* pENTER = preparePackage(*last_package_npk, 4, msg, &content_size, content_size, *is_little_endian);
            write(*server_socket, pENTER, content_size);
        }
    }
    switch (key) {
        case 9: {
            if(*plane != 16){
                *plane = *plane + 1;
            }else{
                *plane = 0;
            }
            break;
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
    glColor3f(0.0f, 0.5f, 0.5f);
    glRasterPos3f(x, y, 0);
    while(*text){
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, *text++);
    }
    glFlush();
}

void printConnectedUsers(char * not_ready, char* ready)
{
    for(int i = 0; i < *players_count; i++){
        glColor3f(0.0f, 0.5f, 0.5f);
        glRasterPos3f(1.0, 9.0-(0.3*i), 0);
        for(int a = 0; a < strlen(players[i].name); a++){
            glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, players[i].name[a]);
        }

        glRasterPos3f(7.0, 9.0-(0.3*i), 0);
        if(players[i].is_ready == 1){
            for(int a = 0; a < strlen(ready); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, ready[a]);
            }
        }else{
            for(int a = 0; a < strlen(not_ready); a++){
                glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, not_ready[a]);
            }
        }
        glFlush();
    }
}

void inputField()
{
    glClear(GL_COLOR_BUFFER_BIT);
    printText("Enter your name", 4.5, 9);
    glColor3f(0.0f, 0.5f, 0.5f);
    glRasterPos3f(3, 8.2, 0);
    for(int i = 0; i < 32 || playerName[i] != '\0'; i++){
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, playerName[i]);
    }
    glFlush();
}

void outlineCube()
{
    glColor3f(0.0f, 0.5f, 0.5f);
    for(int i = 0; i < 64; i ++){
        glBegin(GL_LINES);
            glVertex2f(0.5, 8.5-(0.12*i));
            glVertex2f(0.0964*64, 8.5-(0.12*i));
        glEnd();
    }
    for(int i = 0; i < 64; i ++){
        glBegin(GL_LINES);
            glVertex2f(0.5+(0.09*i), 8.5);
            glVertex2f(0.5+(0.09*i), 0.94);
        glEnd();
    }
    glFlush();
}

void filledCube(int x, int y)
{
    glColor3f(0.0f, 0.5f, 0.5f);
    glBegin(GL_QUADS);
        glVertex3f(0.41+(x*0.09), 8.64-(y*0.12), 0.0);
        glVertex3f(0.5+(x*0.09), 8.64-(y*0.12), 0.0);
        glVertex3f(0.5+(x*0.09), 8.50-(y*0.12), 0.0);
        glVertex3f(0.41+(x*0.09), 8.50-(y*0.12), 0.0);
    glEnd();
    glFlush();
}

void quartalMap()
{
    glColor3f(0.0f, 0.5f, 0.5f);
    for(int y = 0; y < 4;  y++){
        for(int x = 0; x < 4; x++){
            glBegin(GL_LINE_LOOP);
                glVertex3f(0.5+(x*0.1), 9.6-(y*0.20), 0.0);
                glVertex3f(0.6+(x*0.1), 9.6-(y*0.20), 0.0);
                glVertex3f(0.6+(x*0.1), 9.0-(y*0.20), 0.0);
                glVertex3f(0.5+(x*0.1), 9.0-(y*0.20), 0.0);
            glEnd();
            glFlush();
        }
    }
}

void createPlane()
{
    glClear(GL_COLOR_BUFFER_BIT);
    outlineCube();
    quartalMap();
    printText("Battlefield", 4.5, 9.5);
    for(int i = 0; i < 4; i++){
        battlefield[9*256+(i+1)] = 1;
    }
    battlefield[72*256+92] = 1;
    battlefield[240*256+243] = 1;
    
    for(int i = 0; i < 64; i++) {
        for (int a = 0; a < 64; a++){
            if(battlefield[(i+64*(*plane))*256+(a+64*(*plane))] == 1){
                filledCube(a, i);
            }
        }
    }
}

void display()
{
    glClearColor(0.0f, 1.0f, 1.0f, 0.1f);
    glClear(GL_COLOR_BUFFER_BIT);
    if(*nameIsReady == 0){
        inputField();
    }else{
        printConnectedUsers("Not Ready", "Ready");
        //createPlane();
    }
    glutSwapBuffers();
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
}

void getSharedMemory();
void gameloop();
int clientConnect();
void processPackage(char *msg);

// Package types
void pkgACK(char *msg, uint32_t content_size);      // 1
void pkgLOBBY(char *msg, uint32_t content_size);    // 3

int main(int argc, char *argv[])
{
    getSharedMemory();
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

    while(*nameIsReady == 0) {
        if (*to_exit == 1) {
            exit(0);
        }
    }

    *server_socket = clientConnect();

    // Check if current system is little-endian
    *is_little_endian = isLittleEndianSystem();

    uint32_t content_size = *player_name_len;
    char* pLABDIEN = preparePackage(*last_package_npk, 0, playerName, &content_size, MAX_PLAYER_NAME_LEN, *is_little_endian);
    write(*server_socket, pLABDIEN, content_size);

    char input[MAX_PACKAGE_SIZE];
    uint32_t nread = read(*server_socket, input, MAX_PACKAGE_SIZE);
    if(unpackPackage(input, nread, *last_package_npk, *is_little_endian) == -1) {
        exit(0);
    }
    processPackage(input);

    gameloop();
    exit(0);
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
    plane = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    *plane = 0;
    player_name_len = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    need_redisplay = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    playerName = (char*) (shared_memory + shared_size); shared_size += sizeof(char) * MAX_PLAYER_NAME_LEN;
    nameIsReady = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    to_exit = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    server_socket = (int*) (shared_memory + shared_size); shared_size += sizeof(int);
    game_state = (unsigned char*) (shared_memory + shared_size); shared_size += sizeof(char);
    is_little_endian = (char*) (shared_memory + shared_size); shared_size += sizeof(char);
    this_ID = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    this_teamID = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    last_package_npk = (uint32_t*) (shared_memory + shared_size); shared_size += sizeof(uint32_t);
    players_count = (uint8_t*) (shared_memory + shared_size); shared_size += sizeof(uint8_t);
    players = (struct Player*) (shared_memory + shared_size); shared_size += sizeof(struct Player) * MAX_PLAYERS;
    battlefield = (char*) (shared_memory + shared_size); shared_size += sizeof(char) * 256 * 256;
}

void gameloop()
{
    while (1) {
        if (*to_exit == 1) {
            exit(0);
        }

        char input[MAX_PACKAGE_SIZE];
        uint32_t nread = read(*server_socket, input, MAX_PACKAGE_SIZE);
        if(unpackPackage(input, nread, *last_package_npk, *is_little_endian) == -1) {
            continue;
        }
        processPackage(input);
    }
}

void processPackage(char *msg)
{
    *last_package_npk = getPackageNPK(msg, *is_little_endian);
    if (*last_package_npk >= UINT32_MAX) {
        *last_package_npk = 0;
    }

    switch (getPackageType(msg)) {
        case 1: {
            pkgACK(msg, getPackageContentSize(msg, *is_little_endian));
            break;
        }
        case 3: {
            pkgLOBBY(msg, getPackageContentSize(msg, *is_little_endian));
            break;
        }
    }

    *need_redisplay = 1;
}

void pkgACK(char *msg, uint32_t content_size)
{
    char *content = getPackageContent(msg, content_size);

    *this_ID = content[0];
    *this_teamID = content[1];

    if (*this_ID == 0) {
        closeFunc();
    }

    printf("ID: %d\nTEAM: %d\n", *this_ID, *this_teamID);
}

void pkgLOBBY(char *msg, uint32_t content_size)
{
    char *content = getPackageContent(msg, content_size);
    *players_count = content[0];

    struct Player *p = (struct Player*) (content + sizeof(uint8_t));
    for (int i = 0; i < MAX_PLAYERS; i++) {
        players[i] = p[i];
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
