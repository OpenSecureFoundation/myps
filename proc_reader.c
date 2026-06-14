#include "proc_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HZ sysconf(_SC_CLK_TCK)

int lire_stat(int pid, Processus *proc) {
    char chemin[512];
    snprintf(chemin, sizeof(chemin), "/proc/%d/stat", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;

    int  pid_lu, ppid, pgrp, session, tty_nr;
    char nom[256];
    char etat;
    long rss, utime, stime, starttime, dummy;

    int n = fscanf(f,
        "%d %255s %c %d %d %d %d"
        " %ld %ld %ld %ld %ld %ld %ld"
        " %ld %ld %ld %ld %ld %ld %ld"
        " %ld %ld %ld %ld %ld",
        &pid_lu, nom, &etat, &ppid, &pgrp, &session, &tty_nr,
        &dummy, &dummy, &dummy, &dummy, &proc->majfl, &dummy, &dummy,
        &utime, &stime, &dummy, &dummy, &dummy, &dummy, &dummy,
        &starttime, &proc->trs, &proc->drs, &dummy, &rss);
    fclose(f);

    if (n < 24) return 0;

    proc->pid  = pid_lu;
    proc->ppid = ppid;
    proc->tty  = tty_nr;
    proc->rss  = rss;
    proc->etat = etat;
    proc->pgid = pgrp;
    proc->sid  = session;
    proc->utime = utime;
    proc->stime = stime;

    int len = strlen(nom);
    if (len >= 2 && nom[0] == '(' && nom[len-1] == ')') {
        nom[len-1] = '\0';
        strncpy(proc->nom, nom + 1, sizeof(proc->nom) - 1);
        proc->nom[sizeof(proc->nom)-1] = '\0';
    } else {
        strncpy(proc->nom, nom, sizeof(proc->nom)-1);
        proc->nom[sizeof(proc->nom)-1] = '\0';
    }

    long   hz         = HZ;
    double total_time = (utime + stime) / (double)hz;
    proc->total_cpu_sec = total_time;

    double uptime = 0;
    FILE *up = fopen("/proc/uptime", "r");
    if (up) {
        if (fscanf(up, "%lf", &uptime) == 1) {
            double age = uptime - (starttime / (double)hz);
            proc->cpu = (age > 0.0) ? 100.0 * total_time / age : 0.0;
        }
        fclose(up);
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

long lire_vmrss(int pid) {
    char chemin[512];
    snprintf(chemin, sizeof(chemin), "/proc/%d/status", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;
    char line[256];
    long vmrss = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line, "VmRSS: %ld", &vmrss);
            break;
        }
    }
    fclose(f);
    return vmrss;
}

int lire_statm(int pid, unsigned long *trs, unsigned long *drs) {
    char chemin[512];
    snprintf(chemin, sizeof(chemin), "/proc/%d/statm", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;
    long size, rss, shared, text, lib, data;
    if (fscanf(f, "%ld %ld %ld %ld %ld %ld",
               &size, &rss, &shared, &text, &lib, &data) == 6) {
        *trs = (unsigned long)text * 4;
        *drs = (unsigned long)data * 4;
    }
    fclose(f);
    return 1;
}

void lire_cmdline(int pid, char *buf, size_t size) {
    char chemin[512];
    snprintf(chemin, sizeof(chemin), "/proc/%d/cmdline", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) { snprintf(buf, size, "[defunct]"); return; }
    size_t n = fread(buf, 1, size-1, f);
    fclose(f);
    if (n == 0) { snprintf(buf, size, "[kernel thread]"); return; }
    for (size_t i = 0; i < n-1; i++)
        if (buf[i] == '\0') buf[i] = ' ';
    buf[n] = '\0';
}