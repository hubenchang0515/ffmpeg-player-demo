.PHONY: all install clean

all: player

install: all

clean:
	rm -f main.o queue.o decoder.o

player : main.o queue.o decoder.o  
	gcc -o $@ $^ -lSDL2 -lavformat -lavcodec -lavutil -lswscale 

main.o: main.c queue.h decoder.h
	gcc -c main.c -O2 -W -Wall -g 

queue.o: queue.c queue.h
	gcc -c queue.c -O2 -W -Wall -g 

decoder.o: decoder.c decoder.h queue.h
	gcc -c decoder.c -O2 -W -Wall -g 

