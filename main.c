#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include "proc.h"

/* ════════════════════════════════════════════════════════════════
   UTILITAIRES
   ════════════════════════════════════════════════════════════════ */

int est_un_nombre(const char *s) {
    if (!s || !*s) return 0;
    while (*s)
        if (!isdigit(*s++)) return 0;
    return 1;
}

const char *uid_to_name(int uid) {
    struct passwd *pw = getpwuid(uid);
    return pw ? pw->pw_name : "?";
}

/* ════════════════════════════════════════════════════════════════
   LECTURE /proc
   ════════════════════════════════════════════════════════════════ */

int read_stat(int pid, ProcessInfo *proc) {
    char chemin[MAX_CHEMIN];
    snprintf(chemin, sizeof(chemin), "/proc/%d/stat", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;

    long dummy;
    int  pgrp, session;
    long vsize;
    int  priority, nice;
    long utime, stime, starttime;
    char etat;
    int  ppid;

    int lu = fscanf(f,
        "%d (%255[^)])"
        " %c %d"
        " %d %d"
        " %ld %ld"
        " %ld %ld %ld %ld %ld"
        " %ld %ld"
        " %ld %ld"
        " %d %d"
        " %ld %ld"
        " %ld"
        " %ld",
        &proc->pid, proc->nom,
        &etat, &ppid,
        &pgrp, &session,
        &dummy, &dummy,
        &dummy, &dummy, &dummy, &dummy, &dummy,
        &utime, &stime,
        &dummy, &dummy,
        &priority, &nice,
        &dummy, &dummy,
        &starttime,
        &vsize);
    fclose(f);
    if (lu < 22) return 0;

    proc->etat      = etat;
    proc->ppid      = ppid;
    proc->pgrp      = pgrp;
    proc->session   = session;
    proc->utime     = utime;
    proc->stime     = stime;
    proc->starttime = starttime;
    proc->vsize     = vsize / 1024;
    proc->priority  = priority;
    proc->nice      = nice;
    return 1;
}

int read_uid(int pid) {
    char chemin[MAX_CHEMIN];
    char ligne[256];
    int uid = -1;
    snprintf(chemin, sizeof(chemin), "/proc/%d/status", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return -1;
    while (fgets(ligne, sizeof(ligne), f))
        if (strncmp(ligne, "Uid:", 4) == 0) {
            sscanf(ligne + 4, "%d", &uid);
            break;
        }
    fclose(f);
    return uid;
}

/* ════════════════════════════════════════════════════════════════
   CALCUL CPU
   ════════════════════════════════════════════════════════════════ */

double calcul_cpu(ProcessInfo *proc, double uptime) {
    long hz    = sysconf(_SC_CLK_TCK);
    double total   = (proc->utime + proc->stime) / (double)hz;
    double elapsed = uptime - (proc->starttime   / (double)hz);
    if (elapsed <= 0) return 0.0;
    return 100.0 * total / elapsed;
}

/* ════════════════════════════════════════════════════════════════
   TRI
   ════════════════════════════════════════════════════════════════ */

int comparer_pid(const void *a, const void *b) {
    return ((ProcessInfo *)a)->pid - ((ProcessInfo *)b)->pid;
}

/* ════════════════════════════════════════════════════════════════
   AFFICHAGE
   ════════════════════════════════════════════════════════════════ */

void afficher_normal(ProcessInfo *p, double cpu) {
    printf("%-8d %-15s %-20s %-6c %5.1f%%\n",
           p->pid, uid_to_name(p->uid), p->nom, p->etat, cpu);
}

void afficher_long(ProcessInfo *p, double cpu) {
    printf("%-6d %-6d %-6d %-6d %-8s %-20s %-6c %4d %4d %8ld %5.1f%%\n",
           p->pid, p->ppid, p->pgrp, p->session,
           uid_to_name(p->uid), p->nom, p->etat,
           p->priority, p->nice, p->vsize, cpu);
}

void afficher_long_y(ProcessInfo *p, double cpu) {
    char flags[8] = {0};
    int  fi = 0;
    flags[fi++] = p->etat;
    if (p->nice  < 0)          flags[fi++] = '<';
    if (p->nice  > 0)          flags[fi++] = 'N';
    if (p->pgrp == p->pid)     flags[fi++] = 's';
    flags[fi] = '\0';

    printf("%-6d %-6d %-6d %-6d %-8s %-20s %-8s %4d %8ld %5.1f%%\n",
           p->pid, p->ppid, p->pgrp, p->session,
           uid_to_name(p->uid), p->nom, flags,
           p->nice, p->vsize, cpu);
}

void afficher_large(ProcessInfo *p, double cpu) {
    printf("%-8d %-15s %-40s %-6c %5.1f%%\n",
           p->pid, uid_to_name(p->uid), p->nom, p->etat, cpu);
}

/* ════════════════════════════════════════════════════════════════
   MAIN
   ════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {

    int opt_q  = 0;
    int opt_g  = 0;
    int opt_l  = 0;
    int opt_y  = 0;
    int opt_w  = 0;
    int pid_q  = 0;
    int grp_g  = 0;
    int opt;

    while ((opt = getopt(argc, argv, "q:g:lyw")) != -1) {
        switch (opt) {
            case 'q': opt_q = 1; pid_q = atoi(optarg); break;
            case 'g': opt_g = 1; grp_g = atoi(optarg); break;
            case 'l': opt_l = 1;                        break;
            case 'y': opt_y = 1;                        break;
            case 'w': opt_w = 1;                        break;
            default:
                fprintf(stderr,
                    "Usage: %s [-q pid] [-g grp] [-l] [-y] [-w]\n",
                    argv[0]);
                return 1;
        }
    }

    double uptime = 0;
    FILE *fu = fopen("/proc/uptime", "r");
    if (fu) { fscanf(fu, "%lf", &uptime); fclose(fu); }

    int my_uid = getuid();

    /* ── Option -q : accès direct ── */
    if (opt_q) {
        ProcessInfo proc;
        if (!read_stat(pid_q, &proc)) {
            fprintf(stderr, "Erreur : PID %d introuvable\n", pid_q);
            return 1;
        }
        proc.uid = read_uid(pid_q);
        double cpu = calcul_cpu(&proc, uptime);
        printf("%-8s %-15s %-20s %-6s %6s\n",
               "PID","USER","NOM","ETAT","%CPU");
        printf("%-8s %-15s %-20s %-6s %6s\n",
               "--------","---------------","--------------------",
               "------","------");
        afficher_normal(&proc, cpu);
        return 0;
    }

    /* ── Cas général : parcourir /proc ── */
    static ProcessInfo tous[MAX_PROCS];
    int nb = 0;

    DIR           *proc_dir = opendir("/proc");
    struct dirent *entree;
    if (!proc_dir) { perror("opendir"); return 1; }

    while ((entree = readdir(proc_dir)) != NULL && nb < MAX_PROCS) {
        if (!est_un_nombre(entree->d_name)) continue;
        int pid = atoi(entree->d_name);
        if (!read_stat(pid, &tous[nb]))     continue;
        tous[nb].uid = read_uid(pid);

        if (opt_g && tous[nb].pgrp != grp_g && tous[nb].session != grp_g)
            continue;
        if (!opt_g && tous[nb].uid != my_uid)
            continue;

        nb++;
    }
    closedir(proc_dir);

    qsort(tous, nb, sizeof(ProcessInfo), comparer_pid);

    /* ── En-tête ── */
    if (opt_l && opt_y)
        printf("%-6s %-6s %-6s %-6s %-8s %-20s %-8s %4s %8s %6s\n",
               "PID","PPID","PGRP","SESS","USER","NOM","FLAGS","NI","VSZ","%CPU");
    else if (opt_l)
        printf("%-6s %-6s %-6s %-6s %-8s %-20s %-6s %4s %4s %8s %6s\n",
               "PID","PPID","PGRP","SESS","USER","NOM","ETAT","PRI","NI","VSZ","%CPU");
    else if (opt_w)
        printf("%-8s %-15s %-40s %-6s %6s\n",
               "PID","USER","NOM","ETAT","%CPU");
    else
        printf("%-8s %-15s %-20s %-6s %6s\n",
               "PID","USER","NOM","ETAT","%CPU");

    /* ── Affichage ── */
    for (int i = 0; i < nb; i++) {
        double cpu = calcul_cpu(&tous[i], uptime);
        if      (opt_l && opt_y) afficher_long_y(&tous[i], cpu);
        else if (opt_l)          afficher_long(&tous[i], cpu);
        else if (opt_w)          afficher_large(&tous[i], cpu);
        else                     afficher_normal(&tous[i], cpu);
    }

    return 0;
}
