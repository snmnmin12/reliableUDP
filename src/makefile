CC=g++
CXXFLAGS=-std=c++11 -Wall
OUT=main
SRC=SenderSocket.cpp main.cpp checksum.cpp

all: main

main: $(SRC)
	$(CC) -g $(CXXFLAGS) $(SRC) -o $(OUT) -pthread
 
clean:
	rm -f $(OUT)  #test