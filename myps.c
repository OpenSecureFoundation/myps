/**
 * myps.c - Implementation de la commande ps sous Linux
 *
 * Options :
 *   -a          : afficher les processus de tous les utilisateurs (avec terminal)
 *   -u          : format oriente utilisateur (USER PID %CPU %MEM VSZ RSS TTY STAT START TIME COMMAND)
 *   -x          : inclure les processus sans terminal
 *   -e          : equivalent a -a -x (tous les processus)
 *   -A          : idem -e
 *   -f          : format complet (UID PID PPID C STIME TTY TIME CMD)
 *   -F          : format extra-complet (UID PID PPID C SZ RSS PSR STIME TTY TIME CMD)
 *   -l          : format long (F S UID PID PPID C PRI NI SZ RSS TTY TIME CMD)
 *   -v          : format memoire virtuelle (PID TTY STAT TIME MAJFL MINFL %CPU %MEM VSZ RSS CMD)
 *   -j          : format jobs (PID PGID SID TTY TIME CMD)
 *   -c          : afficher le nom court (comm) au lieu de la ligne de commande
 *   -H          : affichage hierarchique (arbre par indentation dans CMD)
 *   --forest    : arbre ASCII dans la colonne CMD (format courant conserve)
 *   -p PID      : filtrer par PID (priorite sur tout autre filtre)
 *   -t TTY      : filtrer par terminal (ex: pts/0)
 *   -n NOM      : filtrer par nom de processus (sous-chaine)
 *   -C NOM      : filtrer par nom exact (comm)
 *   -U USER     : filtrer par utilisateur reel (nom ou UID)
 *   -G GID      : filtrer par groupe reel (RGID)
 *   -Z          : afficher le contexte de securite SELinux (LABEL)
 *   -s          : afficher les colonnes PGID et SID
 *   -L          : afficher les threads (PID LWP TTY TIME CMD)
 *   -w          : largeur illimitee (pas de troncature de CMD)
 *   --sort=CLE  : trier selon pid|ppid|cpu|rss|vsz|nom|uid
 *   --headers   : repeter les en-tetes toutes les N lignes
 *
 * Corrections appliquees :
 *   [FIX1]  --forest : affiche les colonnes du format courant, arbre dans CMD
 *   [FIX2]  --forest/-H : charge tous les procs pour l'arbre, filtre a l'affichage
 *   [FIX3]  CMD : troncature basee sur la largeur reelle du terminal (ioctl)
 *   [FIX4]  -p PID : court-circuite le filtre UID (affiche quel que soit le proprio)
 *   [FIX5]  -G GID : court-circuite le filtre UID (comme -p et -U)
 *   [FIX6]  -F : suppression de NLWP, en-tete UID (pas USER), ordre conforme
 *   [FIX7]  -v : colonnes completes (PID TTY STAT TIME MAJFL MINFL %CPU %MEM VSZ RSS CMD)
 *   [FIX8]  -L : suppression de NLWP et TGID de l'en-tete de base (PID LWP TTY TIME CMD)
 *
 * Compilation : gcc -Wall -O2 -o myps myps.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <time.h>

#define HZ sysconf(_SC_CLK_TCK)

/* ---------- Tailles des buffers ---------- */
#define CMD_MAX   512
#define NOM_MAX   256
#define SCTX_MAX  256
#define TTY_MAX    32
#define TEMPS_MAX  24   /* assez grand pour HH:MM:SS */
#define START_MAX  12

/* ---------- Largeurs de colonnes ----------
 * Largeurs fixes, prudemment larges pour ne jamais deborder.
 * Ajustees sur les largeurs reelles de ps GNU procps.
 */
#define W_PID     7    /* jusqu'a 7 chiffres  */
#define W_PPID    7
#define W_PGID    7
#define W_SID     7
#define W_LWP     7
#define W_UID     6    /* UID numerique ou USER (tronque si besoin) */
#define W_FLAGS   8    /* flags en hexa */
#define W_PRI     4
#define W_NI      4
#define W_C       3    /* %CPU entier */
#define W_USER   16    /* nom utilisateur */
#define W_STAT    5    /* ex: "Ssl+" */
#define W_TTY    10    /* ex: "pts/123" */
#define W_TIME   10    /* ex: "123:45" ou "1:02:03" */
#define W_START   6    /* ex: "Jun15" ou "23:59" */
#define W_VSZ    10    /* KB memoire virtuelle */
#define W_RSS     8    /* KB memoire residente */
#define W_SZ      8    /* taille en pages */
#define W_MAJFL   8    /* major faults */
#define W_MINFL   8    /* minor faults */
#define W_PCPU    6    /* %.1f */
#define W_PMEM    6    /* %.1f */
#define W_PSR     4    /* numero de CPU */
#define W_LABEL  35    /* contexte SELinux */

/* Repetition des en-tetes avec --headers */
#define HEADERS_INTERVAL 20

/* Largeur CMD par defaut quand le terminal n'est pas detectable */
#define CMD_LARGEUR_DEFAUT 60

/* ---------- Structure Processus ---------- */
typedef struct {
    /* Identifiants */
    int   pid;
    int   ppid;
    int   pgid;
    int   sid;
    int   uid;          /* UID effectif (pour getpwuid et filtrage par defaut) */
    int   ruid;         /* UID reel (pour -U) */
    int   gid;
    int   rgid;         /* GID reel (pour -G) */
    int   tgid;         /* thread group ID (pour -L) */
    long  nlwp;         /* nombre de threads */

    /* Nom et commande */
    char  nom[NOM_MAX];       /* nom court (comm) */
    char  cmdline[CMD_MAX];   /* ligne de commande complete */

    /* Etat et ordonnancement */
    char  etat;         /* R, S, D, Z, T ... */
    int   pri;          /* priorite */
    int   ni;           /* valeur nice */
    long  flags;        /* flags du processus */
    int   psr;          /* CPU sur lequel il tourne */

    /* Memoire */
    long  rss;          /* pages residentes (x 4 = KB sur x86) */
    long  vsz;          /* memoire virtuelle en KB */
    long  sz;           /* taille en pages (vsz / 4096) */
    long  majfl;        /* major page faults */
    long  minfl;        /* minor page faults */

    /* Temps */
    double cpu;               /* %CPU cumule */
    double total_cpu_sec;     /* secondes CPU totales */
    long   start_time;        /* jiffies depuis le boot */
    char   start_str[START_MAX]; /* heure ou date de demarrage */

    /* Terminal */
    int  tty_nr;         /* numero brut du tty */
    char tty[TTY_MAX];   /* ex : "pts/0", "tty1", "?" */

    /* SELinux */
    char scontext[SCTX_MAX];

    /* Pour --forest/-H : prefixe d'arbre a injecter dans CMD */
    char arbre_prefix[128];
} Processus;

/* Variable globale de tri (utilisee par qsort) */
char *tri_critere = NULL;

/* ======================================================
   FONCTIONS UTILITAIRES
   ====================================================== */

int est_un_nombre(const char *s) {
    if (!s || !*s) return 0;
    while (*s) if (!isdigit((unsigned char)*s++)) return 0;
    return 1;
}

/* [FIX3] Retourne la largeur utile de la colonne CMD selon la largeur du terminal.
 * Soustrait les colonnes fixes deja affichees (offset) pour que la ligne
 * tienne dans la largeur du terminal.
 * Si le terminal n'est pas detectable, retourne CMD_LARGEUR_DEFAUT.
 */
int largeur_cmd(int offset_colonnes) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        int dispo = (int)ws.ws_col - offset_colonnes;
        return (dispo > 8) ? dispo : 8; /* minimum 8 caracteres */
    }
    return CMD_LARGEUR_DEFAUT;
}

/* Convertit le tty_nr brut en nom lisible ("pts/0", "tty1", "?") */
void resoudre_tty(int tty_nr, char *buf, size_t taille) {
    if (tty_nr == 0) { snprintf(buf, taille, "?"); return; }

    unsigned int maj = (tty_nr >> 8) & 0xFF;
    unsigned int min = tty_nr & 0xFF;

    if (maj >= 136 && maj <= 143) {           /* pts : majors 136-143 */
        snprintf(buf, taille, "pts/%u", (maj - 136) * 256 + min);
        return;
    }
    if (maj == 4 && min >= 64) {              /* ttyS serie */
        snprintf(buf, taille, "ttyS%u", min - 64);
        return;
    }
    if (maj == 4) {                           /* tty virtuel */
        snprintf(buf, taille, "tty%u", min);
        return;
    }
    /* Tentative via /dev/pts */
    char chemin[64];
    struct stat st;
    snprintf(chemin, sizeof(chemin), "/dev/pts/%u", min);
    if (stat(chemin, &st) == 0 && major(st.st_rdev) == maj) {
        snprintf(buf, taille, "pts/%u", min);
        return;
    }
    snprintf(buf, taille, "?");
}

/* Calcule l'heure (HH:MM) ou la date (MmmDD) de demarrage */
void calculer_start(long starttime_jiffies, char *buf, size_t taille) {
    long hz = HZ;
    FILE *up = fopen("/proc/uptime", "r");
    if (!up) { snprintf(buf, taille, "?"); return; }
    double uptime = 0.0;
    if (fscanf(up, "%lf", &uptime) != 1) { fclose(up); snprintf(buf, taille, "?"); return; }
    fclose(up);

    time_t now        = time(NULL);
    time_t boot       = now - (time_t)uptime;
    time_t proc_start = boot + (time_t)(starttime_jiffies / (double)hz);

    struct tm tm_now, tm_proc;
    localtime_r(&now,        &tm_now);
    localtime_r(&proc_start, &tm_proc);

    if (tm_now.tm_yday == tm_proc.tm_yday && tm_now.tm_year == tm_proc.tm_year)
        strftime(buf, taille, "%H:%M", &tm_proc);  /* aujourd'hui : heure */
    else
        strftime(buf, taille, "%b%d",  &tm_proc);  /* sinon : date ex Jun15 */
}

/* Formate le temps CPU en MM:SS ou HH:MM:SS */
void formater_temps(double sec, char *buf, size_t taille) {
    long s  = (long)sec;
    long h  = s / 3600;
    long m  = (s % 3600) / 60;
    long se = s % 60;
    if (h > 0)
        snprintf(buf, taille, "%ld:%02ld:%02ld", h, m, se);
    else
        snprintf(buf, taille, "%ld:%02ld", m, se);
}

/* ======================================================
   LECTURE DE /proc
   ====================================================== */

/* Lit le contexte SELinux depuis /proc/pid/attr/current */
void lire_contexte_Z(int pid, char *buf, size_t taille) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/attr/current", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) { snprintf(buf, taille, "unconfined"); return; }
    if (fgets(buf, (int)taille, f)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    } else {
        snprintf(buf, taille, "unconfined");
    }
    fclose(f);
}

/* Lit UID/GID reel et effectif + nombre de threads depuis /proc/pid/status */
void lire_status(int pid, int *ruid, int *uid_eff, int *rgid, int *gid_eff, long *nlwp) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/status", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return;
    char ligne[256];
    while (fgets(ligne, sizeof(ligne), f)) {
        if (strncmp(ligne, "Uid:", 4) == 0) {
            int ru = -1, eu = -1;
            sscanf(ligne, "Uid: %d %d", &ru, &eu);
            if (ruid)    *ruid    = ru;
            if (uid_eff) *uid_eff = eu;
        } else if (strncmp(ligne, "Gid:", 4) == 0) {
            int rg = -1, eg = -1;
            sscanf(ligne, "Gid: %d %d", &rg, &eg);
            if (rgid)    *rgid    = rg;
            if (gid_eff) *gid_eff = eg;
        } else if (strncmp(ligne, "Threads:", 8) == 0) {
            if (nlwp) sscanf(ligne, "Threads: %ld", nlwp);
        }
    }
    fclose(f);
}

/* Lit /proc/pid/stat et remplit la structure Processus */
int lire_stat(int pid, Processus *proc) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/stat", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;

    long  pid_lu;
    char  nom[NOM_MAX], etat;
    int   ppid, pgrp, session, tty_nr;
    long  flags, minflt, cminflt, majfl, cmajflt;
    long  utime, stime, cutime, cstime;
    long  priority, nice, nlwp_lu;
    long  itrealvalue, starttime, vsz, rss;
    long  dummy;

    /* Lecture des 25 premiers champs de /proc/pid/stat */
    int n = fscanf(f,
        "%ld %255s %c %d %d %d %d"     /* 1-7   pid comm state ppid pgrp session tty_nr  */
        " %ld %ld %ld %ld %ld %ld %ld" /* 8-14  flags minflt cminflt majfl cmajflt utime stime */
        " %ld %ld %ld %ld %ld %ld %ld" /* 15-21 cutime cstime priority nice nlwp itrealvalue starttime */
        " %ld %ld %ld %ld",             /* 22-25 vsize rss rlim ... */
        &pid_lu, nom, &etat, &ppid, &pgrp, &session, &tty_nr,
        &flags, &minflt, &cminflt, &majfl, &cmajflt, &utime, &stime,
        &cutime, &cstime, &priority, &nice, &nlwp_lu, &itrealvalue, &starttime,
        &vsz, &rss, &dummy, &dummy);
    fclose(f);
    if (n < 24) return 0;

    proc->pid        = (int)pid_lu;
    proc->ppid       = ppid;
    proc->pgid       = pgrp;
    proc->sid        = session;
    proc->tty_nr     = tty_nr;
    proc->etat       = etat;
    proc->flags      = flags;
    proc->majfl      = majfl;
    proc->minfl      = minflt;   /* [FIX7] minor faults pour -v */
    proc->pri        = (int)priority;
    proc->ni         = (int)nice;
    proc->nlwp       = nlwp_lu;
    proc->rss        = rss;            /* en pages */
    proc->vsz        = vsz / 1024;    /* en KB */
    proc->sz         = vsz / 4096;    /* en pages (pour -F et -l) */
    proc->start_time = starttime;
    proc->arbre_prefix[0] = '\0';

    /* Nettoyage du nom : enlever les parentheses */
    int len = (int)strlen(nom);
    if (len >= 2 && nom[0] == '(' && nom[len - 1] == ')') {
        nom[len - 1] = '\0';
        strncpy(proc->nom, nom + 1, NOM_MAX - 1);
    } else {
        strncpy(proc->nom, nom, NOM_MAX - 1);
    }
    proc->nom[NOM_MAX - 1] = '\0';

    /* Temps CPU total en secondes */
    long hz = HZ;
    double total_time = (utime + stime) / (double)hz;
    proc->total_cpu_sec = total_time;

    /* Pourcentage CPU cumule depuis le lancement */
    FILE *up = fopen("/proc/uptime", "r");
    if (up) {
        double uptime = 0.0;
        if (fscanf(up, "%lf", &uptime) == 1) {
            double elapsed = uptime - (starttime / (double)hz);
            proc->cpu = (elapsed > 0.0) ? 100.0 * total_time / elapsed : 0.0;
        }
        fclose(up);
    }

    /* Resolution du TTY */
    resoudre_tty(tty_nr, proc->tty, sizeof(proc->tty));

    /* Heure/date de demarrage */
    calculer_start(starttime, proc->start_str, sizeof(proc->start_str));

    return 1;
}

/* Lit la ligne de commande depuis /proc/pid/cmdline */
void lire_cmdline(int pid, char *buf, size_t taille) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/cmdline", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) { snprintf(buf, taille, "[defunct]"); return; }
    size_t n = fread(buf, 1, taille - 1, f);
    fclose(f);
    if (n == 0) { snprintf(buf, taille, "[kernel thread]"); return; }
    /* Les arguments sont separes par '\0' : on les remplace par des espaces */
    for (size_t i = 0; i < n; i++)
        if (buf[i] == '\0') buf[i] = ' ';
    while (n > 0 && buf[n - 1] == ' ') n--;
    buf[n] = '\0';
}

/* Lit le numero du CPU courant (champ 39 de /proc/pid/stat) */
int lire_psr(int pid) {
    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/proc/%d/stat", pid);
    FILE *f = fopen(chemin, "r");
    if (!f) return 0;
    long val = 0;
    int  field = 0, c;
    /* Sauter les 38 premiers champs separes par espaces */
    while (field < 38 && (c = fgetc(f)) != EOF)
        if (c == ' ') field++;
    if (fscanf(f, "%ld", &val) != 1) val = 0;
    fclose(f);
    return (int)val;
}

/* ======================================================
   GESTION DES THREADS (option -L)
   ====================================================== */

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
        if (nb >= cap) {
            cap *= 2;
            int *tmp = realloc(*tids, cap * sizeof(int));
            if (!tmp) { free(*tids); closedir(d); return 0; }
            *tids = tmp;
        }
        (*tids)[nb++] = atoi(ent->d_name);
    }
    closedir(d);
    return nb;
}

/* ======================================================
   FILTRAGE
   ====================================================== */

/*
 * [FIX4][FIX5] Ordre de priorite des filtres :
 *  1. -p PID    : affiche le processus quel que soit l'utilisateur ou le GID
 *  2. -U USER   : filtre par UID reel, independamment de l'utilisateur courant
 *  3. -G GID    : filtre par GID reel, independamment de l'utilisateur courant
 *  4. -a/-x     : filtres habituels (utilisateur courant, terminal)
 *  5. -t/-n/-C  : filtres supplementaires
 */
int doit_afficher(Processus *p,
                  int opt_a, int opt_x, int opt_A,
                  int pid_filtre, int uid_courant,
                  const char *tty_filtre, const char *nom_filtre,
                  const char *nom_exact, int ruid_filtre, int rgid_filtre) {

    /* -A ou -e : tout afficher sans restriction */
    if (opt_A) return 1;

    /* [FIX4] -p PID : affiche ce PID sans verifier l'utilisateur ni le GID */
    if (pid_filtre > 0) return (p->pid == pid_filtre) ? 1 : 0;

    /* [FIX5] -U : filtre par UID reel, independamment de uid_courant */
    if (ruid_filtre != -1 && p->ruid != ruid_filtre) return 0;

    /* [FIX5] -G : filtre par GID reel, independamment de uid_courant */
    if (rgid_filtre != -1 && p->rgid != rgid_filtre) return 0;

    /* Sans -a : uniquement l'utilisateur courant */
    if (!opt_a && p->uid != uid_courant) return 0;

    /* Sans -x : exclure les processus sans terminal */
    if (!opt_x && p->tty_nr == 0) return 0;

    /* -t : filtrer par terminal */
    if (tty_filtre  && strcmp(p->tty, tty_filtre)   != 0)  return 0;

    /* -n : filtrer par sous-chaine du nom */
    if (nom_filtre  && strstr(p->nom, nom_filtre)    == NULL) return 0;

    /* -C : filtrer par nom exact */
    if (nom_exact   && strcmp(p->nom, nom_exact)     != 0)  return 0;

    return 1;
}

/* ======================================================
   TRI
   ====================================================== */

int comparer_processus(const void *a, const void *b) {
    const Processus *pa = (const Processus *)a;
    const Processus *pb = (const Processus *)b;
    if (tri_critere) {
        if (strcmp(tri_critere, "pid")  == 0) return pa->pid  - pb->pid;
        if (strcmp(tri_critere, "ppid") == 0) return pa->ppid - pb->ppid;
        if (strcmp(tri_critere, "rss")  == 0) return (int)(pa->rss - pb->rss);
        if (strcmp(tri_critere, "vsz")  == 0) return (int)(pa->vsz - pb->vsz);
        if (strcmp(tri_critere, "nom")  == 0) return strcmp(pa->nom, pb->nom);
        if (strcmp(tri_critere, "uid")  == 0) return pa->uid  - pb->uid;
        if (strcmp(tri_critere, "cpu")  == 0)
            return (pa->cpu > pb->cpu) ? 1 : (pa->cpu < pb->cpu) ? -1 : 0;
    }
    return pa->pid - pb->pid; /* tri par defaut : PID croissant */
}

/* ======================================================
   CONSTRUCTION DE L'ARBRE (-H et --forest)
   ======================================================
 *
 * [FIX1][FIX2] Principe :
 *   - On charge TOUS les processus dans le tableau procs (filtre desactive).
 *   - On marque les processus "a afficher" selon les filtres reels.
 *   - On construit le prefixe d'arbre (arbre_prefix) de chaque processus.
 *   - On affiche ensuite chaque processus marque avec le format courant,
 *     en injectant arbre_prefix devant la colonne CMD.
 *
 * Le flag doit_afficher_arbre indique si ce proc sera affiche ou non.
 */

/* Remplit arbre_prefix pour chaque processus selon son niveau dans l'arbre.
 * Style -H  : "    \\_ " (indentation simple)
 * Style --forest : "  |   \\_ " (lignes verticales ASCII)
 */
void construire_arbre(Processus *procs, int nb, int parent, int niveau, int forest) {
    for (int i = 0; i < nb; i++) {
        if (procs[i].ppid != parent) continue;

        char buf[128] = {0};
        if (niveau > 0) {   /* racine = pas de préfixe */
            if (forest) {
                for (int j = 1; j < niveau; j++)
                    strncat(buf, "  | ", sizeof(buf) - strlen(buf) - 1);
                strncat(buf, "  \\_ ", sizeof(buf) - strlen(buf) - 1);
            } else {
                for (int j = 1; j < niveau; j++)
                    strncat(buf, "    ", sizeof(buf) - strlen(buf) - 1);
                strncat(buf, " \\_ ", sizeof(buf) - strlen(buf) - 1);
            }
        }
        strncpy(procs[i].arbre_prefix, buf, sizeof(procs[i].arbre_prefix) - 1);
        procs[i].arbre_prefix[sizeof(procs[i].arbre_prefix) - 1] = '\0';

        construire_arbre(procs, nb, procs[i].pid, niveau + 1, forest);
    }
}
/* ======================================================
   EN-TETES (conformes aux en-tetes reels de ps GNU)
   ======================================================
 *
 * Colonnes par format (verification sur ps procps-ng) :
 *
 *  defaut   : PID  TTY          TIME CMD
 *  -f       : UID        PID  PPID  C STIME TTY          TIME CMD
 *  -F [FIX6]: UID        PID  PPID  C    SZ  RSS PSR STIME TTY       TIME CMD
 *  -u       : USER       PID %CPU %MEM    VSZ   RSS TTY      STAT START   TIME COMMAND
 *  -l       : F S   UID   PID  PPID  C PRI  NI    SZ   RSS TTY      TIME CMD
 *  -j       : PID  PGID   SID TTY          TIME CMD
 *  -L [FIX8]: PID   LWP TTY          TIME CMD
 *  -v [FIX7]: PID TTY      STAT   TIME  MAJFL  MINFL %CPU %MEM    VSZ   RSS COMMAND
 *  -Z       : prefixe LABEL devant le format choisi
 *  -s       : prefixe PGID SID devant le format choisi
 */
void afficher_entete(int opt_u, int opt_f, int opt_F, int opt_l,
                     int opt_v, int opt_j, int opt_L,
                     int opt_Z, int opt_s) {
    /* [FIX8] -L : PID LWP TTY TIME CMD (sans NLWP ni TGID en mode de base) */
    if (opt_L) {
        printf("%-*s %-*s %-*s %-*s %s\n",
               W_PID,  "PID",
               W_LWP,  "LWP",
               W_TTY,  "TTY",
               W_TIME, "TIME",
               "CMD");
        return;
    }
    /* -j : jobs */
    if (opt_j) {
        if (opt_Z) printf("%-*s ", W_LABEL, "LABEL");
        if (opt_s) printf("%-*s %-*s ", W_PGID, "PGID", W_SID, "SID");
        printf("%-*s %-*s %-*s %-*s %-*s %s\n",
               W_PID,  "PID",
               W_PGID, "PGID",
               W_SID,  "SID",
               W_TTY,  "TTY",
               W_TIME, "TIME",
               "CMD");
        return;
    }
    /* -l : format long */
    if (opt_l) {
        if (opt_Z) printf("%-*s ", W_LABEL, "LABEL");
        if (opt_s) printf("%-*s %-*s ", W_PGID, "PGID", W_SID, "SID");
        printf("%-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %s\n",
               W_FLAGS, "F",
               1,       "S",
               W_UID,   "UID",
               W_PID,   "PID",
               W_PPID,  "PPID",
               W_C,     "C",
               W_PRI,   "PRI",
               W_NI,    "NI",
               W_SZ,    "SZ",
               W_RSS,   "RSS",
               W_TTY,   "TTY",
               W_TIME,  "TIME",
               "CMD");
        return;
    }
    /* [FIX6] -F : UID PID PPID C SZ RSS PSR STIME TTY TIME CMD (sans NLWP) */
    if (opt_F) {
        if (opt_Z) printf("%-*s ", W_LABEL, "LABEL");
        if (opt_s) printf("%-*s %-*s ", W_PGID, "PGID", W_SID, "SID");
        printf("%-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %s\n",
               W_USER,  "UID",
               W_PID,   "PID",
               W_PPID,  "PPID",
               W_C,     "C",
               W_SZ,    "SZ",
               W_RSS,   "RSS",
               W_PSR,   "PSR",
               W_START, "STIME",
               W_TTY,   "TTY",
               W_TIME,  "TIME",
               "CMD");
        return;
    }
    /* -f : full */
    if (opt_f) {
        if (opt_Z) printf("%-*s ", W_LABEL, "LABEL");
        if (opt_s) printf("%-*s %-*s ", W_PGID, "PGID", W_SID, "SID");
        printf("%-*s %-*s %-*s %-*s %-*s %-*s %-*s %s\n",
               W_USER,  "UID",
               W_PID,   "PID",
               W_PPID,  "PPID",
               W_C,     "C",
               W_START, "STIME",
               W_TTY,   "TTY",
               W_TIME,  "TIME",
               "CMD");
        return;
    }
    /* -u : format utilisateur */
    if (opt_u) {
        if (opt_Z) printf("%-*s ", W_LABEL, "LABEL");
        if (opt_s) printf("%-*s %-*s ", W_PGID, "PGID", W_SID, "SID");
        printf("%-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %s\n",
               W_USER,  "USER",
               W_PID,   "PID",
               W_PCPU,  "%CPU",
               W_PMEM,  "%MEM",
               W_VSZ,   "VSZ",
               W_RSS,   "RSS",
               W_TTY,   "TTY",
               W_STAT,  "STAT",
               W_START, "START",
               W_TIME,  "TIME",
               "COMMAND");
        return;
    }
    /* [FIX7] -v : PID TTY STAT TIME MAJFL MINFL %CPU %MEM VSZ RSS COMMAND */
    if (opt_v) {
        if (opt_Z) printf("%-*s ", W_LABEL, "LABEL");
        if (opt_s) printf("%-*s %-*s ", W_PGID, "PGID", W_SID, "SID");
        printf("%-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %s\n",
               W_PID,   "PID",
               W_TTY,   "TTY",
               W_STAT,  "STAT",
               W_TIME,  "TIME",
               W_MAJFL, "MAJFL",
               W_MINFL, "MINFL",
               W_PCPU,  "%CPU",
               W_PMEM,  "%MEM",
               W_VSZ,   "VSZ",
               W_RSS,   "RSS",
               "COMMAND");
        return;
    }
    /* Format par defaut : PID TTY TIME CMD */
    if (opt_Z) printf("%-*s ", W_LABEL, "LABEL");
    if (opt_s) printf("%-*s %-*s ", W_PGID, "PGID", W_SID, "SID");
    printf("%-*s %-*s %-*s %s\n",
           W_PID,  "PID",
           W_TTY,  "TTY",
           W_TIME, "TIME",
           "CMD");
}

/* ======================================================
   MEMOIRE TOTALE (pour le calcul de %MEM dans -u et -v)
   ====================================================== */

long mem_total_kb = 0;

void lire_mem_total(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;
    char ligne[128];
    while (fgets(ligne, sizeof(ligne), f)) {
        if (strncmp(ligne, "MemTotal:", 9) == 0) {
            sscanf(ligne, "MemTotal: %ld kB", &mem_total_kb);
            break;
        }
    }
    fclose(f);
}

/* ======================================================
   AFFICHAGE D'UNE LIGNE
   ======================================================
 *
 * [FIX3] La colonne CMD est tronquee a largeur_cmd(offset) caracteres,
 *        ou offset est la somme des largeurs de toutes les colonnes fixes
 *        deja affichees sur cette ligne (espaces compris).
 *        Avec -w, aucune troncature.
 *
 * [FIX1] Le prefixe d'arbre (arbre_prefix) est prepend a CMD quand
 *        --forest ou -H est actif. Il est stocke dans p->arbre_prefix.
 */
void afficher_ligne(Processus *p,
                    int opt_u, int opt_f, int opt_F, int opt_l,
                    int opt_v, int opt_j, int opt_L, int opt_c,
                    int opt_Z, int opt_s, int opt_w) {

    /* Calculs derives */
    long   mem_kb  = p->rss * 4;   /* pages -> KB (1 page = 4 Ko sur x86_64) */
    double pmem    = (mem_total_kb > 0) ? (100.0 * mem_kb / mem_total_kb) : 0.0;
    int    cpu_int = (int)p->cpu;   /* %CPU entier pour colonne C */

    struct passwd *pw   = getpwuid(p->uid);
    char          *user = (pw && pw->pw_name) ? pw->pw_name : "?";

    /* Source de la commande : nom court (-c) ou ligne complete */
    const char *cmd_src = opt_c ? p->nom : p->cmdline;

    /* [FIX1] Construction de la commande avec prefixe d'arbre si necessaire */
    char cmd_avec_prefixe[CMD_MAX];
    if (p->arbre_prefix[0] != '\0') {
        snprintf(cmd_avec_prefixe, sizeof(cmd_avec_prefixe),
                 "%s%s", p->arbre_prefix, cmd_src);
    } else {
        strncpy(cmd_avec_prefixe, cmd_src, CMD_MAX - 1);
        cmd_avec_prefixe[CMD_MAX - 1] = '\0';
    }

    /* Temps CPU formate */
    char temps[TEMPS_MAX];
    formater_temps(p->total_cpu_sec, temps, sizeof(temps));

    /* STAT etendu (pour -u et -v) :
     *   l = multi-thread    s = session leader    + = foreground
     * Ordre : etat, modificateurs */
    char stat_str[6] = {p->etat, ' ', ' ', ' ', ' ', '\0'};
    int  pos = 1;
    if (p->nlwp > 1)       stat_str[pos++] = 'l';  /* multi-thread */
    if (p->sid == p->pid)  stat_str[pos++] = 's';  /* session leader */
    if (p->pgid == p->sid) stat_str[pos++] = '+';  /* foreground (approx) */
    stat_str[pos] = '\0';

    /* [FIX3] Calcul de l'offset fixe et troncature CMD */
    int offset;
    char cmd_buf[CMD_MAX];

    /* Macro locale pour tronquer cmd selon l'offset des colonnes fixes */
#define TRONQUER_CMD(off) do { \
    offset = (off); \
    if (!opt_w) { \
        int larg = largeur_cmd(offset); \
        strncpy(cmd_buf, cmd_avec_prefixe, larg); \
        cmd_buf[larg] = '\0'; \
    } else { \
        strncpy(cmd_buf, cmd_avec_prefixe, CMD_MAX - 1); \
        cmd_buf[CMD_MAX - 1] = '\0'; \
    } \
} while(0)

    /* [FIX8] -L : PID LWP TTY TIME CMD */
    if (opt_L) {
        /* offset = W_PID+1 + W_LWP+1 + W_TTY+1 + W_TIME+1 */
        TRONQUER_CMD((W_PID+1) + (W_LWP+1) + (W_TTY+1) + (W_TIME+1));
        printf("%-*d %-*d %-*s %-*s %s\n",
               W_PID,  p->tgid,   /* PID = PID du groupe */
               W_LWP,  p->pid,    /* LWP = TID du thread */
               W_TTY,  p->tty,
               W_TIME, temps,
               cmd_buf);
        return;
    }

    /* -j : jobs */
    if (opt_j) {
        int pref = (opt_Z ? W_LABEL+1 : 0) + (opt_s ? (W_PGID+1)+(W_SID+1) : 0);
        TRONQUER_CMD(pref + (W_PID+1) + (W_PGID+1) + (W_SID+1) + (W_TTY+1) + (W_TIME+1));
        if (opt_Z) printf("%-*s ", W_LABEL, p->scontext);
        if (opt_s) printf("%-*d %-*d ", W_PGID, p->pgid, W_SID, p->sid);
        printf("%-*d %-*d %-*d %-*s %-*s %s\n",
               W_PID,  p->pid,
               W_PGID, p->pgid,
               W_SID,  p->sid,
               W_TTY,  p->tty,
               W_TIME, temps,
               cmd_buf);
        return;
    }

    /* -l : format long */
    if (opt_l) {
        int pref = (opt_Z ? W_LABEL+1 : 0) + (opt_s ? (W_PGID+1)+(W_SID+1) : 0);
        TRONQUER_CMD(pref + (W_FLAGS+1) + 2 + (W_UID+1) + (W_PID+1) + (W_PPID+1)
                     + (W_C+1) + (W_PRI+1) + (W_NI+1) + (W_SZ+1) + (W_RSS+1)
                     + (W_TTY+1) + (W_TIME+1));
        if (opt_Z) printf("%-*s ", W_LABEL, p->scontext);
        if (opt_s) printf("%-*d %-*d ", W_PGID, p->pgid, W_SID, p->sid);
        printf("%-*lx %-*c %-*d %-*d %-*d %-*d %-*d %-*d %-*ld %-*ld %-*s %-*s %s\n",
               W_FLAGS, p->flags,
               1,       p->etat,
               W_UID,   p->uid,
               W_PID,   p->pid,
               W_PPID,  p->ppid,
               W_C,     cpu_int,
               W_PRI,   p->pri,
               W_NI,    p->ni,
               W_SZ,    p->sz,
               W_RSS,   mem_kb,
               W_TTY,   p->tty,
               W_TIME,  temps,
               cmd_buf);
        return;
    }

    /* [FIX6] -F : UID PID PPID C SZ RSS PSR STIME TTY TIME CMD */
    if (opt_F) {
        int pref = (opt_Z ? W_LABEL+1 : 0) + (opt_s ? (W_PGID+1)+(W_SID+1) : 0);
        TRONQUER_CMD(pref + (W_USER+1) + (W_PID+1) + (W_PPID+1) + (W_C+1)
                     + (W_SZ+1) + (W_RSS+1) + (W_PSR+1)
                     + (W_START+1) + (W_TTY+1) + (W_TIME+1));
        if (opt_Z) printf("%-*s ", W_LABEL, p->scontext);
        if (opt_s) printf("%-*d %-*d ", W_PGID, p->pgid, W_SID, p->sid);
        printf("%-*s %-*d %-*d %-*d %-*ld %-*ld %-*d %-*s %-*s %-*s %s\n",
               W_USER,  user,
               W_PID,   p->pid,
               W_PPID,  p->ppid,
               W_C,     cpu_int,
               W_SZ,    p->sz,
               W_RSS,   mem_kb,
               W_PSR,   p->psr,
               W_START, p->start_str,
               W_TTY,   p->tty,
               W_TIME,  temps,
               cmd_buf);
        return;
    }

    /* -f : full */
    if (opt_f) {
        int pref = (opt_Z ? W_LABEL+1 : 0) + (opt_s ? (W_PGID+1)+(W_SID+1) : 0);
        TRONQUER_CMD(pref + (W_USER+1) + (W_PID+1) + (W_PPID+1) + (W_C+1)
                     + (W_START+1) + (W_TTY+1) + (W_TIME+1));
        if (opt_Z) printf("%-*s ", W_LABEL, p->scontext);
        if (opt_s) printf("%-*d %-*d ", W_PGID, p->pgid, W_SID, p->sid);
        printf("%-*s %-*d %-*d %-*d %-*s %-*s %-*s %s\n",
               W_USER,  user,
               W_PID,   p->pid,
               W_PPID,  p->ppid,
               W_C,     cpu_int,
               W_START, p->start_str,
               W_TTY,   p->tty,
               W_TIME,  temps,
               cmd_buf);
        return;
    }

    /* -u : format utilisateur */
    if (opt_u) {
        int pref = (opt_Z ? W_LABEL+1 : 0) + (opt_s ? (W_PGID+1)+(W_SID+1) : 0);
        TRONQUER_CMD(pref + (W_USER+1) + (W_PID+1) + (W_PCPU+1) + (W_PMEM+1)
                     + (W_VSZ+1) + (W_RSS+1) + (W_TTY+1) + (W_STAT+1)
                     + (W_START+1) + (W_TIME+1));
        if (opt_Z) printf("%-*s ", W_LABEL, p->scontext);
        if (opt_s) printf("%-*d %-*d ", W_PGID, p->pgid, W_SID, p->sid);
        printf("%-*s %-*d %-*.1f %-*.1f %-*ld %-*ld %-*s %-*s %-*s %-*s %s\n",
               W_USER,  user,
               W_PID,   p->pid,
               W_PCPU,  p->cpu,
               W_PMEM,  pmem,
               W_VSZ,   p->vsz,
               W_RSS,   mem_kb,
               W_TTY,   p->tty,
               W_STAT,  stat_str,
               W_START, p->start_str,
               W_TIME,  temps,
               cmd_buf);
        return;
    }

    /* [FIX7] -v : PID TTY STAT TIME MAJFL MINFL %CPU %MEM VSZ RSS COMMAND */
    if (opt_v) {
        int pref = (opt_Z ? W_LABEL+1 : 0) + (opt_s ? (W_PGID+1)+(W_SID+1) : 0);
        TRONQUER_CMD(pref + (W_PID+1) + (W_TTY+1) + (W_STAT+1) + (W_TIME+1)
                     + (W_MAJFL+1) + (W_MINFL+1) + (W_PCPU+1) + (W_PMEM+1)
                     + (W_VSZ+1) + (W_RSS+1));
        if (opt_Z) printf("%-*s ", W_LABEL, p->scontext);
        if (opt_s) printf("%-*d %-*d ", W_PGID, p->pgid, W_SID, p->sid);
        printf("%-*d %-*s %-*s %-*s %-*ld %-*ld %-*.1f %-*.1f %-*ld %-*ld %s\n",
               W_PID,   p->pid,
               W_TTY,   p->tty,
               W_STAT,  stat_str,
               W_TIME,  temps,
               W_MAJFL, p->majfl,
               W_MINFL, p->minfl,
               W_PCPU,  p->cpu,
               W_PMEM,  pmem,
               W_VSZ,   p->vsz,
               W_RSS,   mem_kb,
               cmd_buf);
        return;
    }

    /* Format par defaut : PID TTY TIME CMD */
    {
        int pref = (opt_Z ? W_LABEL+1 : 0) + (opt_s ? (W_PGID+1)+(W_SID+1) : 0);
        TRONQUER_CMD(pref + (W_PID+1) + (W_TTY+1) + (W_TIME+1));
        if (opt_Z) printf("%-*s ", W_LABEL, p->scontext);
        if (opt_s) printf("%-*d %-*d ", W_PGID, p->pgid, W_SID, p->sid);
        printf("%-*d %-*s %-*s %s\n",
               W_PID,  p->pid,
               W_TTY,  p->tty,
               W_TIME, temps,
               cmd_buf);
    }

#undef TRONQUER_CMD
}

/* ======================================================
   ANALYSE DES ARGUMENTS
   ====================================================== */

int parse_args(int argc, char **argv,
               int *opt_a, int *opt_u, int *opt_x, int *opt_e, int *opt_A,
               int *opt_f, int *opt_F, int *opt_l, int *opt_v,
               int *opt_c, int *opt_H, int *opt_j, int *opt_L,
               int *opt_Z, int *opt_s, int *opt_w,
               int *opt_forest, int *opt_headers,
               int *pid_filtre,
               int *ruid_filtre, int *rgid_filtre,
               char **tty_filtre, char **nom_filtre, char **nom_exact) {

    *opt_a = *opt_u = *opt_x = *opt_e = *opt_A = 0;
    *opt_f = *opt_F = *opt_l = *opt_v = 0;
    *opt_c = *opt_H = *opt_j = *opt_L = 0;
    *opt_Z = *opt_s = *opt_w = 0;
    *opt_forest = *opt_headers = 0;
    *pid_filtre  = 0;
    *ruid_filtre = -1;
    *rgid_filtre = -1;
    *tty_filtre  = NULL;
    *nom_filtre  = NULL;
    *nom_exact   = NULL;

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];

        /* ── Options longues (testees EN PREMIER avant le test arg[0]=='-') ── */
        if (strncmp(arg, "--sort=", 7) == 0) {
            tri_critere = arg + 7;
            continue;
        }
        if (strcmp(arg, "--forest")  == 0) { *opt_forest  = 1; continue; }
        if (strcmp(arg, "--headers") == 0) { *opt_headers = 1; continue; }

        if (arg[0] != '-') {
            fprintf(stderr, "myps: argument invalide : %s\n", arg);
            return 0;
        }

        /* ── Options avec argument separe ── */
        if (strcmp(arg, "-p") == 0) {
            if (++i >= argc) { fprintf(stderr, "myps: -p : PID manquant\n"); return 0; }
            *pid_filtre = atoi(argv[i]);
            if (*pid_filtre <= 0) { fprintf(stderr, "myps: PID invalide\n"); return 0; }
            continue;
        }
        if (strcmp(arg, "-t") == 0) {
            if (++i >= argc) { fprintf(stderr, "myps: -t : TTY manquant\n"); return 0; }
            *tty_filtre = argv[i];
            continue;
        }
        if (strcmp(arg, "-n") == 0) {
            if (++i >= argc) { fprintf(stderr, "myps: -n : nom manquant\n"); return 0; }
            *nom_filtre = argv[i];
            continue;
        }
        if (strcmp(arg, "-C") == 0) {
            if (++i >= argc) { fprintf(stderr, "myps: -C : nom manquant\n"); return 0; }
            *nom_exact = argv[i];
            continue;
        }
        if (strcmp(arg, "-U") == 0) {
            if (++i >= argc) { fprintf(stderr, "myps: -U : utilisateur manquant\n"); return 0; }
            struct passwd *pw = getpwnam(argv[i]);
            *ruid_filtre = pw ? (int)pw->pw_uid : atoi(argv[i]);
            continue;
        }
        if (strcmp(arg, "-G") == 0) {
            if (++i >= argc) { fprintf(stderr, "myps: -G : GID manquant\n"); return 0; }
            *rgid_filtre = atoi(argv[i]);
            continue;
        }

        /* ── Options courtes groupees (ex: -aux, -ef, -Zs) ── */
        for (int j = 1; arg[j]; j++) {
            switch (arg[j]) {
                case 'a': *opt_a = 1; break;
                case 'u': *opt_u = 1; break;
                case 'x': *opt_x = 1; break;
                case 'e': *opt_e = 1; *opt_a = 1; *opt_x = 1; break;
                case 'A': *opt_A = 1; break;
                case 'f': *opt_f = 1; break;
                case 'F': *opt_F = 1; break;
                case 'l': *opt_l = 1; break;
                case 'v': *opt_v = 1; break;
                case 'c': *opt_c = 1; break;
                case 'H': *opt_H = 1; break;
                case 'j': *opt_j = 1; break;
                case 'L': *opt_L = 1; break;
                case 'Z': *opt_Z = 1; break;
                case 's': *opt_s = 1; break;
                case 'w': *opt_w = 1; break;
                default:
                    fprintf(stderr, "myps: option inconnue : -%c\n", arg[j]);
                    return 0;
            }
        }
    }
    return 1;
}

/* ======================================================
   MAIN
   ====================================================== */

int main(int argc, char **argv) {
    int opt_a, opt_u, opt_x, opt_e, opt_A;
    int opt_f, opt_F, opt_l, opt_v;
    int opt_c, opt_H, opt_j, opt_L;
    int opt_Z, opt_s, opt_w;
    int opt_forest, opt_headers;
    int pid_filtre, ruid_filtre, rgid_filtre;
    char *tty_filtre, *nom_filtre, *nom_exact;

    if (!parse_args(argc, argv,
                    &opt_a, &opt_u, &opt_x, &opt_e, &opt_A,
                    &opt_f, &opt_F, &opt_l, &opt_v,
                    &opt_c, &opt_H, &opt_j, &opt_L,
                    &opt_Z, &opt_s, &opt_w,
                    &opt_forest, &opt_headers,
                    &pid_filtre, &ruid_filtre, &rgid_filtre,
                    &tty_filtre, &nom_filtre, &nom_exact)) {
        fprintf(stderr,
            "Usage: myps [-aAuxeufFlvjLZsHcw] "
            "[-p PID] [-t TTY] [-n NOM] [-C NOM] [-U USER] [-G GID]\n"
            "            [--sort=CLE] [--forest] [--headers]\n"
            "Cles de tri : pid  ppid  cpu  rss  vsz  nom  uid\n");
        return 1;
    }

    lire_mem_total();

    int uid_courant = (int)getuid();

    /* [FIX2] Pour --forest et -H on a besoin de TOUS les processus pour
     * construire l'arbre complet, meme ceux qui ne seront pas affiches.
     * On force eff_A=1 au chargement, puis on filtre a l'affichage. */
    int besoin_arbre = (opt_forest || opt_H);
    int eff_a = opt_a || opt_e || besoin_arbre;
    int eff_x = opt_x || opt_e || besoin_arbre;
    int eff_A = opt_A || opt_e || besoin_arbre;

    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) { perror("myps: /proc"); return 1; }

    Processus *procs    = NULL;
    int        nb       = 0;
    int        capacite = 0;
    struct dirent *ent;

    /* ── Mode -L : enumeration des threads ── */
    if (opt_L) {
        while ((ent = readdir(proc_dir)) != NULL) {
            if (!est_un_nombre(ent->d_name)) continue;
            int pid = atoi(ent->d_name);
            int *tids = NULL;
            int  nb_t = lister_threads(pid, &tids);
            if (nb_t == 0) continue;

            for (int i = 0; i < nb_t; i++) {
                int tid = tids[i];
                Processus p;
                memset(&p, 0, sizeof(p));
                if (!lire_stat(tid, &p)) continue;
                lire_status(tid, &p.ruid, &p.uid, &p.rgid, &p.gid, NULL);
                p.tgid = pid;
                p.nlwp = nb_t;
                p.psr  = lire_psr(tid);
                lire_cmdline(tid, p.cmdline, sizeof(p.cmdline));
                if (opt_Z) lire_contexte_Z(tid, p.scontext, sizeof(p.scontext));

                if (!doit_afficher(&p, eff_a, eff_x, eff_A,
                                   pid_filtre, uid_courant,
                                   tty_filtre, nom_filtre, nom_exact,
                                   ruid_filtre, rgid_filtre))
                    continue;

                if (nb >= capacite) {
                    capacite = capacite ? capacite * 2 : 64;
                    Processus *tmp = realloc(procs, capacite * sizeof(Processus));
                    if (!tmp) { free(procs); free(tids); closedir(proc_dir); return 1; }
                    procs = tmp;
                }
                procs[nb++] = p;
            }
            free(tids);
        }
    }
    /* ── Mode normal : processus ── */
    else {
        while ((ent = readdir(proc_dir)) != NULL) {
            if (!est_un_nombre(ent->d_name)) continue;
            int pid = atoi(ent->d_name);

            Processus p;
            memset(&p, 0, sizeof(p));
            if (!lire_stat(pid, &p)) continue;
            lire_status(pid, &p.ruid, &p.uid, &p.rgid, &p.gid, NULL);
            p.tgid = pid;
            p.psr  = lire_psr(pid);
            lire_cmdline(pid, p.cmdline, sizeof(p.cmdline));
            if (opt_Z) lire_contexte_Z(pid, p.scontext, sizeof(p.scontext));

            /* [FIX2] Pour l'arbre : charger tout, marquer avec filtre reel plus bas */
            if (!doit_afficher(&p, eff_a, eff_x, eff_A,
                               pid_filtre, uid_courant,
                               tty_filtre, nom_filtre, nom_exact,
                               ruid_filtre, rgid_filtre))
                continue;

            if (nb >= capacite) {
                capacite = capacite ? capacite * 2 : 64;
                Processus *tmp = realloc(procs, capacite * sizeof(Processus));
                if (!tmp) { free(procs); closedir(proc_dir); return 1; }
                procs = tmp;
            }
            procs[nb++] = p;
        }
    }
    closedir(proc_dir);

    /* Tri par PID pour que l'arbre soit coherent */
    qsort(procs, nb, sizeof(Processus), comparer_processus);

    /* ── Affichage ── */
    /* ── Affichage ── */
    if (opt_forest || opt_H) {
        /* Trouver les racines réelles : processus dont le ppid
         * n'est pas dans la liste chargée */
        for (int i = 0; i < nb; i++) {
            int ppid_present = 0;
            for (int j = 0; j < nb; j++) {
                if (procs[j].pid == procs[i].ppid) {
                    ppid_present = 1;
                    break;
                }
            }
            if (!ppid_present)
                construire_arbre(procs, nb, procs[i].pid, 0, opt_forest);
        }

        /* Afficher avec le format courant */
        afficher_entete(opt_u, opt_f, opt_F, opt_l,
                        opt_v, opt_j, opt_L, opt_Z, opt_s);

        /* Re-filtrer avec les options réelles (sans besoin_arbre forcé) */
        int eff_a2 = opt_a || opt_e || opt_A;
        int eff_x2 = opt_x || opt_e || opt_A;
        int eff_A2 = opt_A || opt_e;

        for (int i = 0; i < nb; i++) {
            if (!doit_afficher(&procs[i], eff_a2, eff_x2, eff_A2,
                               pid_filtre, uid_courant,
                               tty_filtre, nom_filtre, nom_exact,
                               ruid_filtre, rgid_filtre))
                continue;
            if (opt_headers && i > 0 && i % HEADERS_INTERVAL == 0)
                afficher_entete(opt_u, opt_f, opt_F, opt_l,
                                opt_v, opt_j, opt_L, opt_Z, opt_s);
            afficher_ligne(&procs[i],
                           opt_u, opt_f, opt_F, opt_l,
                           opt_v, opt_j, opt_L, opt_c,
                           opt_Z, opt_s, opt_w);
        }
    } else {
        afficher_entete(opt_u, opt_f, opt_F, opt_l,
                        opt_v, opt_j, opt_L, opt_Z, opt_s);
        for (int i = 0; i < nb; i++) {
            if (opt_headers && i > 0 && i % HEADERS_INTERVAL == 0)
                afficher_entete(opt_u, opt_f, opt_F, opt_l,
                                opt_v, opt_j, opt_L, opt_Z, opt_s);
            afficher_ligne(&procs[i],
                           opt_u, opt_f, opt_F, opt_l,
                           opt_v, opt_j, opt_L, opt_c,
                           opt_Z, opt_s, opt_w);
        }
    }

    free(procs);
    return 0;
}
