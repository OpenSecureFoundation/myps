#include "display.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

void decoder_tty(int tty_nr, char *buf, size_t size) {
    if (tty_nr == 0) { snprintf(buf, size, "?"); return; }
    int majeur = (tty_nr >> 8) & 0xFF;
    int mineur = tty_nr & 0xFF;
    if (majeur == 136) snprintf(buf, size, "pts/%d", mineur);
    else               snprintf(buf, size, "tty%d", mineur);
}

void formater_time(long utime, long stime, char *buf, size_t size) {
    long hz    = sysconf(_SC_CLK_TCK);
    long total = (utime + stime) / hz;
    long h = total / 3600;
    long m = (total % 3600) / 60;
    long s = total % 60;
    snprintf(buf, size, "%ld:%02ld:%02ld", h, m, s);
}

void formater_time_court(long utime, long stime, char *buf, size_t size) {
    long hz    = sysconf(_SC_CLK_TCK);
    long total = (utime + stime) / hz;
    long m = total / 60;
    long s = total % 60;
    snprintf(buf, size, "%ld:%02ld", m, s);
}

void afficher_en_tete(OptsAffichage *opts) {
    if (opts->opt_v) {
        printf("%-10s %-8s %-6s %-9s %-6s %-6s %-8s %-6s %-5s %s\n",
               "PID","TTY","STAT","TIME","MAJFL","TRS","DRS","RSS","%MEM","COMMAND");
    } else if (opts->opt_F) {
        printf("%-10s %-10s %-6s %-6s %-6s %-6s %-4s %-6s %-8s %-9s %s\n",
               "UID","PID","PPID","C","SZ","RSS","PSR","STIME","TTY","TIME","CMD");
    } else if (opts->opt_f) {
        printf("%-10s %-6s %-6s %-2s %-6s %-8s %-9s %s\n",
               "UID","PID","PPID","C","STIME","TTY","TIME","CMD");
    } else if (opts->opt_u) {
        printf("%-10s %-6s %-3s %-4s %-4s %-6s %-6s %-9s %-5s %s\n",
               "USER","PID","%CPU","%MEM","VSZ","RSS","TTY","STAT","START","COMMAND");
    } else {
        printf("%-6s %-8s %-9s %s\n", "PID","TTY","TIME","CMD");
    }
}

void afficher_ligne(Processus *p, OptsAffichage *opts, const char *prefixe) {
    char tty[16], time_str[16], time_court[16];
    decoder_tty(p->tty, tty, sizeof(tty));
    formater_time(p->utime, p->stime, time_str, sizeof(time_str));
    formater_time_court(p->utime, p->stime, time_court, sizeof(time_court));

    /* Tronque la commande à 50 caractères */
    char cmd[51];
    strncpy(cmd, p->cmdline, 50);
    cmd[50] = '\0';

    if (opts->opt_v) {
        long ram_kb   = sysconf(_SC_PHYS_PAGES) * (sysconf(_SC_PAGE_SIZE) / 1024);
        double pct_mem = ram_kb > 0 ? (100.0 * p->rss / ram_kb) : 0.0;
        printf("%-10d %-8s %-6c %-9s %-6lu %-6lu %-8lu %-6ld %-5.1f %s%s\n",
               p->pid, tty, p->etat, time_str,
               p->majfl, p->trs, p->drs, p->rss, pct_mem,
               prefixe, cmd);

    } else if (opts->opt_F) {
        struct passwd *pw = getpwuid(p->uid);
        char *user = (pw && pw->pw_name) ? pw->pw_name : "?";
        long sz = p->rss / 4;
        printf("%-10s %-10d %-6d %-6d %-6ld %-6ld %-4d %-6s %-8s %-9s %s%s\n",
               user, p->pid, p->ppid, 0, sz, p->rss, 0,
               "?", tty, time_str, prefixe, cmd);

    } else if (opts->opt_f) {
        struct passwd *pw = getpwuid(p->uid);
        char *user = (pw && pw->pw_name) ? pw->pw_name : "?";
        printf("%-10s %-6d %-6d %-2d %-6s %-8s %-9s %s%s\n",
               user, p->pid, p->ppid, 0,
               "?", tty, time_str, prefixe, cmd);

    } else if (opts->opt_u) {
        struct passwd *pw = getpwuid(p->uid);
        char *user = (pw && pw->pw_name) ? pw->pw_name : "?";
        long ram_kb    = sysconf(_SC_PHYS_PAGES) * (sysconf(_SC_PAGE_SIZE) / 1024);
        double pct_mem = ram_kb > 0 ? (100.0 * p->rss / ram_kb) : 0.0;
        printf("%-10s %-6d %-3.1f %-4.1f %-6lu %-6ld %-6s %-6c %-9s %-5s %s%s\n",
               user, p->pid, p->cpu, pct_mem,
               p->trs + p->drs, p->rss, tty, p->etat,
               "?", "?", prefixe, cmd);

    } else {
        /* Format par défaut : PID TTY TIME CMD */
        printf("%-6d %-8s %-9s %s%s\n",
               p->pid, tty, time_court, prefixe, cmd);
    }
}

void afficher_tree(Processus *procs, int nb, int pid_parent,
                   const char *prefixe_parent, OptsAffichage *opts) {
    for (int i = 0; i < nb; i++) {
        if (procs[i].ppid != pid_parent) continue;
        if (procs[i].pid  == pid_parent) continue;

        char prefixe_cmd[256];
        char prefixe_enfants[256];
        snprintf(prefixe_cmd,     sizeof(prefixe_cmd),     "%s \\_ ", prefixe_parent);
        snprintf(prefixe_enfants, sizeof(prefixe_enfants), "%s |   ", prefixe_parent);

        afficher_ligne(&procs[i], opts, prefixe_cmd);
        afficher_tree(procs, nb, procs[i].pid, prefixe_enfants, opts);
    }
}