Authors:
- Daniils Leščišins - worked on server side, client side and their communication
- Rihards Dāvids - worked on visual part

There is all information you need to run this project. (Linux)

Needed libraries:
- glut        - sudo apt install freeglut3-dev

Using WSL it is necessary to download XLaunch and tick "Disable access control"

Commands to run the program:

(using make)
- server compilation and run
    make server

- client compilation and run
    make client

(without make)
- server compilation
    gcc server.c utils.c -Wall -o server -lm

- client compilation
    gcc client.c utils.c -Wall -o client -lglut -lGL -lGLEW

- server launch
    ./server

- client launch
    ./client
