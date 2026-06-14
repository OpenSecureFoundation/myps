#include "filters.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

int doit_afficher(Processus *p, Filtres *f, int uid_courant) {
    if (f->pid_filtre != 0  && p->pid != f->pid_filtre)   return 0;
    if (f->uid_filtre != -1 && p->uid != f->uid_filtre)   return 0;
    if (f->tty_filtre[0] != '\0') {
        /* Décode le TTY du processus pour comparer */
        char tty_str[32];
        int majeur = (p->tty >> 8) & 0xFF;
        int mineur = p->tty & 0xFF;
        if (p->tty == 0) snprintf(tty_str, sizeof(tty_str), "?");
        else if (majeur == 136) snprintf(tty_str, sizeof(tty_str), "pts/%d", mineur);
        else snprintf(tty_str, sizeof(tty_str), "tty%d", mineur);
        if (strcmp(tty_str, f->tty_filtre) != 0) return 0;
    }
    if (!f->opt_a && p->uid != uid_courant) return 0;
    if (!f->opt_x && p->tty == 0)          return 0;
    return 1;
}

int est_racine(Processus *procs, int nb, int i) {
    for (int j = 0; j < nb; j++)
        if (procs[j].pid == procs[i].ppid) return 0;
    return 1;
}

int comparer_pid(const void *a, const void *b) {
    return ((Processus*)a)->pid - ((Processus*)b)->pid;
}

int comparer_cpu(const void *a, const void *b) {
    double diff = ((Processus*)b)->cpu - ((Processus*)a)->cpu;
    return (diff > 0) - (diff < 0);
}

int comparer_mem(const void *a, const void *b) {
    return (int)(((Processus*)b)->rss - ((Processus*)a)->rss);
}

int comparer_nom(const void *a, const void *b) {
    return strcmp(((Processus*)a)->nom, ((Processus*)b)->nom);
}

int est_un_nombre(const char *s) {
    if (!s || !*s) return 0;
    while (*s) if (!isdigit(*s++)) return 0;
    return 1;
}