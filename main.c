/**
 * main.c - Point d'entrée du programme myps
 *
 * Compile : gcc -Wall -O2 -o myps main.c proc_reader.c filters.c display.c parse_args.c membre2.c
 * Ou      : make
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#include "process.h"
#include "proc_reader.h"
#include "filters.h"
#include "display.h"
#include "parse_args.h"
#include "membre2.h"

int main(int argc, char **argv) {

    /* --- Parsing des options --- */
    Options opts;
    if (!parse_ps_args(argc, argv, &opts))
        return 1;

    /* --- Ouverture de /proc --- */
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) { perror("opendir /proc"); return 1; }

    /* --- Tableau dynamique de processus --- */
    Processus *procs    = NULL;
    int        nb_procs = 0;
    int        capacite = 0;
    int        uid_courant = getuid();
    struct dirent *ent;

    /* --- Lecture de tous les processus --- */
    while ((ent = readdir(proc_dir)) != NULL) {
        if (!est_un_nombre(ent->d_name)) continue;

        int       pid = atoi(ent->d_name);
        Processus p;

        if (!lire_stat(pid, &p))    continue;
        p.uid = lire_uid(pid);
        p.rss = lire_vmrss(pid);
        lire_statm(pid, &p.trs, &p.drs);
        lire_cmdline(pid, p.cmdline, sizeof(p.cmdline));

        /* Option -d : exclure les chefs de session */
        if (opts.opt_d && est_chef_session(&p)) continue;

        /* Filtrage selon les options */
        if (!doit_afficher(&p, &opts.filtres, uid_courant)) continue;

        /* Agrandissement du tableau si nécessaire */
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

    /* --- Tri --- */
    if (opts.opt_r)
        qsort(procs, nb_procs, sizeof(Processus), comparer_cpu);
    else if (opts.opt_m)
        qsort(procs, nb_procs, sizeof(Processus), comparer_mem);
    else if (opts.opt_k[0] != '\0') {
        if      (strcmp(opts.opt_k, "cpu")  == 0) qsort(procs, nb_procs, sizeof(Processus), comparer_cpu);
        else if (strcmp(opts.opt_k, "mem")  == 0) qsort(procs, nb_procs, sizeof(Processus), comparer_mem);
        else if (strcmp(opts.opt_k, "name") == 0) qsort(procs, nb_procs, sizeof(Processus), comparer_nom);
        else                                        qsort(procs, nb_procs, sizeof(Processus), comparer_pid);
    } else {
        qsort(procs, nb_procs, sizeof(Processus), comparer_pid);
    }

    /* --- Affichage --- */
    afficher_en_tete(&opts.affichage);

    if (opts.affichage.opt_forest) {
        for (int i = 0; i < nb_procs; i++) {
            if (est_racine(procs, nb_procs, i)) {
                afficher_ligne(&procs[i], &opts.affichage, "");
                afficher_tree(procs, nb_procs, procs[i].pid, "", &opts.affichage);
            }
        }
    } else {
        for (int i = 0; i < nb_procs; i++)
            afficher_ligne(&procs[i], &opts.affichage, "");
    }

    free(procs);
    return 0;
}