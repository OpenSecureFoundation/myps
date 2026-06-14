#ifndef PROCESS_H
#define PROCESS_H

/* Structure partagée par tous les membres du projet.
   Une instance par processus lu depuis /proc. */
typedef struct {
    int           pid;           /* Identifiant du processus */
    int           ppid;          /* PID du processus parent */
    int           uid;           /* Identifiant utilisateur */
    char          nom[256];      /* Nom court (ex: bash, node) */
    char          etat;          /* R, S, D, Z, T */
    int           tty;           /* Numéro TTY encodé (0 = aucun) */
    long          rss;           /* Mémoire physique en KB */
    char          cmdline[512];  /* Ligne de commande complète */
    double        cpu;           /* % CPU depuis le lancement */
    int           pgid;          /* Process Group ID */
    int           sid;           /* Session ID */
    double        total_cpu_sec; /* Temps CPU total en secondes */
    unsigned long majfl;         /* Major page faults */
    unsigned long trs;           /* Taille code en KB */
    unsigned long drs;           /* Taille données en KB */
    long          utime;         /* Ticks CPU mode utilisateur */
    long          stime;         /* Ticks CPU mode noyau */
} Processus;

#endif