README
Projektu veidoja:
Daniils Leščišins DL19044 - veidoja serveri un visu tā sadarbību ar klientu, kā arī daudz palīdzēja Rihardam
Rihards Dāvids RD19020 - veidoja klientu, ka arī gandrīz visu vizuālo daļu

Projekts:
Zemāk norādīta visa informācija, lai palaistu projektu.

Vajadzīgās bibliotēkas:
- glut, to var instalēt ar komandu - sudo apt-get install freeglut3-dev

Kā arī vajadzīgs XLaunch priekš palaišanas uz Windows, izmantojot WSL, iespējams, ka nepieciešams palaišanas brīdī atzīmēt - "Disable access control".

Komandas, lai palaistu programmu:

(ar make)
- servera kompilēšana un sākšana
    make server

- klienta kompilēšana un palaišana
    make client

(bez make)
- servera kompilēšana
    gcc server.c utils.c -Wall -o server -lm

- klienta kompilēšana
    gcc client.c utils.c -Wall -o client -lglut -lGL -lGLEW

- server sākšāna
    ./server

- klienta palaišana
    ./client
