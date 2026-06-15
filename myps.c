/**
 * myps.c - Implémentation de la commande ps sous Linux
 * Compilation : gcc -Wall -Wextra -O2 -o myps myps.c
 * Utilisation : ./myps [-a] [-u] [-x] [-p PID] 
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

#define HZ sysconf(_SC_CLK_TCK)   /* ticks par seconde (généralement 100) */

/* Structure pour stocker les informations d'un processus */
typedef struct {
    int pid;
    int ppid;
    int uid;
    int gid;              /* groupe réel (RGID) - non utilisé ici */
    char nom[256];
    char etat;
    int tty;
    long rss;               /* en pages (4 Ko) */
    char cmdline[512];
    double cpu;             /* pourcentage CPU */
    int pgid;
    int sid;
    double total_cpu_sec;   /* Temps CPU total en secondes */
} Processus;

/* ---------- Fonctions utilitaires ---------- */

int est_un_nombre(const char *s) {
    if (!s || !*s) return 0;
    while (*s) if (!isdigit(*s++)) return 0;
    return 1;
}

int lire_stat(int pid, Processus *proc) {
    char chemin[512];
    snprintf(chemin, sizeof(chemin), "/proc/%d/stat", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;

    int pid_lu, ppid, pgrp, session, tty_nr;
    char nom[256];
    char etat;
    long rss;
    long utime, stime, starttime;
    long dummy;

    int n = fscanf(f,
        "%d %255s %c %d %d %d %d"
        " %ld %ld %ld %ld %ld %ld %ld"
        " %ld %ld %ld %ld %ld %ld %ld"
        " %ld %ld %ld %ld %ld",
        &pid_lu, nom, &etat, &ppid, &pgrp, &session, &tty_nr,
        &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy,
        &utime, &stime, &dummy, &dummy, &dummy, &dummy, &dummy,
        &starttime, &dummy, &dummy, &dummy, &rss);
    fclose(f);

    if (n < 24) return 0;

    proc->pid = pid_lu;
    proc->ppid = ppid;
    proc->tty = tty_nr;
    proc->rss = rss;
    proc->etat = etat;
    proc->pgid = pgrp;
    proc->sid = session;

    /* Nettoyage du nom (enlever les parenthèses) */
    int len = strlen(nom);
    if (len >= 2 && nom[0] == '(' && nom[len-1] == ')') {
        nom[len-1] = '\0';
        strncpy(proc->nom, nom + 1, sizeof(proc->nom) - 1);
        proc->nom[sizeof(proc->nom)-1] = '\0';
    } else {
        strncpy(proc->nom, nom, sizeof(proc->nom)-1);
        proc->nom[sizeof(proc->nom)-1] = '\0';
    }

    /* Calcul du %CPU et du temps total CPU */
    long hz = HZ;
    double total_time = (utime + stime) / (double)hz;
    proc->total_cpu_sec = total_time;

    double uptime;
    FILE *up = fopen("/proc/uptime", "r");
    if (up) {
        fscanf(up, "%lf", &uptime);
        fclose(up);
        double seconds = uptime - (starttime / (double)hz);
        proc->cpu = (seconds > 0.0) ? 100.0 * total_time / seconds : 0.0;
    } else {
        proc->cpu = 0.0;
    }
    return 1;
}

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
    for (size_t i = 0; i < n-1; i++)
        if (buf[i] == '\0') buf[i] = ' ';
    buf[n] = '\0';
}

/* ---------- Filtrage et affichage ---------- */

int doit_afficher(Processus *p, int opt_a, int opt_x, int pid_filtre, int uid_courant) {
    if (pid_filtre != 0 && p->pid != pid_filtre) return 0;
    if (!opt_a && p->uid != uid_courant) return 0;
    if (!opt_x && p->tty == 0) return 0;
    return 1;
}

int comparer_pid(const void *a, const void *b) {
    return ((Processus*)a)->pid - ((Processus*)b)->pid;
}

void afficher_en_tete(int opt_u) {
    if (opt_u) {
        printf("%-8s %-12s %-20s %-6s %-6s %-8s %-6s %s\n",
               "PID", "USER", "NOM", "ÉTAT", "PPID", "MEM(KB)", "%CPU", "COMMANDE");
        printf("%-8s %-12s %-20s %-6s %-6s %-8s %-6s %s\n",
               "---", "----", "---", "----", "----", "-------", "----", "--------");
    } else {
        printf("%-8s %-20s %-6s %-6s %-8s %-6s %s\n",
               "PID", "NOM", "ÉTAT", "PPID", "MEM(KB)", "%CPU", "COMMANDE");
        printf("%-8s %-20s %-6s %-6s %-8s %-6s %s\n",
               "---", "---", "----", "----", "-------", "----", "--------");
    }
}

void afficher_ligne(Processus *p, int opt_u) {
    long mem_kb = p->rss * 4;
    if (opt_u) {
        struct passwd *pw = getpwuid(p->uid);
        char *user = (pw && pw->pw_name) ? pw->pw_name : "?";
        printf("%-8d %-12s %-20s %-6c %-6d %-8ld %-5.1f %s\n",
               p->pid, user, p->nom, p->etat, p->ppid, mem_kb, p->cpu, p->cmdline);
    } else {
        printf("%-8d %-20s %-6c %-6d %-8ld %-5.1f %s\n",
               p->pid, p->nom, p->etat, p->ppid, mem_kb, p->cpu, p->cmdline);
    }
}

/* ---------- Analyse manuelle des options (remplace getopt) ---------- */
int parse_ps_args(int argc, char **argv,
                  int *opt_a, int *opt_u, int *opt_x, int *pid_filtre)
{
    int i = 1;
    *opt_a = 0;
    *opt_u = 0;
    *opt_x = 0;
    *pid_filtre = 0;

    while (i < argc)
    {
        char *arg = argv[i];
        if (arg[0] != '-')
        {
            fprintf(stderr, "Erreur: argument inattendu '%s' (les options commencent par -)\n", arg);
            return 0;
        }

        /* Gestion des options groupées (ex: -aux) */
        if (strlen(arg) > 2)
        {
            for (int j = 1; arg[j] != '\0'; j++)
            {
                char c = arg[j];
                switch (c)
                {
                    case 'a': *opt_a = 1; break;
                    case 'u': *opt_u = 1; break;
                    case 'x': *opt_x = 1; break;
                    default:
                        fprintf(stderr, "Erreur: option inconnue '-%c' dans le groupe\n", c);
                        return 0;
                }
            }
            i++;
            continue;
        }

        /* Option simple (un seul caractère après '-') */
        char opt = arg[1];
        switch (opt)
        {
            case 'a': *opt_a = 1; break;
            case 'u': *opt_u = 1; break;
            case 'x': *opt_x = 1; break;
            case 'p':
                if (i + 1 >= argc)
                {
                    fprintf(stderr, "Erreur: l'option -p nécessite un argument (PID)\n");
                    return 0;
                }
                *pid_filtre = atoi(argv[i + 1]);
                if (*pid_filtre <= 0)
                {
                    fprintf(stderr, "Erreur: PID invalide '%s'\n", argv[i + 1]);
                    return 0;
                }
                i++; /* consomme l'argument du PID */
                break;
            default:
                fprintf(stderr, "Erreur: option inconnue '-%c'\n", opt);
                return 0;
        }
        i++;
    }
    return 1;
}

/* ---------- Fonction principale ---------- */
int main(int argc, char **argv) {
    int opt_a = 0, opt_u = 0, opt_x = 0, pid_filtre = 0;

    /* Analyse des options (sans getopt) */
    if (!parse_ps_args(argc, argv, &opt_a, &opt_u, &opt_x, &pid_filtre))
        return 1;

    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) {
        perror("opendir /proc");
        return 1;
    }

    Processus *procs = NULL;
    int nb_procs = 0, capacite = 0;
    int uid_courant = getuid();
    struct dirent *ent;

    while ((ent = readdir(proc_dir)) != NULL) {
        if (!est_un_nombre(ent->d_name)) continue;
        int pid = atoi(ent->d_name);
        Processus p;
        if (!lire_stat(pid, &p)) continue;
        p.uid = lire_uid(pid);
        lire_cmdline(pid, p.cmdline, sizeof(p.cmdline));

        if (!doit_afficher(&p, opt_a, opt_x, pid_filtre, uid_courant))
            continue;

        if (nb_procs >= capacite) {
            capacite = (capacite == 0) ? 64 : capacite * 2;
            Processus *new = realloc(procs, capacite * sizeof(Processus));
            if (!new) {
                perror("realloc");
                free(procs);
                closedir(proc_dir);
                return 1;
            }
            procs = new;
        }
        procs[nb_procs++] = p;
    }
    closedir(proc_dir);

    qsort(procs, nb_procs, sizeof(Processus), comparer_pid);
    afficher_en_tete(opt_u);
    for (int i = 0; i < nb_procs; i++)
        afficher_ligne(&procs[i], opt_u);

    free(procs);
    return 0;
}