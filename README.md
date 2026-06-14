# myps — Réimplémentation de la commande `ps` sous Linux

Projet réalisé dans le cadre du cours Systèmes d'Exploitation.  
Langage : C | Environnement : Ubuntu (natif ou WSL2 sous Windows)

---

## Récupérer et installer le projet

```bash
# 1. Cloner le dépôt
git clone https://github.com/OpenSecureFoundation/myps.git
cd myps

# 2. Basculer sur la branche du membre 2
git checkout gerorgesngk00-v2

# 3. Compiler
make

# 4. Installer globalement (permet de taper "myps" sans "./" ni chemin)
make install

# 5. Vérifier
myps
myps --forest -a -x
```

Fonctionne sur Ubuntu, Kali, Debian et WSL2 (Windows 10/11).

---

## Désinstaller

```bash
make uninstall
```

Supprime le binaire de `/usr/local/bin`. Le code source reste intact.

---

## Utilisation

```bash
myps [OPTIONS]
```

### Options de filtrage

| Option | Description | Exemple |
|--------|-------------|---------|
| `-a` | Tous les utilisateurs | `myps -a` |
| `-x` | Inclure les démons (sans terminal) | `myps -x` |
| `-p PID` | Filtrer par PID exact | `myps -p 320` |
| `-U user` | Filtrer par nom d'utilisateur | `myps -U root` |
| `-T tty` | Filtrer par terminal | `myps -T pts/0` |
| `-d` | Exclure les chefs de session | `myps -d -a -x` |

### Options de format

| Option | Colonnes affichées |
|--------|-------------------|
| défaut | `PID TTY TIME CMD` |
| `-u` | `USER PID %CPU %MEM VSZ RSS TTY STAT START COMMAND` |
| `-f` | `UID PID PPID C STIME TTY TIME CMD` |
| `-F` | `UID PID PPID C SZ RSS PSR STIME TTY TIME CMD` |
| `-v` | `PID TTY STAT TIME MAJFL TRS DRS RSS %MEM COMMAND` |

### Options de visualisation

| Option | Description |
|--------|-------------|
| `--forest` | Arbre ASCII des relations parent/enfant |

### Options de tri

| Option | Description |
|--------|-------------|
| `-r` | Tri par %CPU décroissant (plus gourmands en premier) |
| `-m` | Tri par mémoire RSS décroissante |
| `-k pid` | Tri par PID croissant |
| `-k cpu` | Tri par %CPU décroissant |
| `-k mem` | Tri par mémoire décroissante |
| `-k name` | Tri par nom alphabétique |

### Combinaisons d'options

```bash
myps -a -x                    # tous les processus
myps -U root -x -a            # processus de root
myps -p 320                   # processus PID 320
myps -F -a -x                 # format extra-plein, tous les processus
myps -v                       # format mémoire
myps --forest -a -x           # arbre complet
myps -r -a -x                 # triés par CPU décroissant
myps -aux                     # options groupées (équivalent à -a -u -x)
myps -T pts/0                 # processus du terminal pts/0
```

---

## Architecture du projet

Le projet est organisé en modules indépendants. Chaque membre du groupe travaille dans son propre fichier sans toucher à celui des autres.

```
myps/
│
├── process.h         Structure Processus partagée par tous les modules
│
├── proc_reader.h/c   Lecture de /proc — fonctions communes
├── filters.h/c       Filtrage et tri — fonctions communes
├── display.h/c       Affichage — fonctions communes
├── parse_args.h/c    Parsing des arguments — sans getopt
│
├── membre1.h/c       Options du membre 1
├── membre2.h/c       Options du membre 2 (ce fichier)
├── membre3.h/c       Options du membre 3
├── membre4.h/c       Options du membre 4
├── membre5.h/c       Options du membre 5
│
├── main.c            Point d'entrée — relie tous les modules
└── Makefile          Compilation, installation, désinstallation
```

---

## Ajouter les options d'un nouveau membre

Pour intégrer les options d'un coéquipier sans toucher aux fichiers existants :

**1. Déclarer les fonctions dans `membreN.h` :**
```c
#ifndef MEMBREN_H
#define MEMBREN_H
#include "process.h"
#include "display.h"

int ma_fonction(Processus *p);

#endif
```

**2. Implémenter dans `membreN.c` :**
```c
#include "membreN.h"

int ma_fonction(Processus *p) {
    // implémentation
    return 0;
}
```

**3. Déclarer l'option dans `parse_args.c`** — ajouter un `case` dans `traiter_option()` et le champ correspondant dans la struct `Options`.

**4. Appliquer dans `main.c`** — appeler la fonction au bon endroit.

**5. Ajouter à la compilation dans `Makefile` :**
```makefile
SRCS = main.c proc_reader.c filters.c display.c parse_args.c membre2.c membreN.c
```

---

## Sources de données — comment ça fonctionne

`myps` lit directement le système de fichiers virtuel `/proc`, exactement comme le vrai `ps`.

| Fichier lu | Informations extraites |
|------------|----------------------|
| `/proc/[PID]/stat` | PID, PPID, état, TTY, temps CPU, page faults |
| `/proc/[PID]/status` | UID, VmRSS (mémoire physique en KB) |
| `/proc/[PID]/statm` | TRS (taille code), DRS (taille données) en pages |
| `/proc/[PID]/cmdline` | Ligne de commande complète avec arguments |
| `/proc/uptime` | Temps depuis le boot (pour calculer %CPU) |

---

## Parsing des arguments — sans getopt

La fonction `parse_ps_args()` dans `parse_args.c` parse les arguments manuellement sans utiliser la bibliothèque `getopt`. Elle supporte :

- **Options simples** : `-a`, `-x`, `-F`, `-v`
- **Options avec argument** : `-p 320`, `-U root`, `-T pts/0`, `-k cpu`
- **Options groupées** : `-aux` équivaut à `-a -u -x`
- **Options longues** : `--forest`

```c
// Exemple d'appel dans main.c
Options opts;
if (!parse_ps_args(argc, argv, &opts))
    return 1;  // erreur de syntaxe, message affiché
```

---

## Makefile — commandes disponibles

| Commande | Action |
|----------|--------|
| `make` | Compile tous les fichiers `.c` en un binaire `myps` |
| `make install` | Copie `myps` dans `/usr/local/bin` (accès global) |
| `make uninstall` | Supprime `myps` de `/usr/local/bin` |
| `make clean` | Supprime le binaire et les fichiers `.o` |

---

## Environnement de développement recommandé

- **OS** : Ubuntu 24.04 (natif ou WSL2 sous Windows)
- **Compilateur** : gcc 13+
- **Éditeur** : VS Code avec extension WSL et C/C++
- **Outils** : `make`, `git`, `valgrind`

Installation des outils sur Ubuntu/WSL2 :
```bash
sudo apt install -y gcc make git valgrind build-essential
```