/**
 * my_ps.c - Implémentation de la commande ps sous Linux
 * Compilation : gcc -Wall -Wextra -O2 -o my_ps my_ps.c
 * Utilisation : ./my_ps [-a] [-u] [-x] [-p PID] [-C nom] [-G GID] [-j]
 *
 * Options :
 *   -a : afficher les processus de tous les utilisateurs
 *   -u : afficher également le nom d'utilisateur
 *   -x : inclure les processus sans terminal (démons)
 *   -p PID : n'afficher que le processus spécifié
 *   -C nom : n'afficher que les processus dont le nom correspond
 *   -G GID : n'afficher que les processus dont le groupe réel (RGID) correspond
 *   -j : afficher au format jobs (PGID, SID, TTY, TIME)
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
#include <sys/stat.h>   /* pour stat() — résolution TTY */
#include <bits/getopt_core.h>

#define HZ sysconf(_SC_CLK_TCK)   /* ticks par seconde (généralement 100) */

/* Structure pour stocker les informations d'un processus */
typedef struct {
    int pid;
    int ppid;
    int uid;
    int gid;              /* groupe réel (RGID) */
    char nom[256];
    char etat;
    int tty;
    long rss;               /* en pages (4 Ko) */
    char cmdline[512];
    double cpu;             /* pourcentage CPU */
    int pgid;
    int sid;
    double total_cpu_sec;   /* Temps CPU total en secondes */
    char selinux[256];      // contexte SELinux (option -Z)
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

    /* Calcul du %CPU et du temps total CPU (total_time) */
    long hz = HZ;
    double total_time = (utime + stime) / (double)hz;  // temps CPU total en secondes
    proc->total_cpu_sec = total_time;                  // stockage pour l'option -j

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
    // Initialiser le contexte SELinux à vide 
    proc->selinux[0] = '\0';
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

int lire_gid(int pid) {
    char chemin[512];
    snprintf(chemin, sizeof(chemin), "/proc/%d/status", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return -1;
    char line[256];
    int gid = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Gid:", 4) == 0) {
            sscanf(line, "Gid: %d", &gid);
            break;
        }
    }
    fclose(f);
    return gid;
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

//lire_selinux() - Option -Z
//Lit le contexte SELinux depuis /proc/<pid>/attr/current
//Si SELinux est désactivé ou non supporté, le fichier est vide ou absent
void lire_selinux(int pid, char *buf, size_t size) {
    char chemin[512];
    snprintf(chemin, sizeof(chemin), "/proc/%d/attr/current", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) {
        strncpy(buf, "-", size);
        return;
    }
     size_t n = fread(buf, 1, size - 1, f);
    fclose(f);
    if (n == 0) {
        strncpy(buf, "-", size);
        return;
    }
    buf[n] = '\0';
    //Supprimer le '\n' final s'il existe 
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    //Si le contenu est vide après nettoyage 
    if (buf[0] == '\0') strncpy(buf, "-", size);
}

/*
 resoudre_tty() - Option -t
 Convertit un nom de terminal (ex: "pts/0", "tty1") en numéro de device
 en appelant stat() sur /dev/<tty_name>.
 Retourne le numéro de device (dev_t) ou 0 si échec.
 */

 dev_t resoudre_tty(const char *tty_name) {
    char chemin[512];
    /* Essayer d'abord /dev/<nom> */
    snprintf(chemin, sizeof(chemin), "/dev/%s", tty_name);
    struct stat st;
    if (stat(chemin, &st) == 0 && (S_ISCHR(st.st_mode))) {
        return st.st_rdev;
    }
    return 0;
}


/* ---------- Filtrage et affichage ---------- */

int doit_afficher(Processus *p, int opt_a, int opt_x, int pid_filtre,
                  int uid_courant, int sid_filtre, dev_t tty_filtre) {
    if (pid_filtre != 0 && p->pid != pid_filtre) return 0;
    if (!opt_a && p->uid != uid_courant) return 0;
    if (!opt_x && p->tty == 0) return 0;
    // Filtre -s : Session ID 
    if (sid_filtre != 0 && p->sid != sid_filtre) return 0;
    /* Filtre -t : terminal (comparaison par numéro de device) */
    if (tty_filtre != 0 && (dev_t)p->tty != tty_filtre) return 0;
    return 1;
}

int comparer_pid(const void *a, const void *b) {
    return ((Processus*)a)->pid - ((Processus*)b)->pid;
}

void afficher_en_tete(int opt_u, int opt_j, int opt_n, int opt_Z) {
    /* Colonne USER/UID selon -n */
    const char *col_user     = opt_n ? "UID"  : "USER";
    const char *sep_user     = opt_n ? "---"  : "----";
 if (opt_Z) {
        /* -Z : on ajoute la colonne LABEL en dernier */
        if (opt_u || opt_n) {
            printf("%-8s %-12s %-20s %-6s %-6s %-8s %-6s %-30s %s\n",
                   "PID", col_user, "NOM", "ÉTAT", "PPID", "MEM(KB)", "%CPU", "LABEL", "COMMANDE");
            printf("%-8s %-12s %-20s %-6s %-6s %-8s %-6s %-30s %s\n",
                   "---", sep_user, "---", "----", "----", "-------", "----", "-----", "--------");
        } else if (opt_j) {
            printf("%-8s %-8s %-8s %-8s %-10s %-30s %s\n",
                   "PID", "PGID", "SID", "TTY", "TIME", "LABEL", "CMD");
            printf("%-8s %-8s %-8s %-8s %-10s %-30s %s\n",
                   "---", "----", "---", "---", "----", "-----", "---");
        } else {
            printf("%-8s %-20s %-6s %-6s %-8s %-6s %-30s %s\n",
                   "PID", "NOM", "ÉTAT", "PPID", "MEM(KB)", "%CPU", "LABEL", "COMMANDE");
            printf("%-8s %-20s %-6s %-6s %-8s %-6s %-30s %s\n",
                   "---", "---", "----", "----", "-------", "----", "-----", "--------");
        }
    } else if (opt_u || opt_n) {
        printf("%-8s %-12s %-20s %-6s %-6s %-8s %-6s %s\n",
               "PID", col_user, "NOM", "ÉTAT", "PPID", "MEM(KB)", "%CPU", "COMMANDE");
        printf("%-8s %-12s %-20s %-6s %-6s %-8s %-6s %s\n",
               "---", sep_user, "---", "----", "----", "-------", "----", "--------");
    } else if (opt_j) {
        printf("%-8s %-8s %-8s %-8s %-10s %s\n",
               "PID", "PGID", "SID", "TTY", "TIME", "CMD");
        printf("%-8s %-8s %-8s %-8s %-10s %s\n",
               "---", "----", "---", "---", "----", "---");
    } else {
        printf("%-8s %-20s %-6s %-6s %-8s %-6s %s\n",
               "PID", "NOM", "ÉTAT", "PPID", "MEM(KB)", "%CPU", "COMMANDE");
        printf("%-8s %-20s %-6s %-6s %-8s %-6s %s\n",
               "---", "---", "----", "----", "-------", "----", "--------");
    }
}


void afficher_ligne(Processus *p, int opt_u, int opt_j,int opt_n, int opt_Z) {
    long mem_kb = p->rss * 4;

    /* Préparation de la colonne USER/UID */
    char col_user[32];
    if (opt_n) {
        /* -n : UID numérique */
        snprintf(col_user, sizeof(col_user), "%d", p->uid);
    } else {
        struct passwd *pw = getpwuid(p->uid);
        strncpy(col_user, (pw && pw->pw_name) ? pw->pw_name : "?", sizeof(col_user)-1);
        col_user[sizeof(col_user)-1] = '\0';
    }

    if (opt_Z) {
        /* Colonne LABEL insérée avant COMMANDE */
        if (opt_u || opt_n) {
            printf("%-8d %-12s %-20s %-6c %-6d %-8ld %-5.1f %-30s %s\n",
                   p->pid, col_user, p->nom, p->etat, p->ppid,
                   mem_kb, p->cpu, p->selinux, p->cmdline);
        } else if (opt_j) {
            int total_sec = (int)p->total_cpu_sec;
            char temps_str[16];
            snprintf(temps_str, sizeof(temps_str), "%d:%02d", total_sec/60, total_sec%60);
            printf("%-8d %-8d %-8d %-8d %-10s %-30s %s\n",
                   p->pid, p->pgid, p->sid, p->tty,
                   temps_str, p->selinux, p->cmdline);
        } else {
            printf("%-8d %-20s %-6c %-6d %-8ld %-5.1f %-30s %s\n",
                   p->pid, p->nom, p->etat, p->ppid,
                   mem_kb, p->cpu, p->selinux, p->cmdline);
        }
    } else if (opt_u || opt_n) {
        printf("%-8d %-12s %-20s %-6c %-6d %-8ld %-5.1f %s\n",
               p->pid, col_user, p->nom, p->etat, p->ppid,
               mem_kb, p->cpu, p->cmdline);
    } else if (opt_j) {
        int total_sec = (int)p->total_cpu_sec;
        char temps_str[16];
        snprintf(temps_str, sizeof(temps_str), "%d:%02d", total_sec/60, total_sec%60);
        printf("%-8d %-8d %-8d %-8d %-10s %s\n",
               p->pid, p->pgid, p->sid, p->tty, temps_str, p->cmdline);
    } else {
        printf("%-8d %-20s %-6c %-6d %-8ld %-5.1f %s\n",
               p->pid, p->nom, p->etat, p->ppid, mem_kb, p->cpu, p->cmdline);
    }
}

/* ---------- Fonction principale ---------- */
int main(int argc, char **argv) {
    int opt_a = 0, opt_u = 0, opt_x = 0, pid_filtre = 0, opt_j = 0;
    int opt_n = 0, opt_Z = 0;          /* nouvelles options */
    int sid_filtre = 0;                 /* -s SID */
    dev_t tty_filtre = 0;              /* -t TTY (résolu en dev_t) */
    char nom_filter[256] = "";
    int gid_cible = -1;

    int opt;
    while ((opt = getopt(argc, argv, "auxp:C:G:js:t:nZ")) != -1) {
        switch (opt) {
            case 'a': opt_a = 1; break;
            case 'u': opt_u = 1; break;
            case 'x': opt_x = 1; break;
            case 'p':
                pid_filtre = atoi(optarg);
                if (pid_filtre <= 0) {
                    fprintf(stderr, "Erreur: PID invalide '%s'\n", optarg);
                    return 1;
                }
                break;
            case 'C':
                strncpy(nom_filter, optarg, sizeof(nom_filter)-1);
                nom_filter[sizeof(nom_filter)-1] = '\0';
                break;
            case 'G':
                gid_cible = atoi(optarg);
                if (gid_cible < 0) {
                    fprintf(stderr, "Erreur: GID invalide '%s'\n", optarg);
                    return 1;
                }
                break;
            case 'j': opt_j = 1; break;

            /* ---- Nouvelles options ---- */

            case 's':
                /* -s SID : filtrer par Session ID */
                sid_filtre = atoi(optarg);
                if (sid_filtre <= 0) {
                    fprintf(stderr, "Erreur: SID invalide '%s'\n", optarg);
                    return 1;
                }
                break;

            case 't':
                /*
                 * -t TTY : filtrer par terminal
                 * On résout le nom (ex: pts/0) en numéro de device via stat()
                 * pour pouvoir comparer avec le champ tty_nr de /proc/PID/stat.
                 */
                tty_filtre = resoudre_tty(optarg);
                if (tty_filtre == 0) {
                    fprintf(stderr, "Erreur: terminal introuvable '/dev/%s'\n", optarg);
                    return 1;
                }
                break;

            case 'n':
                /* -n : afficher l'UID en numérique, pas de résolution par getpwuid() */
                opt_n = 1;
                break;

            case 'Z':
                /* -Z : afficher le contexte SELinux depuis /proc/PID/attr/current */
                opt_Z = 1;
                break;

            default:
                fprintf(stderr,
                    "Usage: %s [-a] [-u] [-x] [-p PID] [-C nom] [-G GID] [-j]\n"
                    "           [-s SID] [-t TTY] [-n] [-Z]\n", argv[0]);
                return 1;
        }
    }

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
        p.gid = lire_gid(pid);
        lire_cmdline(pid, p.cmdline, sizeof(p.cmdline));

        /* Lecture du contexte SELinux si l'option -Z est active */
        if (opt_Z)
            lire_selinux(pid, p.selinux, sizeof(p.selinux));

        if (nom_filter[0] != '\0' && strcmp(p.nom, nom_filter) != 0)
            continue;
        if (gid_cible != -1 && p.gid != gid_cible)
            continue;
        if (!doit_afficher(&p, opt_a, opt_x, pid_filtre,
                           uid_courant, sid_filtre, tty_filtre))
            continue;

        if (nb_procs >= capacite) {
            capacite = (capacite == 0) ? 64 : capacite * 2;
            Processus *new_procs = realloc(procs, capacite * sizeof(Processus));
            if (!new_procs) {
                perror("realloc");
                free(procs);
                closedir(proc_dir);
                return 1;
            }
            procs = new_procs;
        }
        procs[nb_procs++] = p;
    }
    closedir(proc_dir);

    qsort(procs, nb_procs, sizeof(Processus), comparer_pid);
    afficher_en_tete(opt_u, opt_j, opt_n, opt_Z);
    for (int i = 0; i < nb_procs; i++)
        afficher_ligne(&procs[i], opt_u, opt_j, opt_n, opt_Z);

    free(procs);
    return 0;
}