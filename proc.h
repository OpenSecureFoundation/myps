#ifndef PROC_H
#define PROC_H

#define MAX_NOM     256
#define MAX_CHEMIN  512
#define MAX_PROCS  1024

typedef struct {
    int   pid;
    char  nom[MAX_NOM];
    char  etat;
    int   ppid;
    int   uid;
    long  rss;
    int   tty;
    long  utime;
    long  stime;
    long  starttime;
    int   pgrp;       /* groupe du processus   */
    int   session;    /* ID de session         */
    long  vsize;      /* mémoire virtuelle     */
    int   priority;   /* priorité              */
    int   nice;       /* valeur nice           */
} ProcessInfo;

#endif
