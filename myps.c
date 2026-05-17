#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>

//stockage des informations d'un processus
typedef struct{
    int pid;
    int ppid;
    char nom[256];
}process;

//Fonction qui vérifie si un nom est un PID
int est_un_nombre(const char *s){
    if(!s || !*s){ // !s:vérifie si le pointeur est NULL ; !*s: vérifie si la chaîne est vide
        return 0;
    }
    while(*s){
        if(!isdigit(*s)){
            return 0;
        }
        s++;
    }
    return 1;
}

//Affichage de l'arbre des processus
void afficher_arbre(process processes[], int nb_process, int parent, int niveau){
    for(int i = 0; i<nb_process; i++){
        //cherche les enfants du parent actuel
        if(processes[i].ppid == parent){
            //indentation selon le niveau
            for(int j = 0; j<niveau; j++){
                printf("   ");
            }
            //Affichage du processus
            printf("|--%s (%d)\n", 
                processes[i].nom, 
                processes[i].pid);
            //appel recursif pour afficher les enfants de ce processus
            afficher_arbre(processes, nb_process, processes[i].pid, niveau + 1);
        }
    }
}

//Fonction principale
int main(int argc, char *argv[]){
    int opt_e = 0;
    int opt_u = 0;
    int opt_f = 0;
    int opt_c = 0;
    int opt_H = 0;
    int opt;
    //Fonction de gestion des fonctionnalités
    while((opt = getopt(argc,argv, "eufcH")) != -1){
        switch(opt){
            case 'e': opt_e = 1; break;
            case 'u': opt_u = 1; break;
            case 'f': opt_f = 1; break;
            case 'c': opt_c = 1; break;
            case 'H': opt_H = 1; break;
            default:
                printf("Usage: ./myps [-e] [-u] [-f] [-c] [-H] \n");
                return 1;
        }
    }
    //UID: utilisateur courant
    int my_uid = getuid();
    //Pointeur sur le dossier /proc
    DIR *proc = opendir("/proc");
    struct dirent *entry;
    //Test à l'ouverture
    if(!proc){
        perror("Erreur lors de l'ouverture");
        return 1;
    }
    //tableau des processus
    process processes[2048];
    int nb_process = 0;
    //Affichage en fonction des fonctionnalités
    if(opt_f){
        printf("PID     PPID     USER     CMD\n");
        printf("--------------------------------------------------\n");

    }
    else if(opt_u){
        printf("PID     USER     NAME     STATE\n");
        printf("--------------------------------------------------\n");

    }
    else if(opt_c){
        printf("COMMAND\n");

    }
    else if(!opt_H){
        printf("PID     NAME     STATE\n");
        printf("--------------------------------------------------\n");
    }
        
    //Parcours des processus
    while((entry = readdir(proc)) != NULL){
        if(!est_un_nombre(entry->d_name)){
            continue;
        }
        int pid = atoi(entry->d_name);
        //Parcours de status pour trouver l'user
        char path_status[256];
        snprintf(path_status, sizeof(path_status), "/proc/%d/status", pid);

        FILE *fs = fopen(path_status, "r");
        int uid = -1;

        if(fs){
            char line[256];
            while(fgets(line, sizeof(line), fs)){
                if(strncmp(line, "Uid:", 4) == 0){
                    sscanf(line + 4, "%d", &uid);
                    break;
                }
            }
            fclose(fs);
        }

        if(!opt_e && uid!= my_uid){
            continue;
        }
        //Lecture du fichier stat
        char path_stat[256];
        snprintf(path_stat, sizeof(path_stat), "/proc/%d/stat", pid);
        FILE *f =fopen(path_stat, "r");
        if(!f){
            continue;
        }
        int ppid;
        char nom[256];
        char etat;
        //Verfification
        if(fscanf(f, "%d %255s %c %d", &pid, nom, &etat,&ppid) != 4){
            fclose(f);
            continue;
        }
        fclose(f);

        //Enlever les parenthèses
        int len = strlen(nom);
        if(len >= 2){
            nom[len - 1] = '\0';
        }
        char *name_clean = nom + 1;

        //Stockage pour le -H
        processes[nb_process].pid = pid;
        processes[nb_process].ppid = ppid;
        strcpy(processes[nb_process].nom, name_clean);
        nb_process++;

        if(opt_H){
            continue;
        }

        //USER (pour -u et -f)
        struct passwd *pw = getpwuid(uid);
        char *user;
        if (pw != NULL){
            user = pw->pw_name;
        }
        else{
            user = "?";
        }

        //Utilisation de CMDLINE
        char cmdline[512]="";
        
        if(opt_f){
            char path_cmd[256];
            snprintf(path_cmd, sizeof(path_cmd), "/proc/%d/cmdline", pid);
            FILE *fc = fopen(path_cmd, "r");
            
            if(fc){
                size_t n = fread(cmdline, 1, sizeof(cmdline)-1, fc);
                fclose(fc);
                for(size_t i = 0; i<n; i++){
                    if(cmdline[i] == '\0'){
                        cmdline[i] = ' ';
                    }
                }
                cmdline[n] = '\0';
            }
        }
        //Affichage selon les options
        if(opt_f){
            char *affichage_cmd;
            if(strlen(cmdline)>0){
                affichage_cmd = cmdline;
            }
            else{
                affichage_cmd = name_clean;
            }
            printf("%-7d %-7d %-8s %.50s\n", pid, ppid, user, affichage_cmd);
        }
        else if(opt_u){
            printf("%-7d %-8s %-10s %-3c\n", pid, user, name_clean, etat);
        }
        else if(opt_c){
            printf("%s\n", name_clean);
        }
        else{
            printf("%-7d %-10s %-3c\n", pid, name_clean, etat);
        }
    }
    if(opt_H){
        printf("ARBRE DES PROCESSUS\n");
        printf("--------------------------------------------------\n");

        afficher_arbre(processes, nb_process, 1, 0);
    }
    closedir(proc);
    return 0;
        
}
