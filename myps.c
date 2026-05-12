/**
 * myps.c - Implémentation de la commande ps sous Linux
 *
 * Compilation : gcc -Wall -o myps myps.c
 * Utilisation : myps [options]
 *
 * Options implémentées :
 *   -a          : afficher les processus de tous les utilisateurs
 *   -u          : afficher le nom d'utilisateur
 *   -x          : inclure les processus sans terminal (démons)
 *   -p PID      : afficher uniquement le processus avec ce PID
 *   -U user     : filtrer par nom d'utilisateur
 *   -F          : format long (UID, SZ en plus)
 *   -v          : format mémoire (MAJFL, TRS, DRS, RSS, %MEM)
 *   --forest    : affichage en arbre des relations parent/enfant
 *
 * Sources de données :
 *   /proc/[PID]/stat    → PID, PPID, état, TTY, temps CPU, faults
 *   /proc/[PID]/status  → UID, VmRSS
 *   /proc/[PID]/statm   → TRS (taille code), DRS (taille données)
 *   /proc/[PID]/cmdline → ligne de commande complète
 *   /proc/uptime        → temps depuis le démarrage (pour %CPU)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <getopt.h>

/* Nombre de ticks horloge par seconde — utilisé pour convertir
   les temps CPU bruts en secondes */
#define HZ sysconf(_SC_CLK_TCK)

/* ------------------------------------------------------------------ */
/*  Structure principale — une instance par processus lu dans /proc    */
/* ------------------------------------------------------------------ */
typedef struct {
    int    pid;            /* Identifiant du processus */
    int    ppid;           /* PID du processus parent */
    int    uid;            /* Identifiant utilisateur (numérique) */
    char   nom[256];       /* Nom court du processus (ex: bash, node) */
    char   etat;           /* État : R=running S=sleeping D=blocked Z=zombie T=stopped */
    int    tty;            /* Numéro encodé du terminal (0 = pas de terminal) */
    long   rss;            /* Mémoire physique utilisée en KB (VmRSS) */
    char   cmdline[512];   /* Ligne de commande complète */
    double cpu;            /* Pourcentage CPU depuis le lancement */
    int    pgid;           /* Process Group ID */
    int    sid;            /* Session ID */
    double total_cpu_sec;  /* Temps CPU total en secondes (utime+stime) */
    unsigned long majfl;   /* Nombre de major page faults */
    unsigned long trs;     /* Taille du segment code en KB */
    unsigned long drs;     /* Taille du segment données en KB */
    long   utime;          /* Temps CPU en mode utilisateur (en ticks) */
    long   stime;          /* Temps CPU en mode noyau (en ticks) */
} Processus;

/* ------------------------------------------------------------------ */
/*  Options longues — seul --forest est ajouté                         */
/*  On lui associe le caractère interne 'T' pour éviter le conflit     */
/*  avec -v qui est déjà le format mémoire                             */
/* ------------------------------------------------------------------ */
static struct option opts_longs[] = {
    {"forest", no_argument, 0, 'T'},
    {0, 0, 0, 0}   /* terminateur obligatoire */
};

/* ================================================================== */
/*  FONCTIONS UTILITAIRES                                               */
/* ================================================================== */

/**
 * est_un_nombre - vérifie si une chaîne ne contient que des chiffres
 * Utilisé pour filtrer les entrées de /proc qui sont des PIDs
 * (on ignore "net", "sys", "self", etc.)
 */
int est_un_nombre(const char *s) {
    if (!s || !*s) return 0;
    while (*s) if (!isdigit(*s++)) return 0;
    return 1;
}

/**
 * est_racine - détermine si le processus i n'a pas de parent
 * dans notre liste de processus chargés.
 * Utilisé par --forest pour trouver les points de départ de l'arbre.
 */
int est_racine(Processus *procs, int nb, int i) {
    for (int j = 0; j < nb; j++) {
        if (procs[j].pid == procs[i].ppid)
            return 0;  /* le parent est dans la liste → pas une racine */
    }
    return 1;  /* aucun parent trouvé → c'est une racine */
}

/* ================================================================== */
/*  FONCTIONS DE LECTURE /proc                                          */
/* ================================================================== */

/**
 * lire_stat - lit /proc/[pid]/stat et remplit la struct Processus
 *
 * /proc/[pid]/stat est une ligne unique avec ~52 champs séparés par
 * des espaces. On utilise fscanf avec %ld pour chaque champ.
 * Les champs non utiles sont lus dans "dummy" pour avancer le curseur.
 *
 * Champs lus :
 *   1  = PID
 *   2  = nom entre parenthèses ex: (bash)
 *   3  = état (R, S, D, Z, T)
 *   4  = PPID
 *   5  = PGID
 *   6  = SID
 *   7  = numéro TTY encodé
 *   12 = majfl (major page faults)
 *   15 = utime (ticks CPU utilisateur)
 *   16 = stime (ticks CPU noyau)
 *   22 = starttime (ticks depuis le boot, pour calculer %CPU)
 */
int lire_stat(int pid, Processus *proc) {
    char chemin[512];
    snprintf(chemin, sizeof(chemin), "/proc/%d/stat", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;

    int  pid_lu, ppid, pgrp, session, tty_nr;
    char nom[256];
    char etat;
    long rss, utime, stime, starttime, dummy;

    int n = fscanf(f,
        "%d %255s %c %d %d %d %d"          /* champs 1-7  */
        " %ld %ld %ld %ld %ld %ld %ld"     /* champs 8-14 : dummy sauf 12=majfl */
        " %ld %ld %ld %ld %ld %ld %ld"     /* champs 15-21 : utime, stime, puis dummy */
        " %ld %ld %ld %ld %ld",            /* champs 22-26 : starttime, trs, drs, dummy, rss */
        &pid_lu, nom, &etat, &ppid, &pgrp, &session, &tty_nr,
        &dummy, &dummy, &dummy, &dummy, &proc->majfl, &dummy, &dummy,
        &utime, &stime, &dummy, &dummy, &dummy, &dummy, &dummy,
        &starttime, &proc->trs, &proc->drs, &dummy, &rss);
    fclose(f);

    /* Si moins de 24 champs lus, le fichier est incomplet ou corrompu */
    if (n < 24) return 0;

    proc->pid  = pid_lu;
    proc->ppid = ppid;
    proc->tty  = tty_nr;
    proc->rss  = rss;
    proc->etat = etat;
    proc->pgid = pgrp;
    proc->sid  = session;

    /* Le nom dans /proc/stat est entre parenthèses : "(bash)"
       On supprime les parenthèses pour obtenir "bash" */
    int len = strlen(nom);
    if (len >= 2 && nom[0] == '(' && nom[len-1] == ')') {
        nom[len-1] = '\0';
        strncpy(proc->nom, nom + 1, sizeof(proc->nom) - 1);
        proc->nom[sizeof(proc->nom)-1] = '\0';
    } else {
        strncpy(proc->nom, nom, sizeof(proc->nom)-1);
        proc->nom[sizeof(proc->nom)-1] = '\0';
    }

    /* Calcul du %CPU :
       total_time = (utime + stime) / HZ  → temps CPU consommé en secondes
       seconds    = uptime - starttime/HZ  → âge du processus en secondes
       cpu%       = 100 * total_time / seconds */
    long   hz         = HZ;
    double total_time = (utime + stime) / (double)hz;
    proc->total_cpu_sec = total_time;
    proc->utime = utime;
    proc->stime = stime;

    double uptime = 0;
    FILE *up = fopen("/proc/uptime", "r");
    if (up) {
        fscanf(up, "%lf", &uptime);
        fclose(up);
        double age = uptime - (starttime / (double)hz);
        proc->cpu = (age > 0.0) ? 100.0 * total_time / age : 0.0;
    } else {
        proc->cpu = 0.0;
    }
    return 1;
}

/**
 * lire_uid - lit l'UID réel du processus depuis /proc/[pid]/status
 *
 * On préfère /proc/status à /proc/stat pour l'UID car /proc/stat
 * ne contient pas cette information directement.
 * La ligne "Uid:" contient 4 valeurs : réel, effectif, saved, filesystem.
 * On prend le premier (UID réel).
 */
int lire_uid(int pid) {
    char chemin[512];
    snprintf(chemin, sizeof(chemin), "/proc/%d/status", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return -1;
    char line[256];
    int uid = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Uid:", 4) == 0) {
            sscanf(line, "Uid: %d", &uid);
            break;
        }
    }
    fclose(f);
    return uid;
}

/**
 * lire_vmrss - lit la mémoire physique utilisée depuis /proc/[pid]/status
 *
 * VmRSS (Resident Set Size) = mémoire physique réellement occupée en KB.
 * C'est la valeur la plus représentative de la consommation mémoire réelle.
 */
long lire_vmrss(int pid) {
    char chemin[512];
    snprintf(chemin, sizeof(chemin), "/proc/%d/status", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;
    char line[256];
    long vmrss = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line, "VmRSS: %ld", &vmrss);
            break;
        }
    }
    fclose(f);
    return vmrss;
}

/**
 * lire_cmdline - lit la ligne de commande depuis /proc/[pid]/cmdline
 *
 * /proc/[pid]/cmdline contient les arguments séparés par des octets nuls \0.
 * On remplace chaque \0 par un espace pour obtenir une string lisible.
 * Si le fichier est vide, c'est un thread noyau → on affiche [kernel thread].
 */
void lire_cmdline(int pid, char *buf, size_t size) {
    char chemin[512];
    snprintf(chemin, sizeof(chemin), "/proc/%d/cmdline", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) {
        snprintf(buf, size, "[defunct]");
        return;
    }
    size_t n = fread(buf, 1, size-1, f);
    fclose(f);
    if (n == 0) {
        snprintf(buf, size, "[kernel thread]");
        return;
    }
    /* Remplace les séparateurs \0 entre arguments par des espaces */
    for (size_t i = 0; i < n-1; i++)
        if (buf[i] == '\0') buf[i] = ' ';
    buf[n] = '\0';
}

/**
 * lire_statm - lit TRS et DRS depuis /proc/[pid]/statm
 *
 * /proc/[pid]/statm est plus précis que /proc/stat pour les tailles mémoire.
 * Format : size rss shared text lib data dirty (tous en pages)
 *   text (champ 4) = TRS : taille du segment code
 *   data (champ 6) = DRS : taille du segment données
 * On multiplie par 4 pour convertir pages → KB (1 page = 4 KB).
 */
int lire_statm(int pid, unsigned long *trs, unsigned long *drs) {
    char chemin[512];
    snprintf(chemin, sizeof(chemin), "/proc/%d/statm", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;
    long size, rss, shared, text, lib, data;
    fscanf(f, "%ld %ld %ld %ld %ld %ld", &size, &rss, &shared, &text, &lib, &data);
    fclose(f);
    *trs = (unsigned long)text * 4;  /* pages → KB */
    *drs = (unsigned long)data * 4;
    return 1;
}

/* ================================================================== */
/*  FONCTIONS DE FILTRAGE ET TRI                                        */
/* ================================================================== */

/**
 * doit_afficher - décide si un processus doit être affiché
 *
 * Applique les filtres dans l'ordre :
 *   1. -p : PID exact demandé
 *   2. -U : UID spécifique demandé
 *   3. sans -a : uniquement les processus de l'utilisateur courant
 *   4. sans -x : uniquement les processus avec un terminal (tty != 0)
 */
int doit_afficher(Processus *p, int opt_a, int opt_x,
                  int pid_filtre, int uid_filtre, int uid_courant) {
    if (pid_filtre != 0  && p->pid != pid_filtre)    return 0;
    if (uid_filtre != -1 && p->uid != uid_filtre)    return 0;
    if (!opt_a           && p->uid != uid_courant)   return 0;
    if (!opt_x           && p->tty == 0)             return 0;
    return 1;
}

/**
 * comparer_pid - comparateur pour qsort
 * Trie les processus par PID croissant.
 */
int comparer_pid(const void *a, const void *b) {
    return ((Processus*)a)->pid - ((Processus*)b)->pid;
}

/* ================================================================== */
/*  FONCTIONS DE CONVERSION ET FORMATAGE                                */
/* ================================================================== */

/**
 * decoder_tty - convertit le numéro TTY encodé en string lisible
 *
 * Linux encode le terminal dans un entier :
 *   bits 8-15 = numéro majeur (136 = pts, autres = tty)
 *   bits 0-7  = numéro mineur (numéro du terminal)
 * Exemple : 34816 → majeur=136, mineur=0 → "pts/0"
 */
void decoder_tty(int tty_nr, char *buf, size_t size) {
    if (tty_nr == 0) {
        snprintf(buf, size, "?");  /* pas de terminal */
        return;
    }
    int majeur = (tty_nr >> 8) & 0xFF;
    int mineur = tty_nr & 0xFF;
    if (majeur == 136)
        snprintf(buf, size, "pts/%d", mineur);
    else
        snprintf(buf, size, "tty%d", mineur);
}

/**
 * formater_time - convertit utime+stime en format HH:MM:SS
 *
 * utime et stime sont en ticks horloge.
 * On divise par HZ pour obtenir des secondes, puis on décompose.
 */
void formater_time(long utime, long stime, char *buf, size_t size) {
    long hz    = sysconf(_SC_CLK_TCK);
    long total = (utime + stime) / hz;
    long h     = total / 3600;
    long m     = (total % 3600) / 60;
    long s     = total % 60;
    snprintf(buf, size, "%ld:%02ld:%02ld", h, m, s);
}

/* ================================================================== */
/*  FONCTIONS D'AFFICHAGE                                               */
/* ================================================================== */

/**
 * afficher_en_tete - affiche la ligne de header selon le format actif
 *
 * Priorité : -v > -F > -u > format par défaut
 */
void afficher_en_tete(int opt_u, int opt_F, int opt_v) {
    if (opt_v) {
        printf("%-10s %-8s %-6s %-8s %-6s %-6s %-8s %-6s %-5s %s\n",
               "PID", "TTY", "STAT", "TIME", "MAJFL", "TRS", "DRS", "RSS", "%MEM", "COMMANDE");
        printf("%-10s %-8s %-6s %-8s %-6s %-6s %-8s %-6s %-5s %s\n",
               "---", "---", "----", "----", "-----", "---", "---", "---", "----", "--------");
    } else if (opt_F) {
        printf("%-10s %-12s %-10s %-16s %-6s %-8s %-8s %-6s %s\n",
               "UID", "PID", "PPID", "NOM", "ETAT", "MEM(KB)", "SZ", "%CPU", "COMMANDE");
        printf("%-10s %-12s %-10s %-16s %-6s %-8s %-8s %-6s %s\n",
               "---", "---", "----", "---", "----", "-------", "--", "----", "--------");
    } else if (opt_u) {
        printf("%-10s %-12s %-16s %-6s %-10s %-8s %-6s %s\n",
               "PID", "USER", "NOM", "ETAT", "PPID", "MEM(KB)", "%CPU", "COMMANDE");
        printf("%-10s %-12s %-16s %-6s %-10s %-8s %-6s %s\n",
               "---", "----", "---", "----", "----", "-------", "----", "--------");
    } else {
        printf("%-10s %-16s %-6s %-10s %-8s %-6s %s\n",
               "PID", "NOM", "ETAT", "PPID", "MEM(KB)", "%CPU", "COMMANDE");
        printf("%-10s %-16s %-6s %-10s %-8s %-6s %s\n",
               "---", "---", "----", "----", "-------", "----", "--------");
    }
}

/**
 * afficher_ligne - affiche une ligne pour un processus
 *
 * Le paramètre "prefixe" contient les caractères d'arbre (\_ et |)
 * générés par afficher_tree. Il est vide "" en mode normal.
 *
 * Formats disponibles selon les options actives :
 *   -v  : PID TTY STAT TIME MAJFL TRS DRS RSS %MEM COMMANDE
 *   -F  : UID PID PPID NOM ETAT MEM SZ %CPU COMMANDE
 *   -u  : PID USER NOM ETAT PPID MEM %CPU COMMANDE
 *   défaut : PID NOM ETAT PPID MEM %CPU COMMANDE
 */
void afficher_ligne(Processus *p, int opt_u, int opt_F, int opt_v,
                    const char *prefixe) {
    long mem_kb = p->rss;

    /* Tronque la commande à 50 caractères pour l'alignement */
    char cmd_tronque[51];
    strncpy(cmd_tronque, p->cmdline, 50);
    cmd_tronque[50] = '\0';

    if (opt_v) {
        char tty[16], time_str[16];
        decoder_tty(p->tty, tty, sizeof(tty));
        formater_time(p->utime, p->stime, time_str, sizeof(time_str));

        /* %MEM = RSS / RAM physique totale * 100
           _SC_PHYS_PAGES * _SC_PAGE_SIZE donne la RAM totale en octets
           On divise par 1024 pour obtenir des KB (même unité que rss) */
        long   ram_totale = sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGE_SIZE) / 1024;
        double pct_mem    = ram_totale > 0 ? (100.0 * mem_kb / ram_totale) : 0.0;

        printf("%-10d %-8s %-6c %-8s %-6lu %-6lu %-8lu %-6ld %-5.1f %s%s\n",
               p->pid, tty, p->etat, time_str,
               p->majfl, p->trs, p->drs, mem_kb, pct_mem,
               prefixe, cmd_tronque);

    } else if (opt_F) {
        struct passwd *pw   = getpwuid(p->uid);
        char          *user = (pw && pw->pw_name) ? pw->pw_name : "?";
        long           sz   = mem_kb / 4;  /* KB → pages approximatif */
        printf("%-10s %-12d %-10d %-16s %-6c %-8ld %-8ld %-6.1f %s%s\n",
               user, p->pid, p->ppid, p->nom, p->etat,
               mem_kb, sz, p->cpu, prefixe, cmd_tronque);

    } else if (opt_u) {
        struct passwd *pw   = getpwuid(p->uid);
        char          *user = (pw && pw->pw_name) ? pw->pw_name : "?";
        printf("%-10d %-12s %-16s %-6c %-10d %-8ld %-6.1f %s%s\n",
               p->pid, user, p->nom, p->etat,
               p->ppid, mem_kb, p->cpu, prefixe, cmd_tronque);

    } else {
        printf("%-10d %-16s %-6c %-10d %-8ld %-6.1f %s%s\n",
               p->pid, p->nom, p->etat,
               p->ppid, mem_kb, p->cpu, prefixe, cmd_tronque);
    }
}

/**
 * afficher_tree - affiche récursivement les enfants d'un processus
 *
 * Algorithme :
 *   Pour chaque processus dont ppid == pid_parent :
 *     1. Construire le préfixe visuel (\_ pour ce niveau)
 *     2. Construire le préfixe pour les enfants (|  pour continuer la branche)
 *     3. Afficher ce processus avec son préfixe
 *     4. Appel récursif pour ses propres enfants (profondeur + 1)
 *
 * La récursion s'arrête naturellement quand un processus n'a plus d'enfants.
 */
void afficher_tree(Processus *procs, int nb, int pid_parent,
                   int profondeur, const char *prefixe_parent,
                   int opt_u, int opt_F, int opt_v) {
    for (int i = 0; i < nb; i++) {
        if (procs[i].ppid != pid_parent) continue;
        if (procs[i].pid  == pid_parent) continue; /* évite boucle PID 0 */

        /* Préfixe affiché devant la commande de ce processus */
        char prefixe_cmd[256];
        snprintf(prefixe_cmd, sizeof(prefixe_cmd), "%s \\_ ", prefixe_parent);

        /* Préfixe transmis aux enfants du niveau suivant */
        char prefixe_enfants[256];
        snprintf(prefixe_enfants, sizeof(prefixe_enfants), "%s |  ", prefixe_parent);

        afficher_ligne(&procs[i], opt_u, opt_F, opt_v, prefixe_cmd);
        afficher_tree(procs, nb, procs[i].pid, profondeur + 1,
                      prefixe_enfants, opt_u, opt_F, opt_v);
    }
}

/* ================================================================== */
/*  FONCTION PRINCIPALE                                                 */
/* ================================================================== */

int main(int argc, char **argv) {

    /* --- Variables d'options --- */
    int opt_a      = 0;   /* -a : tous les utilisateurs */
    int opt_u      = 0;   /* -u : afficher le nom d'utilisateur */
    int opt_x      = 0;   /* -x : inclure les démons (sans TTY) */
    int pid_filtre = 0;   /* -p : filtrer par PID (0 = pas de filtre) */
    int uid_filtre = -1;  /* -U : filtrer par UID (-1 = pas de filtre) */
    int opt_F      = 0;   /* -F : format long */
    int opt_v      = 0;   /* -v : format mémoire */
    int opt_forest = 0;   /* --forest : affichage arbre */
    char nom_filter[256] = "";  /* filtre par nom (non exposé, usage interne) */

    /* --- Parsing des options --- */
    int opt, idx = 0;
    while ((opt = getopt_long(argc, argv, "auxp:U:FvT", opts_longs, &idx)) != -1) {
        switch (opt) {
            case 'a': opt_a = 1; break;
            case 'u': opt_u = 1; break;
            case 'x': opt_x = 1; break;

            case 'p':
                pid_filtre = atoi(optarg);
                if (pid_filtre <= 0) {
                    fprintf(stderr, "Erreur: PID invalide '%s'\n", optarg);
                    return 1;
                }
                break;

            case 'U': {
                /* Convertit le nom d'utilisateur en UID numérique */
                struct passwd *pw = getpwnam(optarg);
                if (!pw) {
                    fprintf(stderr, "Erreur: utilisateur inconnu '%s'\n", optarg);
                    return 1;
                }
                uid_filtre = pw->pw_uid;
                break;
            }

            case 'F': opt_F = 1; break;
            case 'v': opt_v = 1; break;
            case 'T': opt_forest = 1; break;  /* --forest */

            default:
                fprintf(stderr,
                    "Usage: %s [-a] [-u] [-x] [-p PID] [-U user] [-F] [-v] [--forest]\n",
                    argv[0]);
                return 1;
        }
    }

    /* --- Ouverture de /proc --- */
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) {
        perror("opendir /proc");
        return 1;
    }

    /* --- Tableau dynamique de processus ---
       On commence avec une capacité de 64, doublée à chaque saturation */
    Processus *procs    = NULL;
    int        nb_procs = 0;
    int        capacite = 0;
    int        uid_courant = getuid();  /* UID de l'utilisateur qui lance myps */
    struct dirent *ent;

    /* --- Lecture de tous les processus dans /proc --- */
    while ((ent = readdir(proc_dir)) != NULL) {

        /* On ignore les entrées qui ne sont pas des PIDs */
        if (!est_un_nombre(ent->d_name)) continue;

        int       pid = atoi(ent->d_name);
        Processus p;

        /* Lecture du fichier stat principal */
        if (!lire_stat(pid, &p)) continue;

        /* Complétion avec les autres sources */
        p.uid = lire_uid(pid);
        p.rss = lire_vmrss(pid);
        lire_statm(pid, &p.trs, &p.drs);
        lire_cmdline(pid, p.cmdline, sizeof(p.cmdline));

        /* Filtre optionnel par nom (usage interne) */
        if (nom_filter[0] != '\0' && strcmp(p.nom, nom_filter) != 0)
            continue;

        /* Application des filtres utilisateur */
        if (!doit_afficher(&p, opt_a, opt_x, pid_filtre, uid_filtre, uid_courant))
            continue;

        /* Agrandissement du tableau si nécessaire (doublement de capacité) */
        if (nb_procs >= capacite) {
            capacite = (capacite == 0) ? 64 : capacite * 2;
            Processus *nouveau = realloc(procs, capacite * sizeof(Processus));
            if (!nouveau) {
                perror("realloc");
                free(procs);
                closedir(proc_dir);
                return 1;
            }
            procs = nouveau;
        }

        procs[nb_procs++] = p;
    }
    closedir(proc_dir);

    /* --- Tri par PID croissant --- */
    qsort(procs, nb_procs, sizeof(Processus), comparer_pid);

    /* --- Affichage --- */
    afficher_en_tete(opt_u, opt_F, opt_v);

    if (opt_forest) {
        /* Mode arbre : on part des racines et on descend récursivement */
        for (int i = 0; i < nb_procs; i++) {
            if (est_racine(procs, nb_procs, i)) {
                afficher_ligne(&procs[i], opt_u, opt_F, opt_v, "");
                afficher_tree(procs, nb_procs, procs[i].pid, 1, "",
                              opt_u, opt_F, opt_v);
            }
        }
    } else {
        /* Mode normal : affichage linéaire dans l'ordre des PIDs */
        for (int i = 0; i < nb_procs; i++)
            afficher_ligne(&procs[i], opt_u, opt_F, opt_v, "");
    }

    free(procs);
    return 0;
}
