#ifndef FILTERS_H
#define FILTERS_H

#include "process.h"

/* Critères de filtrage passés à doit_afficher() */
typedef struct {
    int opt_a;       /* -a : tous les utilisateurs */
    int opt_x;       /* -x : inclure les démons */
    int pid_filtre;  /* -p : PID exact (0 = pas de filtre) */
    int uid_filtre;  /* -U : UID exact (-1 = pas de filtre) */
    char tty_filtre[32]; /* -T : TTY exact ("" = pas de filtre) */
} Filtres;

/* Retourne 1 si le processus doit être affiché, 0 sinon */
int doit_afficher(Processus *p, Filtres *f, int uid_courant);

/* Retourne 1 si le processus n'a pas de parent dans la liste (racine pour --forest) */
int est_racine(Processus *procs, int nb, int i);

/* Comparateurs pour qsort */
int comparer_pid(const void *a, const void *b);
int comparer_cpu(const void *a, const void *b);
int comparer_mem(const void *a, const void *b);
int comparer_nom(const void *a, const void *b);

/* Vérifie si une string est un nombre (pour filtrer /proc) */
int est_un_nombre(const char *s);

#endif