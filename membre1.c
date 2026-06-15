
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

    /* Calcul du %CPU et du temps total CPU (total_time) */
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

