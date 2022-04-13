#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <GL/glew.h>
#include <GL/freeglut.h>

#define HOST "127.0.0.1" /* localhost */
#define PORT 12345

char data[100];

void display()
{
    glClearColor(0.5f, 0.0f, 0.5f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glutSwapBuffers();
}

void mouse(int btn, int state, int x, int y)
{
    if (btn == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        sprintf(data, "[%d, %d]", x, y);
    }
}

void keyboard(unsigned char key, int x, int y) {
    sprintf(data, "%d - [%d, %d]", (int)key, x, y);
}

void specKeyboard(int key, int x, int y) {
    sprintf(data, "%d - [%d, %d]", key, x, y);
}

void glMain(int argc, char *argv[])
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(480, 320);
    glutInitWindowPosition(100, 100);
    glutCreateWindow("PROJECT");
    glutDisplayFunc(display);
    glutMouseFunc(mouse);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(specKeyboard);
}

void gameloop(int socket);
int clientConnect();

int main(int argc, char *argv[])
{
    int my_socket = clientConnect();

    glMain(argc, argv);
    gameloop(my_socket);

    exit(0);
}

void gameloop(int socket)
{
    char inputs[100];
    data[0] = 0;

    while (1) {
        glutMainLoopEvent();

        if (data[0] == 0) {
            continue;
        }

        strcpy(inputs, data);
        write(socket, inputs, strlen(inputs));
        data[0] = 0;
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
