#include <stdio.h>//printf, fopen, fclose, fscanf
#include<stdlib.h>//atoi
#include<dirent.h>//opendir, readdir, closedir
#include<ctype.h>//isdigit
#include<string.h>//snprintf


#include<getopt.h>//pour pouvir reconnaitre getopt
#include<unistd.h>// getopt, getuid
#include<pwd.h>//getpwuid:convertir UID en uername

//premiere focntion , est celle qui verifie si ce qui est dans le fichier /proc/pid/stat est un nombre
int est_nombre(const char *s){//la fonction prend en parametre chaque caractere du fichier proc qu'on traverse
    if(s==NULL || *s=='\0'){//null c'est pour verifier que le pointeru existe, et '\0' c'est pour verifier que le premier caractere de la chain n'est pas un caractere de fin
        return 0;
    while (*s!='\0'){
        if(!isdigit(*s)){
            return 0;
        s++;
        }
    }
    return 1;// si on ne trouvre pas d'erreur alors on retoune 1 pour dire que le contenu lu est un nombre
    }
}

int main(int argc, char *argv[]){
    //argc=nobre d'arg tapes
    //argv=tableau des argument tapes sous forme de chaine de caractere, argv[0] est tjr le nom du programme
    //on initialise toutes les options a 0 et ils passeront a 1 une fois qu'il seront detecte

    int opt_a=0;//-a pour tous les processsus
    int opt_u=0;//-u pour affciher la colone user
    int opt_x=0;//-x pour affciher les processus sans tty ie programme qui s'execute en arriere plan sans etre ratache au terminal
    int opt;// va recevoir l'option lu par getoptS

    //pour lire les options de la ligne de cmd, getopt parcours argv et recherche les options "aux", s'il retrouve une ioption alors il retourne son char , s'il y en aplus il reourne -1
    while((opt=getopt(argc,argv,"aux"))!=-1){
        switch(opt){
            case 'a':
                opt_a=1;
                break;
            case 'u':
                opt_u=1;
                break;
            case 'x':
                opt_x=1;
                break;
            default:
                //au cas ou on entre une option qui n'est pas valide, on doit afficher la liste des options et quitter
                fprintf(stderr,"usage: %s[-a] [-u] [-x]\n",argv[0]);//afficher sur la sortie un message d'errreru
        }       return 1;
    }


    //getuid retourne l'id numerique de la personne qui lance le programme
    int mon_iud=(int)getuid();
    
    //dans le fichier stat , il y a plusieurs champs : environs 50 or le champ qui est dedie a la memmoire  vive qui est actuelement utilisee est (rss) est le champ 24
    int tty;//c'est le champ 7 du ficher stat
    long rss;// c'est le champ 24 dediee a la memoire en page
    long avaleur_l;//pour pouvoir lire le champ 24 , on a besoirn de lire les champs qui viennent avant pour cela on utilise des aspirateurs qui vont permettre de sauter les les donnees unitiles dans fscanf
    int avaleur_p;

    DIR *proc_dir;//fait reference a la connexion globale ddu dossier ouvert
    struct dirent *entree;//c'est l'identite d'un seul dossier dans tout ce fichier
    char chemin[256];//va stocke l'@ complete du fichier qu'on veut ouvrir,exple /proc/pid/stat
    FILE *f;//va permettre d'ouvrir le fichier chemin
    int pid;
    char nom[256];
    char Etat;
    int ppid;

    //ouvrons le dossier proc
    proc_dir=opendir("/proc");
    if(proc_dir==NULL){
        printf("erreur: impossible d;ouvrir /proc\n");
        return 1;
    }

    //afficher les en tete
    printf("%-8s %-20s %-6s\n", "PID", "NOM", "ETAT");
    printf("%-8s %-20s %-6s\n", "--------", "--------------------", "------");

    //parcourir le dossier proc
    while((entree=readdir(proc_dir))!=NULL){
        if(!est_nombre(entree->d_name))
            continue;
        pid=atoi(entree->d_name);//convertir le chaine rencontree en chiffre
        
        snprintf(chemin, sizeof(chemin),"/proc/%d/stat",pid);//ecrire dans chemin le format de donnee de /proc/pid/stat
        
        f=fopen(chemin, "r");
        if(f==NULL)
            continue;
        if(fscanf(f,"%d (%255[^)]) %s %d", &pid, nom, &Etat, &ppid)==4){//(%255[^)]) signifie lire les nom entre parenthese et se limiter 255 caracteres
            printf("%-8d %-20s %-6s\n", pid, nom, Etat);
        }
        fclose(f);

    }
    closedir(proc_dir);
    return 0;
}