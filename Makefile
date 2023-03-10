compile: server_compile client_compile

server_compile:
	gcc server.c utils.c -Wall -o server -lm

client_compile:
	gcc client.c utils.c -Wall -o client -lglut -lGL -lGLEW

server: server_compile
	./server

client: client_compile
	./client
