# my_ps - Implementation de la commande ps en C

Projet : recreer la commande ps en lisant /proc.

## Compilation
  make

## Utilisation
  ./my_ps        -> processus de l'utilisateur
  ./my_ps -a     -> tous les processus
  ./my_ps -p 42  -> uniquement le PID 42

## Colonnes
  PID, USER, NOM, ETAT, %CPU

## Fichiers
  main.c    : code principal
  proc.h    : structure ProcessInfo
  Makefile  : compilation
  README.md : documentation
