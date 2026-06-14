#ifndef PROC_READER_H
#define PROC_READER_H

#include "process.h"
#include <stddef.h>

/* Lit /proc/[pid]/stat → remplit la majorité des champs */
int  lire_stat(int pid, Processus *proc);

/* Lit /proc/[pid]/status → retourne l'UID réel */
int  lire_uid(int pid);

/* Lit /proc/[pid]/status → retourne VmRSS en KB */
long lire_vmrss(int pid);

/* Lit /proc/[pid]/statm → remplit trs et drs en KB */
int  lire_statm(int pid, unsigned long *trs, unsigned long *drs);

/* Lit /proc/[pid]/cmdline → remplit buf avec la ligne de commande */
void lire_cmdline(int pid, char *buf, size_t size);

#endif