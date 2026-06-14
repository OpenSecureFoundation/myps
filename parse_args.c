#include "parse_args.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>

void afficher_aide(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\nFiltres:\n"
        "  -a          Tous les utilisateurs\n"
        "  -x          Inclure les démons (sans TTY)\n"
        "  -p PID      Filtrer par PID\n"
        "  -U user     Filtrer par utilisateur\n"
        "  -T tty      Filtrer par terminal (ex: pts/0)\n"
        "  -d          Exclure les chefs de session\n"
        "\nFormats:\n"
        "  -u          Format orienté utilisateur\n"
        "  -f          Format plein\n"
        "  -F          Format extra-plein\n"
        "  -v          Format mémoire virtuelle\n"
        "\nVisualisation:\n"
        "  --forest    Arbre ASCII parent/enfant\n"
        "\nTri:\n"
        "  -r          Tri par %%CPU décroissant\n"
        "  -m          Tri par mémoire décroissante\n"
        "  -k clé      Tri par colonne (pid/cpu/mem/name)\n",
        prog);
}

/* Traite un seul caractère d'option */
static int traiter_option(char c, int i, int argc, char **argv, Options *opts) {
    switch (c) {
        case 'a': opts->filtres.opt_a = 1; break;
        case 'x': opts->filtres.opt_x = 1; break;
        case 'u': opts->affichage.opt_u = 1; break;
        case 'f': opts->affichage.opt_f = 1; break;
        case 'F': opts->affichage.opt_F = 1; break;
        case 'v': opts->affichage.opt_v = 1; break;
        case 'r': opts->opt_r = 1; break;
        case 'm': opts->opt_m = 1; break;
        case 'd': opts->opt_d = 1; break;

        case 'p':
            /* -p nécessite un argument PID */
            if (i + 1 >= argc) {
                fprintf(stderr, "Erreur: -p nécessite un PID\n");
                return -1;
            }
            opts->filtres.pid_filtre = atoi(argv[i + 1]);
            if (opts->filtres.pid_filtre <= 0) {
                fprintf(stderr, "Erreur: PID invalide '%s'\n", argv[i + 1]);
                return -1;
            }
            return 1; /* signale qu'on a consommé un argument supplémentaire */

        case 'U': {
            /* -U nécessite un nom d'utilisateur */
            if (i + 1 >= argc) {
                fprintf(stderr, "Erreur: -U nécessite un nom d'utilisateur\n");
                return -1;
            }
            struct passwd *pw = getpwnam(argv[i + 1]);
            if (!pw) {
                fprintf(stderr, "Erreur: utilisateur inconnu '%s'\n", argv[i + 1]);
                return -1;
            }
            opts->filtres.uid_filtre = pw->pw_uid;
            return 1;
        }

        case 'T':
            /* -T nécessite un nom de terminal */
            if (i + 1 >= argc) {
                fprintf(stderr, "Erreur: -T nécessite un terminal (ex: pts/0)\n");
                return -1;
            }
            strncpy(opts->filtres.tty_filtre, argv[i + 1],
                    sizeof(opts->filtres.tty_filtre) - 1);
            return 1;

        case 'k':
            /* -k nécessite une clé de tri */
            if (i + 1 >= argc) {
                fprintf(stderr, "Erreur: -k nécessite une clé (pid/cpu/mem/name)\n");
                return -1;
            }
            strncpy(opts->opt_k, argv[i + 1], sizeof(opts->opt_k) - 1);
            return 1;

        default:
            fprintf(stderr, "Erreur: option inconnue '-%c'\n", c);
            return -1;
    }
    return 0;
}

int parse_ps_args(int argc, char **argv, Options *opts) {
    /* Initialisation de toutes les options à leur valeur par défaut */
    opts->filtres.opt_a      = 0;
    opts->filtres.opt_x      = 0;
    opts->filtres.pid_filtre = 0;
    opts->filtres.uid_filtre = -1;
    opts->filtres.tty_filtre[0] = '\0';
    opts->affichage.opt_u    = 0;
    opts->affichage.opt_f    = 0;
    opts->affichage.opt_F    = 0;
    opts->affichage.opt_v    = 0;
    opts->affichage.opt_forest = 0;
    opts->opt_r              = 0;
    opts->opt_m              = 0;
    opts->opt_k[0]           = '\0';
    opts->opt_d              = 0;

    int i = 1;
    while (i < argc) {
        char *arg = argv[i];

        /* Gestion de --forest (option longue) */
        if (strcmp(arg, "--forest") == 0) {
            opts->affichage.opt_forest = 1;
            i++;
            continue;
        }

        /* Toute option doit commencer par '-' */
        if (arg[0] != '-') {
            fprintf(stderr, "Erreur: argument inattendu '%s'\n", arg);
            return 0;
        }

        /* Options groupées (ex: -aux, -aU root) */
        if (strlen(arg) > 2) {
            for (int j = 1; arg[j] != '\0'; j++) {
                int ret = traiter_option(arg[j], i, argc, argv, opts);
                if (ret == -1) return 0;
                if (ret == 1)  i++; /* consomme l'argument suivant */
            }
            i++;
            continue;
        }

        /* Option simple (ex: -a, -p 312) */
        int ret = traiter_option(arg[1], i, argc, argv, opts);
        if (ret == -1) return 0;
        if (ret == 1)  i++; /* consomme l'argument de l'option */
        i++;
    }
    return 1;
}