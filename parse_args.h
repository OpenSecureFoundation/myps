#ifndef PARSE_ARGS_H
#define PARSE_ARGS_H

#include "filters.h"
#include "display.h"

/* Contient toutes les options parsées */
typedef struct {
    Filtres        filtres;   /* options de filtrage */
    OptsAffichage  affichage; /* options d'affichage */
    int            opt_r;     /* -r : tri par CPU décroissant */
    int            opt_m;     /* -m : tri par mémoire décroissante */
    char           opt_k[32]; /* -k clé : tri par colonne (pid/cpu/mem/name) */
    int            opt_d;     /* -d : exclure chefs de session */
} Options;

/*
 * parse_ps_args - parse les arguments sans getopt
 *
 * Retourne 1 si succès, 0 si erreur (message affiché sur stderr).
 * Supporte les options groupées (ex: -aux) et les options avec argument.
 */
int parse_ps_args(int argc, char **argv, Options *opts);

/* Affiche l'aide */
void afficher_aide(const char *prog);

#endif