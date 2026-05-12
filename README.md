# myps — Implémentation de la commande `ps` sous Linux

Projet réalisé dans le cadre du cours Systèmes d'Exploitation.  
Auteur : georgesngk  
Langage : C  
Environnement : Ubuntu 24.04 (WSL2)

---

## Description

`myps` est une réimplémentation partielle de la commande `ps` de Linux.  
Il lit les informations des processus directement depuis le système de fichiers virtuel `/proc`, exactement comme le fait le vrai `ps`.

---

## Compilation

```bash
gcc -Wall -o myps myps.c
```

Ou avec le Makefile :

```bash
make
```

Pour installer `myps` globalement (accessible sans `./`) :

```bash
sudo cp myps /usr/local/bin/myps
```

---

## Utilisation

```bash
myps [options]
```

### Options disponibles

| Option | Description |
|--------|-------------|
| `-a` | Afficher les processus de tous les utilisateurs |
| `-u` | Afficher le nom d'utilisateur dans la sortie |
| `-x` | Inclure les processus sans terminal (démons, services) |
| `-p PID` | Afficher uniquement le processus avec ce PID |
| `-U user` | Filtrer les processus par nom d'utilisateur |
| `-F` | Format long : UID, PID, PPID, NOM, ETAT, MEM, SZ, %CPU, CMD |
| `-v` | Format mémoire : PID, TTY, STAT, TIME, MAJFL, TRS, DRS, RSS, %MEM, CMD |
| `--forest` | Afficher les processus en arbre (relations parent/enfant) |

---

## Exemples

```bash
# Processus de l'utilisateur courant avec terminal
myps

# Tous les processus de tous les utilisateurs
myps -a -x

# Processus d'un utilisateur spécifique
myps -U root -x -a

# Processus spécifique par PID
myps -p 312

# Format long
myps -F -x

# Format mémoire
myps -v

# Affichage en arbre
myps --forest -x -a

# Combinaisons
myps -U georgesngk --forest
```

---

## Sources de données

Toutes les informations viennent du système de fichiers virtuel `/proc` :

| Fichier | Informations lues |
|---------|-------------------|
| `/proc/[PID]/stat` | PID, PPID, état, TTY, temps CPU, page faults |
| `/proc/[PID]/status` | UID, VmRSS (mémoire physique) |
| `/proc/[PID]/statm` | TRS (taille code), DRS (taille données) |
| `/proc/[PID]/cmdline` | Ligne de commande complète |
| `/proc/uptime` | Temps depuis le boot (pour calculer %CPU) |

---

## Architecture du code

```
myps.c
│
├── Structure Processus        → stocke toutes les infos d'un processus
│
├── Lecture /proc
│   ├── lire_stat()            → /proc/[PID]/stat  (infos principales)
│   ├── lire_uid()             → /proc/[PID]/status (UID)
│   ├── lire_vmrss()           → /proc/[PID]/status (mémoire RSS)
│   ├── lire_statm()           → /proc/[PID]/statm  (TRS, DRS)
│   └── lire_cmdline()         → /proc/[PID]/cmdline
│
├── Filtrage
│   ├── est_un_nombre()        → filtre les entrées non-PID de /proc
│   ├── doit_afficher()        → applique les filtres -a -x -p -U
│   └── est_racine()           → détecte les racines pour --forest
│
├── Formatage
│   ├── decoder_tty()          → numéro encodé → "pts/0" ou "tty1"
│   └── formater_time()        → ticks → "H:MM:SS"
│
└── Affichage
    ├── afficher_en_tete()     → header selon le format actif
    ├── afficher_ligne()       → une ligne par processus
    └── afficher_tree()        → affichage récursif pour --forest
```

---

## Concepts clés utilisés

**`/proc`** — Système de fichiers virtuel Linux. Chaque processus vivant a un dossier `/proc/[PID]/` contenant ses informations en temps réel.

**Parsing de `/proc/[PID]/stat`** — Fichier sur une ligne avec ~52 champs séparés par des espaces. On utilise `fscanf` pour lire champ par champ.

**`opendir` / `readdir`** — Parcours du répertoire `/proc` pour trouver tous les PIDs (dossiers dont le nom est un nombre).

**`getopt_long`** — Parsing des options courtes (`-a`, `-F`...) et longues (`--forest`).

**Tableau dynamique** — Les processus sont stockés dans un tableau alloué avec `malloc`, agrandi avec `realloc` (doublement de capacité).

**`qsort`** — Tri des processus par PID croissant avant affichage.

**Récursion** — `afficher_tree()` s'appelle elle-même pour descendre dans l'arbre parent/enfant.

---

## Limites connues

- Le décodage TTY gère `pts/X` et `tty X` mais pas tous les types de terminaux spéciaux
- `--forest` sans `-x` ni `-a` peut afficher des arbres incomplets si des processus parents sont filtrés
- Les colonnes STIME et PSR (présentes dans `ps -F`) ne sont pas implémentées
