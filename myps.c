/**
 * myps.c - Implémentation de la commande ps sous Linux
 *
 * Options :
 *   -a        : afficher les processus de tous les utilisateurs
 *   -u        : afficher le nom d'utilisateur (et format étendu)
 *   -x        : inclure les processus sans terminal
 *   -e        : équivalent à -a -x (tous les processus)
 *   -f        : affichage complet (colonnes supplémentaires)
 *   -c        : afficher le nom de commande (comm) au lieu de la ligne complète
 *   -H        : affichage hiérarchique (arbre des processus)
 *   -p PID    : n’afficher que le processus de PID donné
 *   -t tty    : filtrer par terminal (ex: pts/0)
 *   -n nom    : filtrer par nom de processus (sous-chaîne)
 *   -Z        : afficher le contexte de sécurité SELinux
 *   -s        : afficher les colonnes PGID et SID
 *   --sort=critère : trier selon pid, ppid, cpu, rss, nom
 *   -C nom    : filtrer par nom exact (commande)
 *   -G GID    : filtrer par groupe réel (RGID)
 *   -j        : format "jobs" (PGID, SID, TTY, TIME)
 *   -L        : afficher les threads (avec TID, NLWP)
 *

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <sys/stat.h>

#define HZ sysconf(_SC_CLK_TCK)

/* Structure pour stocker les informations d'un processus/thread */
typedef struct {
    int pid;                // Process ID (ou TID pour un thread)
    int ppid;               // Parent PID
    int uid;                // User ID
    int gid;                // Group ID réel (RGID)
    char nom[256];          // Nom du processus (comm)
    char etat;              // État (R, S, D, Z, T, ...)
    char tty[32];           // Terminal associé (ex: "pts/0", "?")
    long rss;               // Mémoire résidente (pages de 4 Ko)
    char cmdline[512];      // Ligne de commande complète (avec arguments)
    double cpu;             // Pourcentage CPU
    int pgid;               // Group ID du processus (pgrp)
    int sid;                // Session ID
    double total_cpu_sec;   // Temps CPU total (secondes)
    char scontext[256];     // Contexte SELinux (pour -Z)
    // Pour l'option -L (threads)
    int tgid;               // Thread group ID (PID du processus principal)
    int nlwp;               // Nombre de threads du processus (NLWP)
} Processus;

// Variable globale pour le critère de tri (utilisée par qsort)
char *tri_critere = NULL;

/* ---------- Fonctions utilitaires ---------- */
int est_un_nombre(const char *s) {
    if (!s || !*s) return 0;
    while (*s) if (!isdigit(*s++)) return 0;
    return 1;
}

/* Lit le terminal associé depuis /proc/pid/status (ligne "Tty:") */
void lire_tty(int pid, char *buf, size_t taille) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/status", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) {
        snprintf(buf, taille, "?");
        return;
    }
    char ligne[256];
    while (fgets(ligne, sizeof(ligne), f)) {
        if (strncmp(ligne, "Tty:", 4) == 0) {
            char *p = ligne + 4;
            while (*p == ' ' || *p == '\t') p++;
            char *q = p;
            while (*q && *q != '\n') q++;
            *q = '\0';
            snprintf(buf, taille, "%s", p);
            fclose(f);
            return;
        }
    }
    fclose(f);
    snprintf(buf, taille, "?");
}

/* Lit le contexte SELinux (attr/current) pour l'option -Z */
void lire_contexte_Z(int pid, char *buf, size_t taille) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/attr/current", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) {
        snprintf(buf, taille, "?");
        return;
    }
    if (fgets(buf, taille, f)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
    } else {
        snprintf(buf, taille, "?");
    }
    fclose(f);
}

/* Lit /proc/pid/stat et remplit la structure Processus */
int lire_stat(int pid, Processus *proc) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/stat", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;

    int pid_lu, ppid, pgrp, session, tty_nr;
    char nom[256], etat;
    long rss, utime, stime, starttime;
    long dummy; // pour les champs ignorés

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
    proc->rss = rss;
    proc->pgid = pgrp;
    proc->sid = session;

    // Nettoyage du nom (enlever les parenthèses)
    int len = strlen(nom);
    if (len >= 2 && nom[0] == '(' && nom[len-1] == ')') {
        nom[len-1] = '\0';
        strncpy(proc->nom, nom+1, sizeof(proc->nom)-1);
    } else {
        strncpy(proc->nom, nom, sizeof(proc->nom)-1);
    }
    proc->nom[sizeof(proc->nom)-1] = '\0';

    // Temps CPU total en secondes
    long hz = HZ;
    double total_time = (utime + stime) / (double)hz;
    proc->total_cpu_sec = total_time;

    // Pourcentage CPU
    FILE *up = fopen("/proc/uptime", "r");
    if (up) {
        double uptime;
        fscanf(up, "%lf", &uptime);
        fclose(up);
        double seconds = uptime - (starttime / (double)hz);
        proc->cpu = (seconds > 0.0) ? 100.0 * total_time / seconds : 0.0;
    } else {
        proc->cpu = 0.0;
    }

    // Lecture du terminal (via status)
    lire_tty(pid, proc->tty, sizeof(proc->tty));

    return 1;
}

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

int lire_gid(int pid) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/status", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return -1;
    char ligne[256];
    int gid = -1;
    while (fgets(ligne, sizeof(ligne), f)) {
        if (strncmp(ligne, "Gid:", 4) == 0) {
            sscanf(ligne, "Gid: %d", &gid);
            break;
        }
    }
    fclose(f);
    return gid;
}

/* Récupère le nombre de threads d'un processus (pour NLWP) */
int lire_nlwp(int pid) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/status", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;
    char ligne[256];
    int threads = 0;
    while (fgets(ligne, sizeof(ligne), f)) {
        if (strncmp(ligne, "Threads:", 8) == 0) {
            sscanf(ligne, "Threads: %d", &threads);
            break;
        }
    }
    fclose(f);
    return threads;
}

void lire_cmdline(int pid, char *buf, size_t taille) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/cmdline", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) {
        snprintf(buf, taille, "[defunct]");
        return;
    }
    size_t n = fread(buf, 1, taille-1, f);
    fclose(f);
    if (n == 0) {
        snprintf(buf, taille, "[kernel thread]");
        return;
    }
    for (size_t i = 0; i < n; i++)
        if (buf[i] == '\0') buf[i] = ' ';
    buf[n] = '\0';
}

/* ---------- Filtrage ---------- */
int doit_afficher(Processus *p, int opt_a, int opt_x, int pid_filtre,
                  int uid_courant, const char *tty_filtre, const char *nom_filtre,
                  const char *nom_exact, int gid_filtre) {
    if (pid_filtre && p->pid != pid_filtre) return 0;
    if (!opt_a && p->uid != uid_courant) return 0;
    if (!opt_x && strcmp(p->tty, "?") == 0) return 0;
    if (tty_filtre && strcmp(p->tty, tty_filtre) != 0) return 0;
    if (nom_filtre && strstr(p->nom, nom_filtre) == NULL) return 0;
    if (nom_exact && strcmp(p->nom, nom_exact) != 0) return 0;
    if (gid_filtre != -1 && p->gid != gid_filtre) return 0;
    return 1;
}

/* ---------- Tri (qsort) ---------- */
int comparer_processus(const void *a, const void *b) {
    const Processus *pa = (const Processus*)a;
    const Processus *pb = (const Processus*)b;
    if (tri_critere) {
        if (strcmp(tri_critere, "pid") == 0) return pa->pid - pb->pid;
        if (strcmp(tri_critere, "ppid") == 0) return pa->ppid - pb->ppid;
        if (strcmp(tri_critere, "rss") == 0) return pa->rss - pb->rss;
        if (strcmp(tri_critere, "nom") == 0) return strcmp(pa->nom, pb->nom);
        if (strcmp(tri_critere, "cpu") == 0) {
            if (pa->cpu < pb->cpu) return -1;
            if (pa->cpu > pb->cpu) return 1;
            return 0;
        }
    }
    return pa->pid - pb->pid; // tri par défaut
}

/* ---------- Affichage hiérarchique (arbre) ---------- */
void afficher_arbre(Processus *procs, int nb, int parent, int niveau) {
    for (int i = 0; i < nb; i++) {
        if (procs[i].ppid == parent) {
            for (int j = 0; j < niveau; j++) printf("  ");
            printf("|-- %s (%d)\n", procs[i].nom, procs[i].pid);
            afficher_arbre(procs, nb, procs[i].pid, niveau + 1);
        }
    }
}

/* ---------- En-têtes ---------- */
void afficher_entete(int opt_u, int opt_f, int opt_Z, int opt_s, int opt_j, int opt_L) {
    if (opt_L) {
        printf("%-8s %-8s %-8s %-8s %-8s %-10s %s\n",
               "PID", "TID", "TGID", "NLWP", "TTY", "TIME", "CMD");
        return;
    }
    if (opt_j) {
        printf("%-8s %-8s %-8s %-8s %-10s %s\n",
               "PID", "PGID", "SID", "TTY", "TIME", "CMD");
        return;
    }
    if (opt_f) {
        printf("%-8s %-8s %-8s %-8s %-10s %-8s %-6s %s\n",
               "PID", "PPID", "PGID", "SID", "USER", "MEM(KB)", "%CPU", "CMD");
        return;
    }
    if (opt_u) {
        printf("%-8s %-10s %-20s %-6s %-8s %-8s %-6s %s\n",
               "PID", "USER", "NAME", "STAT", "TTY", "MEM(KB)", "%CPU", "CMD");
        return;
    }
    if (opt_Z || opt_s) {
        if (opt_Z) printf("%-25s ", "LABEL");
        if (opt_s) printf("%-8s %-8s ", "PGID", "SID");
        printf("%-8s %-8s %-10s %s\n", "PID", "TTY", "TIME", "CMD");
        return;
    }
    // Affichage par défaut
    printf("%-8s %-8s %-10s %s\n", "PID", "TTY", "TIME", "CMD");
    printf("%-8s %-8s %-10s %s\n", "---", "---", "----", "---");
}

/* ---------- Affichage d'une ligne ---------- */
void afficher_ligne(Processus *p, int opt_u, int opt_f, int opt_c,
                    int opt_Z, int opt_s, int opt_j, int opt_L) {
    long mem_kb = p->rss * 4;
    struct passwd *pw = getpwuid(p->uid);
    char *user = (pw && pw->pw_name) ? pw->pw_name : "?";
    const char *cmd = opt_c ? p->nom : p->cmdline;
    // Troncature pour l'affichage (éviter les lignes trop longues)
    char cmd_trunc[61];
    strncpy(cmd_trunc, cmd, 60);
    cmd_trunc[60] = '\0';

    // Formatage du temps CPU
    int total_sec = (int)p->total_cpu_sec;
    int minutes = total_sec / 60;
    int secondes = total_sec % 60;
    char temps[16];
    snprintf(temps, sizeof(temps), "%d:%02d", minutes, secondes);

    if (opt_L) {
        printf("%-8d %-8d %-8d %-8d %-8s %-10s %s\n",
               p->tgid, p->pid, p->tgid, p->nlwp, p->tty, temps, cmd_trunc);
        return;
    }
    if (opt_j) {
        printf("%-8d %-8d %-8d %-8s %-10s %s\n",
               p->pid, p->pgid, p->sid, p->tty, temps, cmd_trunc);
        return;
    }
    if (opt_f) {
        printf("%-8d %-8d %-8d %-8d %-10s %-8ld %-5.1f %s\n",
               p->pid, p->ppid, p->pgid, p->sid, user, mem_kb, p->cpu, cmd_trunc);
        return;
    }
    if (opt_u) {
        printf("%-8d %-10s %-20s %-6c %-8s %-8ld %-5.1f %s\n",
               p->pid, user, p->nom, p->etat, p->tty, mem_kb, p->cpu, cmd_trunc);
        return;
    }
    // Affichage avec -Z ou -s (sans -f -u)
    if (opt_Z) printf("%-25s ", p->scontext);
    if (opt_s) printf("%-8d %-8d ", p->pgid, p->sid);
    printf("%-8d %-8s %-10s %s\n", p->pid, p->tty, temps, cmd_trunc);
}

/* ---------- Analyse des options (sans getopt, mais complet) ---------- */
int parse_args(int argc, char **argv,
               int *opt_a, int *opt_u, int *opt_x, int *opt_e,
               int *opt_f, int *opt_c, int *opt_H,
               int *pid_filtre, int *opt_Z, int *opt_s,
               char **tty_filtre, char **nom_filtre, char **sort_critere,
               char **nom_exact, int *gid_filtre,
               int *opt_j, int *opt_L) {
    // Initialisation
    *opt_a = *opt_u = *opt_x = *opt_e = *opt_f = *opt_c = *opt_H = 0;
    *opt_Z = *opt_s = *opt_j = *opt_L = 0;
    *pid_filtre = 0;
    *tty_filtre = NULL;
    *nom_filtre = NULL;
    *nom_exact = NULL;
    *sort_critere = NULL;
    *gid_filtre = -1;

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        // Option longue --sort
        if (strncmp(arg, "--sort=", 7) == 0) {
            *sort_critere = arg + 7;
            tri_critere = *sort_critere;
            continue;
        }
        if (arg[0] != '-') {
            fprintf(stderr, "Argument invalide : %s\n", arg);
            return 0;
        }
        // Options avec argument
        if (strcmp(arg, "-p") == 0) {
            if (++i >= argc) return 0;
            *pid_filtre = atoi(argv[i]);
            if (*pid_filtre <= 0) return 0;
            continue;
        }
        if (strcmp(arg, "-t") == 0) {
            if (++i >= argc) return 0;
            *tty_filtre = argv[i];
            continue;
        }
        if (strcmp(arg, "-n") == 0) {
            if (++i >= argc) return 0;
            *nom_filtre = argv[i];
            continue;
        }
        if (strcmp(arg, "-C") == 0) {
            if (++i >= argc) return 0;
            *nom_exact = argv[i];
            continue;
        }
        if (strcmp(arg, "-G") == 0) {
            if (++i >= argc) return 0;
            *gid_filtre = atoi(argv[i]);
            if (*gid_filtre < 0) return 0;
            continue;
        }
        // Options sans argument (groupées possibles)
        for (int j = 1; arg[j]; j++) {
            char c = arg[j];
            switch (c) {
                case 'a': *opt_a = 1; break;
                case 'u': *opt_u = 1; break;
                case 'x': *opt_x = 1; break;
                case 'e': *opt_e = 1; *opt_a = 1; *opt_x = 1; break;
                case 'f': *opt_f = 1; break;
                case 'c': *opt_c = 1; break;
                case 'H': *opt_H = 1; break;
                case 'Z': *opt_Z = 1; break;
                case 's': *opt_s = 1; break;
                case 'j': *opt_j = 1; break;
                case 'L': *opt_L = 1; break;
                default:
                    fprintf(stderr, "Option inconnue : -%c\n", c);
                    return 0;
            }
        }
    }
    return 1;
}

/* ---------- Gestion des threads pour -L ---------- */
int lister_threads(int pid, int **tids) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/task", pid);
    DIR *d = opendir(chemin);
    if (!d) return 0;
    struct dirent *ent;
    int cap = 8, nb = 0;
    *tids = malloc(cap * sizeof(int));
    if (!*tids) { closedir(d); return 0; }
    while ((ent = readdir(d)) != NULL) {
        if (!est_un_nombre(ent->d_name)) continue;
        int tid = atoi(ent->d_name);
        if (nb >= cap) {
            cap *= 2;
            int *tmp = realloc(*tids, cap * sizeof(int));
            if (!tmp) { free(*tids); closedir(d); return 0; }
            *tids = tmp;
        }
        (*tids)[nb++] = tid;
    }
    closedir(d);
    return nb;
}

/* ---------- Main ---------- */
int main(int argc, char **argv) {
    int opt_a, opt_u, opt_x, opt_e, opt_f, opt_c, opt_H;
    int pid_filtre, opt_Z, opt_s, opt_j, opt_L;
    char *tty_filtre = NULL, *nom_filtre = NULL, *sort_critere = NULL;
    char *nom_exact = NULL;
    int gid_filtre = -1;

    if (!parse_args(argc, argv,
                    &opt_a, &opt_u, &opt_x, &opt_e,
                    &opt_f, &opt_c, &opt_H,
                    &pid_filtre, &opt_Z, &opt_s,
                    &tty_filtre, &nom_filtre, &sort_critere,
                    &nom_exact, &gid_filtre,
                    &opt_j, &opt_L))
        return 1;

    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) { perror("/proc"); return 1; }

    Processus *procs = NULL;
    int nb_procs = 0, capacite = 0;
    int uid_courant = getuid();
    struct dirent *ent;

    // Si -L, on parcourt les threads
    if (opt_L) {
        while ((ent = readdir(proc_dir)) != NULL) {
            if (!est_un_nombre(ent->d_name)) continue;
            int pid = atoi(ent->d_name);
            int *tids = NULL;
            int nb_threads = lister_threads(pid, &tids);
            if (nb_threads == 0) continue;
            int nlwp = nb_threads;
            for (int i = 0; i < nb_threads; i++) {
                int tid = tids[i];
                Processus p;
                if (!lire_stat(tid, &p)) continue;
                p.uid = lire_uid(tid);
                p.gid = lire_gid(tid);
                p.tgid = pid;
                p.nlwp = nlwp;
                lire_cmdline(tid, p.cmdline, sizeof(p.cmdline));
                if (opt_Z) lire_contexte_Z(tid, p.scontext, sizeof(p.scontext));
                // Filtres
                if (!doit_afficher(&p, opt_a, opt_x, pid_filtre, uid_courant,
                                   tty_filtre, nom_filtre, nom_exact, gid_filtre))
                    continue;
                if (nb_procs >= capacite) {
                    capacite = (capacite == 0) ? 64 : capacite * 2;
                    Processus *tmp = realloc(procs, capacite * sizeof(Processus));
                    if (!tmp) { free(procs); free(tids); closedir(proc_dir); return 1; }
                    procs = tmp;
                }
                procs[nb_procs++] = p;
            }
            free(tids);
        }
    } else {
        // Parcours normal des processus
        while ((ent = readdir(proc_dir)) != NULL) {
            if (!est_un_nombre(ent->d_name)) continue;
            int pid = atoi(ent->d_name);
            Processus p;
            if (!lire_stat(pid, &p)) continue;
            p.uid = lire_uid(pid);
            p.gid = lire_gid(pid);
            lire_cmdline(pid, p.cmdline, sizeof(p.cmdline));
            if (opt_Z) lire_contexte_Z(pid, p.scontext, sizeof(p.scontext));
            if (!doit_afficher(&p, opt_a, opt_x, pid_filtre, uid_courant,
                               tty_filtre, nom_filtre, nom_exact, gid_filtre))
                continue;
            if (nb_procs >= capacite) {
                capacite = (capacite == 0) ? 64 : capacite * 2;
                Processus *tmp = realloc(procs, capacite * sizeof(Processus));
                if (!tmp) { free(procs); closedir(proc_dir); return 1; }
                procs = tmp;
            }
            procs[nb_procs++] = p;
        }
    }
    closedir(proc_dir);

    // Tri
    qsort(procs, nb_procs, sizeof(Processus), comparer_processus);

    // Affichage
    if (opt_H && !opt_L) {
        printf("ARBRE DES PROCESSUS\n");
        afficher_arbre(procs, nb_procs, 1, 0);
    } else {
        afficher_entete(opt_u, opt_f, opt_Z, opt_s, opt_j, opt_L);
        for (int i = 0; i < nb_procs; i++) {
            afficher_ligne(&procs[i], opt_u, opt_f, opt_c, opt_Z, opt_s, opt_j, opt_L);
        }
    }

    free(procs);
    return 0;
}
