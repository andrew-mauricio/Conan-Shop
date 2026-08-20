#!/bin/bash
# Compila o ConanShop. Nada alem do compilador cruzado do MinGW e' necessario —
# quem for da comunidade nao precisa de Visual Studio nem do editor da Unreal.
#
# O QUE ENTRA NO DLL, E POR QUE
#   ConanShop.cpp  Config.cpp  Pontos.cpp  Comandos.cpp   o plugin
#   MySqlCliente.cpp                        o cliente MySQL desta casa
#   sqlite3.o                               o banco local E o leitor de JSON
#
# O SQLITE ENTRA DUAS VEZES DE PROPOSITO: ele e' o banco local (conanshop.db) e
# tambem o parser do config.json (json_valid/json_extract/json_each). Escrever
# um parser de JSON a mao para ler arquivo que o dono edita a mao seria codigo
# novo no caminho mais exposto do plugin — e o SQLite ja' esta aqui.
#
# O MySQL entra SEMPRE, mesmo em quem so' vai usar o banco local: um .dll que
# so' linka o MySQL "quando precisa" seriam dois arquivos com o mesmo nome, e o
# suporte a um servidor de terceiro comecaria por descobrir qual dos dois o dono
# baixou.
set -e
AQUI="$(cd "$(dirname "$0")" && pwd)"
OBJ="$AQUI/build"
mkdir -p "$OBJ"

# ── onde estao os headers ───────────────────────────────────────────────────
#
# Procurados, nunca assumidos: este mesmo script roda na arvore de
# desenvolvimento (plugins/ConanShop/) e no SDK que a comunidade baixa
# (Exemplos/ConanShop/), que tem formatos diferentes.
INC=""
if [ -n "${CONAN_SDK_INCLUDE:-}" ] && [ -f "$CONAN_SDK_INCLUDE/Conan/ConanPluginApi.h" ]; then
    INC="$CONAN_SDK_INCLUDE"
else
    d="$AQUI"
    for _ in 1 2 3 4 5 6; do
        for c in "$d/include" "$d/api/include"; do
            [ -f "$c/Conan/ConanPluginApi.h" ] && { INC="$c"; break 2; }
        done
        d="$(dirname "$d")"
        [ "$d" = "/" ] && break
    done
fi
if [ -z "$INC" ]; then
    echo "  x nao achei Conan/ConanPluginApi.h. Procurei a partir de: $AQUI"
    echo "    Aponte com:  CONAN_SDK_INCLUDE=/caminho/do/sdk/include ./compilar.sh"
    exit 1
fi

# O header do Permission mora em lugar diferente na arvore e no SDK.
PERM=""
if [ ! -f "$INC/Conan/ConanPermission.h" ]; then
    for c in "$AQUI/../Permission/include" "$AQUI/../../plugins/Permission/include" \
             "$AQUI/../../Exemplos/Permission/include"; do
        [ -f "$c/Conan/ConanPermission.h" ] && { PERM="-I $c"; break; }
    done
fi
if [ -z "$PERM" ] && [ ! -f "$INC/Conan/ConanPermission.h" ]; then
    echo "  x nao achei Conan/ConanPermission.h — o ConanShop consulta o Permission"
    echo "    para VIP e para a identidade do jogador."
    exit 1
fi

# O que e' generico mora em plugins/comum e NAO e' copiado para ca.
COMUM=""
for c in "$AQUI/../comum" "$AQUI/../../plugins/comum" "$AQUI/comum"; do
    [ -f "$c/MySqlCliente.h" ] && { COMUM="$c"; break; }
done
if [ -z "$COMUM" ]; then
    echo "  x nao achei MySqlCliente.h (esperado em plugins/comum/)"
    exit 1
fi

FLAGS_SQLITE=(
  -O2
  -DSQLITE_THREADSAFE=1
  -DSQLITE_OMIT_LOAD_EXTENSION
  -DSQLITE_DEFAULT_MEMSTATUS=0
  -DSQLITE_OMIT_DEPRECATED
  -DSQLITE_DQS=0
  -DSQLITE_ENABLE_JSON1
  -DSQLITE_MAX_EXPR_DEPTH=0
  -DSQLITE_DEFAULT_FOREIGN_KEYS=1
)

if [ ! -f "$OBJ/sqlite3.o" ]; then
  echo "== sqlite3.c (uma vez, ~75 s) =="
  x86_64-w64-mingw32-gcc -c "$COMUM/terceiros/sqlite3/sqlite3.c" \
      -o "$OBJ/sqlite3.o" "${FLAGS_SQLITE[@]}"
fi

echo "== ConanShop.dll =="
# -Wl,--no-insert-timestamp: build REPRODUZIVEL. O MinGW carimba a hora da
# compilacao no cabecalho PE, entao duas compilacoes do MESMO fonte saiam com
# md5 diferente. Isso impedia provar que o binario publicado veio do fonte
# publicado — a unica pergunta que importa para quem baixa um DLL que vai rodar
# dentro do servidor dele. Sem a flag, "confira o hash" nao significava nada.
#
# -Wl,--exclude-all-symbols: o DLL exporta SO' o que tem __declspec(dllexport).
# Sem isso o MinGW auto-exporta as ~250 funcoes sqlite3_* que estao estaticamente
# dentro dele, e outro plugin que fizesse GetProcAddress pegaria o NOSSO handle.
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -shared \
    -I "$INC" $PERM -I "$AQUI" -I "$COMUM" \
    -o "$OBJ/ConanShop.dll" \
    "$AQUI/ConanShop.cpp" "$AQUI/Config.cpp" "$AQUI/Pontos.cpp" "$AQUI/Comandos.cpp" \
    "$COMUM/MySqlCliente.cpp" "$OBJ/sqlite3.o" \
    -static-libgcc -static-libstdc++ -static -lws2_32 \
    -Wl,--exclude-all-symbols \
    -Wl,--no-insert-timestamp

cp "$OBJ/ConanShop.dll" "$AQUI/ConanShop.dll"

# ── a guarda de exportacao, com controle positivo ───────────────────────────
#
# Contar zero so' prova alguma coisa se o instrumento souber contar. A isca
# EXPORTA de proposito o que a guarda procura; se ela nao enxergar a isca, a
# guarda esta cega e o "zero" do DLL de verdade nao vale nada.
echo
echo "== a guarda de exportacao sabe enxergar? (controle positivo) =="
ISCA="$OBJ/isca_da_guarda"
cat > "$ISCA.cpp" <<'ISCAEOF'
extern "C" __declspec(dllexport) int sqlite3_teste_da_guarda() { return 1; }
ISCAEOF
if x86_64-w64-mingw32-g++ -std=c++17 -O0 -shared -o "$ISCA.dll" "$ISCA.cpp" -static 2>/dev/null; then
    IP=$(x86_64-w64-mingw32-objdump -p "$ISCA.dll" | grep -c "sqlite3_" || true)
    if [ "$IP" -eq 0 ]; then
        echo "  [ x ] a guarda NAO enxerga nem a isca. Zero no DLL real nao prova nada."
        exit 1
    fi
    echo "  [ok] a isca foi vista ($IP simbolo(s)) — a guarda enxerga."
fi

N=$(x86_64-w64-mingw32-objdump -p "$AQUI/ConanShop.dll" | grep -c "sqlite3_\|MySqlCliente" || true)
if [ "$N" -ne 0 ]; then
    echo "  [ x ] o DLL exporta $N simbolo(s) interno(s) que deveria esconder."
    exit 1
fi
echo "  [ok] nenhum simbolo interno exportado"

x86_64-w64-mingw32-objdump -p "$AQUI/ConanShop.dll" | grep -q "ConanPluginCarregar" \
  && echo "  [ok] ConanPluginCarregar exportada" \
  || { echo "  [ x ] ConanPluginCarregar NAO foi exportada — o loader nao vai achar."; exit 1; }

echo
echo "  ✅ $AQUI/ConanShop.dll"
echo
echo "  INSTALAR (a pasta tem HIFEN: Conan-Api, nunca ConanApi)"
echo "    <servidor>/ConanSandbox/Binaries/Win64/Conan-Api/Plugins/ConanShop/"
echo "      ConanShop.dll"
echo "      PluginInfo.json"
echo "      config.json        <- o banco conanshop.db nasce aqui do lado"
