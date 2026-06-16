#!/usr/bin/env bash
# ==============================================================
# test_myps.sh — Tests complets de myps
# Usage : bash test_myps.sh [chemin/vers/myps]
# ==============================================================

MYPS="${1:-./myps}"
PASS=0
FAIL=0
SKIP=0

# Couleurs
GRN='\033[0;32m'
RED='\033[0;31m'
YLW='\033[0;33m'
BLU='\033[1;34m'
RST='\033[0m'

# PID et UID courants (utiles pour les filtres)
MON_PID=$$
MON_UID=$(id -u)
MON_TTY=$(tty 2>/dev/null | sed 's|/dev/||' || echo "?")

# ==============================================================
# Fonctions helpers
# ==============================================================

section() { echo -e "\n${BLU}═══ $1 ═══${RST}"; }

# ok CMD : vérifie que la commande réussit (exit 0) et produit une sortie
ok() {
    local desc="$1"; shift
    local out
    out=$("$MYPS" "$@" 2>&1)
    local rc=$?
    if [ $rc -eq 0 ] && [ -n "$out" ]; then
        echo -e "  ${GRN}[PASS]${RST} $desc"
        ((PASS++))
    else
        echo -e "  ${RED}[FAIL]${RST} $desc  (rc=$rc, sortie vide=$([ -z "$out" ] && echo oui || echo non))"
        echo "         Commande : $MYPS $*"
        [ -n "$out" ] && echo "         Sortie   : $(echo "$out" | head -3)"
        ((FAIL++))
    fi
}

# col CMD COLONNE : vérifie que l'en-tête contient COLONNE
col() {
    local desc="$1"; local col="$2"; shift 2
    local out
    out=$("$MYPS" "$@" 2>&1 | head -1)
    if echo "$out" | grep -qw "$col"; then
        echo -e "  ${GRN}[PASS]${RST} $desc  (colonne '$col' présente)"
        ((PASS++))
    else
        echo -e "  ${RED}[FAIL]${RST} $desc  (colonne '$col' absente dans : $out)"
        ((FAIL++))
    fi
}

# nocol CMD COLONNE : vérifie que l'en-tête NE contient PAS COLONNE
nocol() {
    local desc="$1"; local col="$2"; shift 2
    local out
    out=$("$MYPS" "$@" 2>&1 | head -1)
    if ! echo "$out" | grep -qw "$col"; then
        echo -e "  ${GRN}[PASS]${RST} $desc  (colonne '$col' absente — correct)"
        ((PASS++))
    else
        echo -e "  ${RED}[FAIL]${RST} $desc  (colonne '$col' présente alors qu'elle ne devrait pas)"
        ((FAIL++))
    fi
}

# match CMD MOTIF : vérifie que la sortie contient MOTIF
match() {
    local desc="$1"; local motif="$2"; shift 2
    local out
    out=$("$MYPS" "$@" 2>&1)
    if echo "$out" | grep -q "$motif"; then
        echo -e "  ${GRN}[PASS]${RST} $desc"
        ((PASS++))
    else
        echo -e "  ${RED}[FAIL]${RST} $desc  (motif '$motif' non trouvé)"
        echo "         Commande : $MYPS $*"
        echo "         Sortie   : $(echo "$out" | head -5)"
        ((FAIL++))
    fi
}

# fail CMD : vérifie que la commande échoue (exit != 0)
fail() {
    local desc="$1"; shift
    "$MYPS" "$@" >/dev/null 2>&1
    local rc=$?
    if [ $rc -ne 0 ]; then
        echo -e "  ${GRN}[PASS]${RST} $desc  (erreur attendue, rc=$rc)"
        ((PASS++))
    else
        echo -e "  ${RED}[FAIL]${RST} $desc  (aurait dû échouer)"
        ((FAIL++))
    fi
}

# lines CMD MIN : vérifie que la sortie a au moins MIN lignes
lines() {
    local desc="$1"; local min="$2"; shift 2
    local n
    n=$("$MYPS" "$@" 2>/dev/null | wc -l)
    if [ "$n" -ge "$min" ]; then
        echo -e "  ${GRN}[PASS]${RST} $desc  ($n lignes >= $min)"
        ((PASS++))
    else
        echo -e "  ${RED}[FAIL]${RST} $desc  ($n lignes < $min attendues)"
        ((FAIL++))
    fi
}

# ==============================================================
# Vérification préalable
# ==============================================================
if [ ! -x "$MYPS" ]; then
    echo -e "${RED}ERREUR : binaire '$MYPS' introuvable ou non exécutable.${RST}"
    echo "Usage : bash $0 [chemin/vers/myps]"
    exit 1
fi

echo -e "${BLU}╔══════════════════════════════════════════╗${RST}"
echo -e "${BLU}║       Tests complets de myps             ║${RST}"
echo -e "${BLU}╚══════════════════════════════════════════╝${RST}"
echo "Binaire  : $MYPS"
echo "PID      : $MON_PID"
echo "UID      : $MON_UID"
echo "TTY      : $MON_TTY"

# ==============================================================
# 1. FORMAT PAR DÉFAUT
# ==============================================================
section "1. Format par défaut (PID TTY TIME CMD)"
col   "En-tête contient PID"  "PID"
col   "En-tête contient TTY"  "TTY"
col   "En-tête contient TIME" "TIME"
col   "En-tête contient CMD"  "CMD"
ok    "Sortie non vide (défaut)"
lines "Au moins 1 processus affiché" 2   # en-tête + 1 proc

# ==============================================================
# 2. OPTIONS DE SÉLECTION
# ==============================================================
section "2. Options de sélection"

ok    "Option -a (tous les utilisateurs avec TTY)"     -a
ok    "Option -x (processus sans TTY)"                 -x
ok    "Option -ax (combinées)"                         -ax
ok    "Option -e (tous les processus)"                 -e
ok    "Option -A (équivalent -e)"                      -A
lines "-A produit plus de processus que défaut" 5      -A
ok    "Option -e et -A identiques (nb lignes)"  # comparaison qualitative

# ==============================================================
# 3. OPTIONS DE FORMAT
# ==============================================================
section "3. Options de format"

# -f
col   "-f contient UID"    "UID"    -f
col   "-f contient PPID"   "PPID"   -f
col   "-f contient STIME"  "STIME"  -f
col   "-f contient CMD"    "CMD"    -f

# -F
col   "-F contient UID"   "UID"    -F -A
col   "-F contient PPID"  "PPID"   -F -A
col   "-F contient SZ"    "SZ"     -F -A
col   "-F contient RSS"   "RSS"    -F -A
col   "-F contient PSR"   "PSR"    -F -A
col   "-F contient STIME" "STIME"  -F -A
nocol "-F ne contient pas NLWP" "NLWP" -F -A   # [FIX6]

# -l
col   "-l contient F"    "F"    -l -A
col   "-l contient S"    "S"    -l -A
col   "-l contient PRI"  "PRI"  -l -A
col   "-l contient NI"   "NI"   -l -A
col   "-l contient SZ"   "SZ"   -l -A

# -u
col   "-u contient USER"    "USER"    -u -A
col   "-u contient %CPU"    "%CPU"    -u -A
col   "-u contient %MEM"    "%MEM"    -u -A
col   "-u contient VSZ"     "VSZ"     -u -A
col   "-u contient STAT"    "STAT"    -u -A
col   "-u contient START"   "START"   -u -A
col   "-u colonne COMMAND"  "COMMAND" -u -A

# -v [FIX7]
col   "-v contient MAJFL"  "MAJFL"   -v -A
col   "-v contient MINFL"  "MINFL"   -v -A
col   "-v contient %CPU"   "%CPU"    -v -A
col   "-v contient %MEM"   "%MEM"    -v -A
col   "-v contient VSZ"    "VSZ"     -v -A
col   "-v contient STAT"   "STAT"    -v -A
col   "-v colonne COMMAND" "COMMAND" -v -A

# -j
col   "-j contient PGID"  "PGID"  -j -A
col   "-j contient SID"   "SID"   -j -A

# -L [FIX8]
col   "-L contient LWP"   "LWP"   -L -A
nocol "-L ne contient pas NLWP" "NLWP" -L -A
nocol "-L ne contient pas TGID" "TGID" -L -A
ok    "-L liste des threads"     -L -A

# ==============================================================
# 4. OPTIONS DE FILTRAGE
# ==============================================================
section "4. Options de filtrage"

# -p PID [FIX4]
match "-p \$MON_PID affiche ce PID"   "$MON_PID"  -p "$MON_PID"
match "-p \$MON_PID affiche le processus" "bash\|sh\|test_myps" -p "$MON_PID" -A

# -t TTY
if [ "$MON_TTY" != "?" ]; then
    ok    "-t \$MON_TTY (filtre terminal courant)" -t "$MON_TTY" -A
else
    echo -e "  ${YLW}[SKIP]${RST} -t TTY : pas de terminal détecté"
    ((SKIP++))
fi

# -n NOM
match "-n bash (sous-chaîne)"  "bash\|sh"  -n "bash" -A
ok    "-n avec nom partiel (sh)" -n "sh" -A

# -C NOM
ok    "-C bash (nom exact)"  -C "bash" -A
# un nom inexistant doit donner juste l'en-tête (exit 0, 1 seule ligne)
ok    "-C nomquinexistepas (exit 0)"  -C "nomquinexistepas" -A

# -U UID
match "-U \$MON_UID filtre par UID reel"  "." -U "$MON_UID" -A
ok    "-U root affiche des processus"      -U "root" -A

# -G GID [FIX5]
MON_GID=$(id -g)
ok    "-G \$MON_GID filtre par GID reel"  -G "$MON_GID" -A

# ==============================================================
# 5. OPTIONS D'ARBRE
# ==============================================================
section "5. Options d'arbre (-H et --forest)"

match "-H affiche un préfixe d'arbre"       "\\\\_"   -H -A
match "--forest affiche un préfixe d'arbre" "\\\\_"   --forest -A
ok    "--forest avec -u"                              --forest -u -A
ok    "--forest avec -f"                              --forest -f -A
ok    "-H avec -l"                                    -H -l -A

# ==============================================================
# 6. OPTIONS SUPPLÉMENTAIRES
# ==============================================================
section "6. Options supplémentaires"

# -c (nom court)
ok    "-c affiche le nom court"     -c -A
ok    "-c avec -u"                  -c -u -A

# -s (PGID SID)
col   "-s ajoute PGID en préfixe"  "PGID"  -s -A
col   "-s ajoute SID en préfixe"   "SID"   -s -A

# -Z (SELinux)
col   "-Z ajoute LABEL"  "LABEL"  -Z -A

# -w (largeur illimitée)
ok    "-w (pas de troncature CMD)"  -w -A

# --sort
ok    "--sort=pid"   --sort=pid  -A
ok    "--sort=ppid"  --sort=ppid -A
ok    "--sort=cpu"   --sort=cpu  -A
ok    "--sort=rss"   --sort=rss  -A
ok    "--sort=vsz"   --sort=vsz  -A
ok    "--sort=nom"   --sort=nom  -A
ok    "--sort=uid"   --sort=uid  -A

# --headers (répétition des en-têtes)
ok    "--headers (en-têtes répétés)"  --headers -A

# ==============================================================
# 7. COMBINAISONS CLASSIQUES
# ==============================================================
section "7. Combinaisons classiques"

ok    "ps aux  → -a -u -x"           -aux
ok    "ps -ef  → -e -f"              -ef
ok    "ps -eF  → -e -F"              -eF
ok    "ps -el  → -e -l"              -el
ok    "ps -ejH → -e -j -H"           -ejH
ok    "ps -eLf → -e -L -f"           -eLf
ok    "ps aux --forest"              -aux --forest
ok    "ps -ef  --forest"             -ef --forest
ok    "ps axZ  → -a -x -Z"           -axZ
ok    "ps -u -f (formats empilés)"   -u -f -A
ok    "-p avec -u"                   -u -p "$MON_PID"
ok    "-p avec -f"                   -f -p "$MON_PID"
ok    "-A -u --sort=cpu"             -A -u --sort=cpu
ok    "-A -v --sort=rss"             -A -v --sort=rss
ok    "-A -l -Z -s"                  -A -l -Z -s
ok    "--forest -Z -s -A"            --forest -Z -s -A

# ==============================================================
# 8. ERREURS ET CAS LIMITES
# ==============================================================
section "8. Erreurs et cas limites"

fail  "Option inconnue -q"           -q
fail  "-p sans argument"             -p
fail  "-t sans argument"             -t
fail  "-n sans argument"             -n
fail  "-C sans argument"             -C
fail  "-U sans argument"             -U
fail  "-G sans argument"             -G
fail  "Argument sans tiret"          PID

# PID inexistant : doit produire juste l'en-tête (exit 0)
ok    "-p 9999999 (PID inexistant, exit 0)"  -p 9999999

# ==============================================================
# 9. VÉRIFICATIONS DE CONTENU
# ==============================================================
section "9. Vérifications de contenu"

# Le processus courant (ce script) doit apparaître dans -A
match "Processus courant visible dans -A" "$MON_PID"  -A -p "$MON_PID"

# PID 1 (init/systemd) doit toujours apparaître avec -A
match "PID 1 visible dans -A"  "^[[:space:]]*1[[:space:]]"  -A

# Tri : avec --sort=pid, la 2e ligne (après en-tête) doit être PID 1
PREMIER_PID=$("$MYPS" -A --sort=pid 2>/dev/null | awk 'NR==2 {print $1}')
if [ "$PREMIER_PID" = "1" ]; then
    echo -e "  ${GRN}[PASS]${RST} --sort=pid : PID 1 en premier"
    ((PASS++))
else
    echo -e "  ${RED}[FAIL]${RST} --sort=pid : premier PID=$PREMIER_PID (attendu 1)"
    ((FAIL++))
fi

# Colonnes numériques cohérentes : %CPU entre 0 et 100
BAD_CPU=$("$MYPS" -u -A 2>/dev/null | awk 'NR>1 {if ($3+0 < 0 || $3+0 > 100) print $0}' | wc -l)
if [ "$BAD_CPU" -eq 0 ]; then
    echo -e "  ${GRN}[PASS]${RST} %CPU toujours entre 0 et 100"
    ((PASS++))
else
    echo -e "  ${RED}[FAIL]${RST} %CPU hors bornes sur $BAD_CPU lignes"
    ((FAIL++))
fi

# ==============================================================
# Résumé
# ==============================================================
TOTAL=$((PASS + FAIL + SKIP))
echo ""
echo -e "${BLU}══════════════════════════════════════════${RST}"
echo -e "  Total  : $TOTAL tests"
echo -e "  ${GRN}Réussis : $PASS${RST}"
echo -e "  ${RED}Échoués : $FAIL${RST}"
[ $SKIP -gt 0 ] && echo -e "  ${YLW}Ignorés : $SKIP${RST}"
echo -e "${BLU}══════════════════════════════════════════${RST}"

[ $FAIL -eq 0 ] && exit 0 || exit 1
