CC      = gcc
CFLAGS  = -Wall -O2
TARGET  = myps
PREFIX  = /usr/local/bin

SRCS    = main.c proc_reader.c filters.c display.c parse_args.c membre2.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install: $(TARGET)
	sudo cp $(TARGET) $(PREFIX)/$(TARGET)
	@echo "myps installé — tape: myps [options]"

uninstall:
	sudo rm -f $(PREFIX)/$(TARGET)
	@echo "myps désinstallé"

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all install uninstall clean