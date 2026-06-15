/**
 * Options :
 *   -a : tous les utilisateurs
 *   -u : afficher USER
 *   -x : inclure processus sans terminal
 *   -e : équivalent à -a -x
 *   -f : affichage complet
 *   -c : afficher le nom de commande au lieu de cmdline
 *   -H : affichage hiérarchique (arbre des processus)
 *   -p PID : filtrer un PID
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#define HZ sysconf(_SC_CLK_TCK)

typedef struct {
    int pid;
    int ppid;
    int uid;
    char nom[256];
    char etat;
    int tty;
    long rss;
    char cmdline[512];
    double cpu;
    int pgid;
    int sid;
    double total_cpu_sec;
} Processus;

/* Vérifie qu'une chaîne ne contient que des chiffres */
int est_un_nombre(const char *s) {
    if (!s || !*s) return 0;
    while (*s) {
        if (!isdigit(*s++)) return 0;
    }
    return 1;
}

/* Lecture de /proc/PID/stat */
int lire_stat(int pid, Processus *proc) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/stat", pid);

    FILE *f = fopen(chemin, "r");
    if (!f) return 0;

    int pid_lu, ppid, pgrp, session, tty_nr;
    char nom[256], etat;
    long rss, utime, stime, starttime, dummy;

    int n = fscanf(f,
        "%d %255s %c %d %d %d %d "
        "%ld %ld %ld %ld %ld %ld %ld "
        "%ld %ld %ld %ld %ld %ld %ld "
        "%ld %ld %ld %ld %ld",
        &pid_lu, nom, &etat, &ppid, &pgrp, &session, &tty_nr,
        &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy,
        &utime, &stime, &dummy, &dummy, &dummy, &dummy, &dummy,
        &starttime, &dummy, &dummy, &dummy, &rss);

    fclose(f);

    if (n < 24) return 0;

    proc->pid = pid_lu;
    proc->ppid = ppid;
    proc->etat = etat;
    proc->tty = tty_nr;
    proc->rss = rss;
    proc->pgid = pgrp;
    proc->sid = session;

    int len = strlen(nom);
    if (len >= 2 && nom[0] == '(' && nom[len - 1] == ')') {
        nom[len - 1] = '\0';
        strncpy(proc->nom, nom + 1, sizeof(proc->nom) - 1);
    } else {
        strncpy(proc->nom, nom, sizeof(proc->nom) - 1);
    }
    proc->nom[sizeof(proc->nom)-1] = '\0';

    long hz = HZ;
    double total_time = (utime + stime) / (double) hz;
    proc->total_cpu_sec = total_time;

    FILE *up = fopen("/proc/uptime", "r");
    if (up) {
        double uptime;
        fscanf(up, "%lf", &uptime);
        fclose(up);

        double seconds = uptime - (starttime / (double) hz);
        proc->cpu = (seconds > 0.0)
                        ? 100.0 * total_time / seconds
                        : 0.0;
    } else {
        proc->cpu = 0.0;
    }

    return 1;
}

/* Lecture UID */
int lire_uid(int pid) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/status", pid);

    FILE *f = fopen(chemin, "r");
    if (!f) return -1;

    char ligne[256];
    int uid = -1;

    while (fgets(ligne, sizeof(ligne), f)) {
        if (strncmp(ligne, "Uid:", 4) == 0) {
            sscanf(ligne, "Uid: %d", &uid);
            break;
        }
    }

    fclose(f);
    return uid;
}

/* Lecture cmdline */
void lire_cmdline(int pid, char *buf, size_t taille) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/cmdline", pid);

    FILE *f = fopen(chemin, "r");
    if (!f) {
        snprintf(buf, taille, "[defunct]");
        return;
    }

    size_t n = fread(buf, 1, taille - 1, f);
    fclose(f);

    if (n == 0) {
        snprintf(buf, taille, "[kernel thread]");
        return;
    }

    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\0')
            buf[i] = ' ';
    }

    buf[n] = '\0';
}

/* Filtrage */
int doit_afficher(Processus *p, int opt_a, int opt_x,
                  int pid_filtre, int uid_courant) {

    if (pid_filtre && p->pid != pid_filtre)
        return 0;

    if (!opt_a && p->uid != uid_courant)
        return 0;

    if (!opt_x && p->tty == 0)
        return 0;

    return 1;
}

/* Tri par PID */
int comparer_pid(const void *a, const void *b) {
    return ((Processus *)a)->pid - ((Processus *)b)->pid;
}

/* Affichage arbre */
void afficher_arbre(Processus *procs, int nb, int parent, int niveau) {
    for (int i = 0; i < nb; i++) {
        if (procs[i].ppid == parent) {
            for (int j = 0; j < niveau; j++)
                printf("   ");

            printf("|-- %s (%d)\n",
                   procs[i].nom,
                   procs[i].pid);

            afficher_arbre(procs,
                           nb,
                           procs[i].pid,
                           niveau + 1);
        }
    }
}

void afficher_entete(int opt_u, int opt_f) {

    if (opt_f) {
        printf("%-7s %-7s %-7s %-7s %-10s %-8s %-6s %s\n",
               "PID","PPID","PGID","SID",
               "USER","MEM(KB)","%CPU","CMD");
        return;
    }

    if (opt_u) {
        printf("%-7s %-10s %-20s %-5s %-7s %-8s %-6s %s\n",
               "PID","USER","NAME","ETAT",
               "PPID","MEM(KB)","%CPU","CMD");
    } else {
        printf("%-7s %-20s %-5s %-7s %-8s %-6s %s\n",
               "PID","NAME","ETAT",
               "PPID","MEM(KB)","%CPU","CMD");
    }
}

void afficher_ligne(Processus *p,
                    int opt_u,
                    int opt_f,
                    int opt_c) {

    long mem_kb = p->rss * 4;

    struct passwd *pw = getpwuid(p->uid);
    char *user = (pw && pw->pw_name) ? pw->pw_name : "?";

    const char *commande =
        opt_c ? p->nom : p->cmdline;

    if (opt_f) {
        printf("%-7d %-7d %-7d %-7d %-10s %-8ld %-5.1f %s\n",
               p->pid, p->ppid,
               p->pgid, p->sid,
               user,
               mem_kb,
               p->cpu,
               commande);
    }
    else if (opt_u) {
        printf("%-7d %-10s %-20s %-5c %-7d %-8ld %-5.1f %s\n",
               p->pid, user, p->nom,
               p->etat, p->ppid,
               mem_kb, p->cpu,
               commande);
    }
    else {
        printf("%-7d %-20s %-5c %-7d %-8ld %-5.1f %s\n",
               p->pid, p->nom,
               p->etat, p->ppid,
               mem_kb, p->cpu,
               commande);
    }
}

/* Analyse des options sans getopt */
int parse_ps_args(int argc, char **argv,
                  int *opt_a,
                  int *opt_u,
                  int *opt_x,
                  int *opt_e,
                  int *opt_f,
                  int *opt_c,
                  int *opt_H,
                  int *pid_filtre) {

    *opt_a = *opt_u = *opt_x = 0;
    *opt_e = *opt_f = *opt_c = *opt_H = 0;
    *pid_filtre = 0;

    for (int i = 1; i < argc; i++) {

        char *arg = argv[i];

        if (arg[0] != '-') {
            fprintf(stderr, "Argument invalide : %s\n", arg);
            return 0;
        }

        if (strcmp(arg, "-p") == 0) {
            if (i + 1 >= argc) return 0;

            *pid_filtre = atoi(argv[++i]);
            continue;
        }

        for (int j = 1; arg[j]; j++) {

            switch (arg[j]) {

                case 'a': *opt_a = 1; break;
                case 'u': *opt_u = 1; break;
                case 'x': *opt_x = 1; break;

                case 'e':
                    *opt_e = 1;
                    *opt_a = 1;
                    *opt_x = 1;
                    break;

                case 'f': *opt_f = 1; break;
                case 'c': *opt_c = 1; break;
                case 'H': *opt_H = 1; break;

                default:
                    fprintf(stderr,
                            "Option inconnue : -%c\n",
                            arg[j]);
                    return 0;
            }
        }
    }

    return 1;
}

int main(int argc, char **argv) {

    int opt_a, opt_u, opt_x;
    int opt_e, opt_f, opt_c, opt_H;
    int pid_filtre;

    if (!parse_ps_args(argc, argv,
                       &opt_a, &opt_u, &opt_x,
                       &opt_e, &opt_f, &opt_c,
                       &opt_H,
                       &pid_filtre))
        return 1;

    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) {
        perror("/proc");
        return 1;
    }

    Processus *procs = NULL;
    int nb = 0;
    int capacite = 0;

    int uid_courant = getuid();

    struct dirent *ent;

    while ((ent = readdir(proc_dir)) != NULL) {

        if (!est_un_nombre(ent->d_name))
            continue;

        int pid = atoi(ent->d_name);

        Processus p;

        if (!lire_stat(pid, &p))
            continue;

        p.uid = lire_uid(pid);

        lire_cmdline(pid,
                     p.cmdline,
                     sizeof(p.cmdline));

        if (!doit_afficher(&p,
                           opt_a,
                           opt_x,
                           pid_filtre,
                           uid_courant))
            continue;

        if (nb >= capacite) {

            capacite =
                (capacite == 0)
                    ? 64
                    : capacite * 2;

            Processus *tmp =
                realloc(procs,
                        capacite * sizeof(Processus));

            if (!tmp) {
                free(procs);
                closedir(proc_dir);
                return 1;
            }

            procs = tmp;
        }

        procs[nb++] = p;
    }

    closedir(proc_dir);

    qsort(procs,
          nb,
          sizeof(Processus),
          comparer_pid);

    if (opt_H) {

        printf("ARBRE DES PROCESSUS\n");
        printf("===================\n");

        afficher_arbre(procs,
                       nb,
                       1,
                       0);
    }
    else {

        afficher_entete(opt_u,
                        opt_f);

        for (int i = 0; i < nb; i++) {
            afficher_ligne(&procs[i],
                           opt_u,
                           opt_f,
                           opt_c);
        }
    }

    free(procs);

    return 0;
}
