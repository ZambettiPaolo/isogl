CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -I./include
LIBS = -lSDL2 -lGL -lm -ldl

TARGET = myopengl
SRC = main.c R_GL.c glad.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)  $(LIBS)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run

