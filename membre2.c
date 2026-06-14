#include "membre2.h"

/*
 * est_chef_session - détecte si un processus est chef de session
 * Un chef de session a son PID égal à son SID.
 * Utilisé par l'option -d pour les exclure de l'affichage.
 */
int est_chef_session(Processus *p) {
    return p->pid == p->sid;
}