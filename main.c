#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include "proc.h"

int est_un_nombre(const char *s) {
    if (!s || !*s) return 0;
    while (*s)
        if (!isdigit(*s++)) return 0;
    return 1;
}

int read_stat(int pid, ProcessInfo *proc) {
    char chemin[MAX_CHEMIN];
    snprintf(chemin, sizeof(chemin), "/proc/%d/stat", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;

    /* Lire pid et nom jusqu'a ')' pour geree les noms avec espaces/tirets */
    if (fscanf(f, "%d (%255[^)])", &proc->pid, proc->nom) < 2) {
        fclose(f); return 0;
    }

    long utime, stime, starttime, dummy;
    int  ppid;
    char etat;

    int lu = fscanf(f,
        " %c %d"
        " %ld %ld %ld %ld"
        " %ld %ld %ld %ld %ld"
        " %ld %ld"
        " %ld %ld %ld"
        " %ld %ld %ld %ld"
        " %ld",
        &etat, &ppid,
        &dummy, &dummy, &dummy, &dummy,
        &dummy, &dummy, &dummy, &dummy, &dummy,
        &utime, &stime,
        &dummy, &dummy, &dummy,
        &dummy, &dummy, &dummy, &dummy,
        &starttime);
    fclose(f);
    if (lu < 21) return 0;

    proc->etat      = etat;
    proc->ppid      = ppid;
    proc->utime     = utime;
    proc->stime     = stime;
    proc->starttime = starttime;
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

const char *uid_to_name(int uid) {
    struct passwd *pw = getpwuid(uid);
    return pw ? pw->pw_name : "?";
}

int doit_afficher(ProcessInfo *proc, int opt_a, int pid_filtre, int my_uid) {
    if (pid_filtre != 0 && proc->pid != pid_filtre) return 0;
    if (!opt_a && proc->uid != my_uid)               return 0;
    return 1;
}

double calcul_cpu(ProcessInfo *proc, double uptime) {
    long hz = sysconf(_SC_CLK_TCK);
    double total   = (proc->utime + proc->stime) / (double)hz;
    double elapsed = uptime - (proc->starttime   / (double)hz);
    if (elapsed <= 0) return 0.0;
    return 100.0 * total / elapsed;
}

int comparer_pid(const void *a, const void *b) {
    return ((ProcessInfo *)a)->pid - ((ProcessInfo *)b)->pid;
}

int main(int argc, char *argv[]) {
    int opt_a      = 0;
    int pid_filtre = 0;
    int opt;

    while ((opt = getopt(argc, argv, "ap:")) != -1) {
        switch (opt) {
            case 'a': opt_a = 1;                  break;
            case 'p': pid_filtre = atoi(optarg);   break;
            default:
                fprintf(stderr, "Usage: %s [-a] [-p PID]\n", argv[0]);
                return 1;
        }
    }

    double uptime = 0;
    FILE *fu = fopen("/proc/uptime", "r");
    if (fu) { fscanf(fu, "%lf", &uptime); fclose(fu); }

    int my_uid = getuid();

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
        if (!doit_afficher(&tous[nb], opt_a, pid_filtre, my_uid)) continue;
        nb++;
    }
    closedir(proc_dir);

    qsort(tous, nb, sizeof(ProcessInfo), comparer_pid);

    printf("%-8s %-15s %-20s %-6s %6s\n",
           "PID", "USER", "NOM", "ETAT", "%CPU");
    printf("%-8s %-15s %-20s %-6s %6s\n",
           "--------", "---------------", "--------------------",
           "------", "------");

    for (int i = 0; i < nb; i++) {
        double cpu = calcul_cpu(&tous[i], uptime);
        printf("%-8d %-15s %-20s %-6c %5.1f%%\n",
               tous[i].pid,
               uid_to_name(tous[i].uid),
               tous[i].nom,
               tous[i].etat,
               cpu);
    }

    return 0;
}
