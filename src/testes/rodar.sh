#!/bin/bash
# Roda a bateria do ConanShop SOB WINE — o ambiente de verdade.
#
# POR QUE SOB WINE, E NAO EM LINUX NATIVO
# ----------------------------------------
# O plugin roda dentro do servidor Windows do Conan, que nesta VPS roda sob
# Wine. Testar a logica em Linux nativo provaria que ela funciona num lugar onde
# ela nao vai rodar — e as diferencas que importam (o SQLite abrindo arquivo, o
# escalonamento das threads na corrida) sao justamente as que mudam entre os
# dois.
#
# O QUE CADA UM PROVA
#   controle_positivo  que o teste_pontos SABE reprovar (ver abaixo)
#   teste_pontos       a carteira: ninguem gasta o que nao tem, nem duas vezes
#   teste_config       config quebrada NAO substitui a boa
#   teste_texto        !shop nao engole !shopreload; o formato do dono nao e' printf
#
# O CONTROLE POSITIVO NAO E' ENFEITE. Na primeira versao, o teste_pontos passou
# e o controle positivo REPROVOU: com o mutex do proprio teste em volta, ate' a
# implementacao defeituosa passava. O instrumento estava cego e o "APROVADO" nao
# media nada. Enquanto o controle positivo nao passar, ignore o resto.
set -e
AQUI="$(cd "$(dirname "$0")" && pwd)"
PLUG="$AQUI/.."
OBJ="$PLUG/build"
mkdir -p "$OBJ"

COMUM=""
for c in "$PLUG/../comum" "$PLUG/../../plugins/comum" "$PLUG/comum"; do
    [ -f "$c/MySqlCliente.h" ] && { COMUM="$c"; break; }
done
[ -z "$COMUM" ] && { echo "  x nao achei plugins/comum/ (MySqlCliente.h)"; exit 1; }

[ -f "$OBJ/sqlite3.o" ] || {
  echo "== sqlite3.c (uma vez, ~75 s) =="
  x86_64-w64-mingw32-gcc -c "$COMUM/terceiros/sqlite3/sqlite3.c" -o "$OBJ/sqlite3.o" \
    -O2 -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 \
    -DSQLITE_OMIT_DEPRECATED -DSQLITE_DQS=0 -DSQLITE_ENABLE_JSON1 \
    -DSQLITE_MAX_EXPR_DEPTH=0 -DSQLITE_DEFAULT_FOREIGN_KEYS=1
}

echo "== compilando a bateria =="
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -I "$PLUG" -I "$COMUM" \
  -o "$OBJ/teste_pontos.exe" \
  "$AQUI/teste_pontos.cpp" "$PLUG/Pontos.cpp" "$COMUM/MySqlCliente.cpp" "$OBJ/sqlite3.o" \
  -static-libgcc -static-libstdc++ -static -lws2_32

x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -I "$COMUM" \
  -o "$OBJ/controle_positivo.exe" \
  "$AQUI/controle_positivo.cpp" "$OBJ/sqlite3.o" \
  -static-libgcc -static-libstdc++ -static

x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -I "$PLUG" -I "$COMUM" \
  -o "$OBJ/teste_config.exe" \
  "$AQUI/teste_config.cpp" "$PLUG/Config.cpp" "$OBJ/sqlite3.o" \
  -static-libgcc -static-libstdc++ -static

x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -I "$PLUG" \
  -o "$OBJ/teste_texto.exe" "$AQUI/teste_texto.cpp" \
  -static-libgcc -static-libstdc++ -static

# ── onde rodar ──────────────────────────────────────────────────────────────
#
# Aceita um wine local; se nao houver, tenta o do container do servidor, que e'
# onde o Wine desta VPS mora. Sem nenhum dos dois, PARA com codigo 2 — em vez de
# "pular os testes" e devolver sucesso, que e' a forma mais comum de bateria
# mentir. Codigo 2 quer dizer NAO CONFERI, e isso nao e' aprovacao.
RODAR=""
if command -v wine >/dev/null 2>&1; then
    RODAR="local"
elif command -v docker >/dev/null 2>&1 && docker ps --format '{{.Names}}' 2>/dev/null | grep -q '^conan_Server$'; then
    RODAR="container"
else
    echo "  x nao ha wine local nem o container conan_Server de pe."
    echo "    Os testes NAO rodaram. Isto NAO e' aprovacao."
    exit 2
fi

executar() {   # $1 = exe, $2... = argumentos
    exe="$1"; shift
    if [ "$RODAR" = "local" ]; then
        ( cd "$OBJ" && WINEDEBUG=-all wine "./$exe" "$@" 2>/dev/null )
    else
        # A pasta ~/conan/logs esta montada dentro do container.
        ponte="/home/andrew/conan/logs"
        sudo -n install -o conanlink -g conanlink -m 755 "$OBJ/$exe" "$ponte/" 2>/dev/null \
          || cp "$OBJ/$exe" "$ponte/"
        docker exec conan_Server sh -c \
          "cd /home/conan/logs && WINEDEBUG=-all timeout 300 wine ./$exe $* 2>/dev/null"
    fi
}

FALHOU=0
NAO_CONFERI=0
set +e
# Cada teste quer um argumento diferente, e passar o errado faz o teste
# reprovar por motivo que nao e' o dele — aconteceu aqui: o teste_config
# recebeu um nome de BANCO onde esperava uma PASTA, e acusou "nao consegui
# abrir bom.json" como se o parser estivesse quebrado.
for t in controle_positivo teste_pontos teste_config teste_texto; do
    echo
    echo "══════════════ $t ══════════════"
    case "$t" in
        teste_config) executar "$t.exe" "." ;;    # pasta onde escrever os .json
        teste_texto)  executar "$t.exe"     ;;    # nao usa argumento
        *)            executar "$t.exe" "$t.db" ;;
    esac
    rc=$?
    # ── REPROVOU x NAO CONSEGUI RODAR ───────────────────────────────────────
    #
    # A versao anterior fazia `else FALHOU=1` — qualquer codigo diferente de
    # zero virava REPROVADO. So' que os testes devolvem 1 quando reprovam, e
    # tudo o mais vem de FORA deles:
    #
    #   124  o `timeout 300` estourou
    #   125  o docker nao conseguiu executar
    #   126  o binario nao e' executavel
    #   127  nao achou o comando (wine sumiu?)
    #   137  morto por sinal (OOM, kill)
    #
    # Isso importa porque estes testes rodam DENTRO do container do servidor de
    # jogo, compartilhando o wineserver com o Conan. Com o servidor sob carga,
    # o wine as vezes nao responde — e a bateria dizia "REPROVADA", mandando
    # procurar um defeito que nao existe.
    #
    # Medido em 20/08/2026: 1 reprovacao em ~10 rodadas, sem nenhuma alteracao
    # de codigo entre elas. Um teste que reprova as vezes e' pior que um que
    # reprova sempre: ensina a ignorar a reprovacao.
    #
    # Agora: 0 = passou · 1 = REPROVOU de verdade · resto = NAO CONFERI, que
    # nao e' aprovacao nem reprovacao.
    case $rc in
        0) echo "   -> passou" ;;
        1) echo "   -> REPROVOU"; FALHOU=1 ;;
        *) echo "   -> NAO CONSEGUI RODAR (codigo $rc — timeout, docker ou wine)."
           echo "      Isto NAO e' reprovacao, e tambem NAO e' aprovacao."
           NAO_CONFERI=1 ;;
    esac
done
set -e

# ── nao deixar rastro na pasta do servidor ──────────────────────────────────
#
# Quando roda pelo container, a ponte e' `~/conan/logs`, que e' uma pasta REAL
# do servidor montada la' dentro. Os .exe, os .db e os .json que o teste_config
# escreve ficavam ali depois — 5 MB de lixo nosso numa pasta que o dono do
# servidor abre para ver LOG. Some quem criou.
if [ "$RODAR" = "container" ]; then
    ponte="/home/andrew/conan/logs"
    for t in controle_positivo teste_pontos teste_config teste_texto; do
        sudo -n rm -f "$ponte/$t.exe" "$ponte/$t.db" \
                      "$ponte/$t.db-wal" "$ponte/$t.db-shm" 2>/dev/null || true
    done
    # os .json que o teste_config escreve para exercitar o parser
    for j in bom quebrado semid mysqll iguais misto limites min0; do
        sudo -n rm -f "$ponte/$j.json" 2>/dev/null || true
    done
    sudo -n rm -f "$ponte/cp.db" 2>/dev/null || true
fi

echo
if [ "$FALHOU" != "0" ]; then
    echo "  ❌ bateria REPROVADA"
    exit 1
fi
if [ "$NAO_CONFERI" != "0" ]; then
    echo "  ⚠  NAO CONSEGUI CONFERIR tudo (algum teste nao rodou)."
    echo "     Isto NAO e' aprovacao. Tente de novo com o servidor mais calmo,"
    echo "     ou rode com wine local em vez do container."
    exit 2
fi
echo "  ✅ bateria completa APROVADA (rodada: $RODAR)"
exit 0
