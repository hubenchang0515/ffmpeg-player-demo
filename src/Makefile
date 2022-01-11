.PHONY: all install clean

all: player

install: all

clean:
	rm -f main.o decoder.o

player : main.o decoder.o  
	gcc -o $@ $^ -lSDL2 -lavformat -lavcodec -lavutil -lswscale 

main.o: main.c decoder.h
	gcc -c main.c -O2 -W -Wall 

decoder.o: decoder.c decoder.h
	gcc -c decoder.c -O2 -W -Wall 

