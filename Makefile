CC     = gcc
CFLAGS = -Wall -Wextra
TARGET = my_ps
SRC    = main.c

all: $(TARGET)

$(TARGET): $(SRC) proc.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

valgrind: $(TARGET)
	valgrind --leak-check=full ./$(TARGET)

.PHONY: all clean valgrind
