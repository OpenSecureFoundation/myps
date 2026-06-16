# ==============================================================
# Makefile — myps
# Cibles : all, install, uninstall, clean, distclean, run, help
# ==============================================================

# --- Compilateur et flags ---
CC      = gcc
CFLAGS  = -Wall -Wextra -std=gnu11 -O2 
LDFLAGS =

# --- Noms ---
TARGET  = myps
SRC     = myps.c

# --- Répertoires d'installation ---
# Installe dans le répertoire courant du projet par défaut.
# Pour installer globalement : make install PREFIX=/usr/local
PREFIX  = $(shell pwd)/..
BINDIR  = $(PREFIX)/bin

# ==============================================================
# Cibles principales
# ==============================================================

.PHONY: all install uninstall clean distclean run help

## all : compile le binaire (défaut)
all: $(TARGET)

$(TARGET): $(SRC)
	@echo "[CC]  Compilation de $(SRC) ..."
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)
	@echo "[OK]  Binaire '$(TARGET)' créé."

## install : compile puis copie le binaire dans $(BINDIR)
install: all
	@echo "[INSTALL]  Copie de $(TARGET) vers $(BINDIR) ..."
	@mkdir -p $(BINDIR)
	cp $(TARGET) $(BINDIR)/$(TARGET)
	chmod 755 $(BINDIR)/$(TARGET)
	@echo "[OK]  '$(TARGET)' installé dans $(BINDIR)."
	@echo "      Ajoutez $(BINDIR) à votre PATH si nécessaire."

## uninstall : supprime le binaire installé
uninstall:
	@echo "[UNINSTALL]  Suppression de $(BINDIR)/$(TARGET) ..."
	@if [ -f "$(BINDIR)/$(TARGET)" ]; then \
		rm -f $(BINDIR)/$(TARGET); \
		echo "[OK]  '$(TARGET)' supprimé de $(BINDIR)."; \
	else \
		echo "[INFO]  '$(TARGET)' n'est pas installé dans $(BINDIR)."; \
	fi

## clean : supprime le binaire local
clean:
	@echo "[CLEAN]  Suppression du binaire local ..."
	rm -f $(TARGET)
	@echo "[OK]  Nettoyage terminé."

## distclean : clean + supprime les fichiers temporaires
distclean: clean
	@echo "[DISTCLEAN]  Suppression des fichiers temporaires ..."
	rm -f *.o *~ core
	@echo "[OK]  Distclean terminé."

## run : compile et exécute myps avec les options passées via ARGS
##       exemple : make run ARGS="-A -F"
run: all
	@echo "[RUN]  ./$(TARGET) $(ARGS)"
	@./$(TARGET) $(ARGS)

## help : affiche cette aide
help:
	@echo ""
	@echo "Usage : make [cible] [OPTIONS]"
	@echo ""
	@echo "Cibles disponibles :"
	@echo "  all           Compile le binaire (défaut)"
	@echo "  install       Compile et installe dans \$$(BINDIR)"
	@echo "  uninstall     Supprime le binaire installé"
	@echo "  clean         Supprime le binaire local"
	@echo "  distclean     clean + fichiers temporaires (*.o, *~, core)"
	@echo "  run [ARGS=..] Compile et exécute  (ex: make run ARGS=\"-A -u\")"
	@echo "  help          Affiche cette aide"
	@echo ""
	@echo "Variables configurables :"
	@echo "  CC            Compilateur      (défaut: gcc)"
	@echo "  CFLAGS        Flags de compil  (défaut: -Wall -Wextra -std=c11 -O2)"
	@echo "  PREFIX        Préfixe install  (défaut: répertoire parent du projet)"
	@echo "  BINDIR        Dossier binaire  (défaut: \$$(PREFIX)/bin)"
	@echo ""
	@echo "Exemples :"
	@echo "  make                          # compile"
	@echo "  make install                  # installe dans $(BINDIR)"
	@echo "  make install PREFIX=/usr/local # installe dans /usr/local/bin"
	@echo "  make run ARGS=\"-A --forest\"   # exécute avec options"
	@echo "  make uninstall                # désinstalle"
	@echo "  make distclean               # nettoie tout"
	@echo ""
