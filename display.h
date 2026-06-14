#ifndef DISPLAY_H
#define DISPLAY_H

#include "process.h"
#include <stddef.h>

/* Options d'affichage actives */
typedef struct {
    int opt_u;      /* -u : afficher USER */
    int opt_f;      /* -f : format plein  */
    int opt_F;      /* -F : format extra-plein */
    int opt_v;      /* -v : format mémoire */
    int opt_forest; /* --forest : arbre ASCII */
} OptsAffichage;

/* Convertit le numéro TTY encodé en string lisible (ex: pts/0) */
void decoder_tty(int tty_nr, char *buf, size_t size);

/* Convertit utime+stime en HH:MM:SS */
void formater_time(long utime, long stime, char *buf, size_t size);

/* Convertit utime+stime en MM:SS pour le format court */
void formater_time_court(long utime, long stime, char *buf, size_t size);

/* Affiche le header selon le format actif */
void afficher_en_tete(OptsAffichage *opts);

/* Affiche une ligne pour un processus.
   prefixe = "" en mode normal, "\_ " ou "|  " en mode --forest */
void afficher_ligne(Processus *p, OptsAffichage *opts, const char *prefixe);

/* Affiche récursivement les enfants d'un processus (--forest) */
void afficher_tree(Processus *procs, int nb, int pid_parent,
                   const char *prefixe_parent, OptsAffichage *opts);

#endif