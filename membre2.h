#ifndef MEMBRE2_H
#define MEMBRE2_H

#include "process.h"
#include "display.h"

/*
 * Options implémentées par le membre 2 :
 *   -p [PID]    filtre par PID               → géré dans parse_args + filters
 *   -U [user]   filtre par utilisateur        → géré dans parse_args + filters
 *   -F          format extra-plein            → géré dans display
 *   -v          format mémoire virtuelle      → géré dans display
 *   --forest    arbre ASCII parent/enfant     → géré dans display
 *   -r          tri par CPU décroissant       → géré dans main
 *   -m          tri par mémoire décroissante  → géré dans main
 *   -k clé      tri par colonne               → géré dans main
 *   -T tty      filtre par terminal           → géré dans parse_args + filters
 *   -d          exclure chefs de session      → géré dans main
 */

/* Retourne 1 si le processus est chef de session (PID == SID) */
int est_chef_session(Processus *p);

#endif