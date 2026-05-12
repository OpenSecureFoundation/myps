CC      = gcc
CFLAGS  = -Wall -O2
TARGET  = myps
PREFIX  = /usr/local/bin

all: $(TARGET)

$(TARGET): myps.c
	$(CC) $(CFLAGS) -o $(TARGET) myps.c

install: $(TARGET)
	sudo cp $(TARGET) $(PREFIX)/$(TARGET)
	@echo "myps installé — tu peux maintenant taper: myps [options]"

uninstall:
	sudo rm -f $(PREFIX)/$(TARGET)
	@echo "myps désinstallé"

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean