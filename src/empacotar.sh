#!/bin/bash
# Builds the ConanShop package for distribution — the folder a server owner
# drags into Conan-Api/Plugins/ and is done.
#
# WHY A SEPARATE PACKAGE, AND NOT INSIDE THE API'S
# ------------------------------------------------
# Conan-Api's distribution ships ONE plugin in `Plugins/` on purpose:
# Permission, which is a service to the others. The reasoning is written down in
# montar-distribuicao.sh — if the folder arrives with eight things of ours
# inside, the owner doesn't know what's theirs, doesn't know what they can
# delete, and brings up things on their server nobody asked for.
#
# ConanShop is a REAL plugin, not an example: whoever downloads it wants the
# shop working, not the source to study. So it gets its own package, and the
# owner chooses to install it.
#
# WHAT GOES INSIDE
#   ConanShop.dll             the plugin
#   config.json               what the owner edits (120 items already filled in)
#   PluginInfo.json           the identity the loader reads
#   README.md                 how to install and how to use it
#   TESTING-WITH-A-PLAYER.md  the script for what can only be proved by playing
#
# conanshop.db does NOT go: it's born on its own on the first run. Shipping a
# ready-made database would carry THIS server's players' points along with it.
set -e
AQUI="$(cd "$(dirname "$0")" && pwd)"
SAIDA="${1:-$AQUI/pacote}"
NOME="ConanShop"

# ── the DLL has to come from THIS build ─────────────────────────────────────
#
# Packaging a stale DLL is a mistake this project has already made twice: the
# link fails, the previous file stays where it was, and the package ships
# yesterday's binary. Comparing the md5 before and after is the only way to
# know.
echo "== compilando =="
antes=""
[ -f "$AQUI/$NOME.dll" ] && antes=$(md5sum "$AQUI/$NOME.dll" | cut -d' ' -f1)
"$AQUI/compilar.sh" >/dev/null 2>&1 || { echo "  x a compilacao FALHOU. Nada foi empacotado."; exit 1; }
depois=$(md5sum "$AQUI/$NOME.dll" | cut -d' ' -f1)
if [ -n "$antes" ] && [ "$antes" = "$depois" ]; then
    echo "  (o DLL nao mudou desde a ultima vez: $depois)"
else
    echo "  DLL novo: $depois"
fi

# ── the tests MUST pass before packaging ────────────────────────────────────
#
# A package built on a failed suite is a package that shouldn't exist. And exit
# code 2 (couldn't run them) is NOT a pass either — it's the opposite of one.
echo
echo "== bateria de testes =="
set +e
"$AQUI/testes/rodar.sh" > "$AQUI/build/bateria.log" 2>&1
rc=$?
set -e
case $rc in
  0) echo "  ✅ bateria APROVADA" ;;
  2) echo "  x NAO CONSEGUI RODAR os testes (sem wine e sem o container)."
     echo "    Isto NAO e' aprovacao. Empacotamento abortado."
     echo "    Log: $AQUI/build/bateria.log"; exit 2 ;;
  *) echo "  x bateria REPROVADA. Empacotamento abortado."
     grep -E "\[ X\]|REPROVADO" "$AQUI/build/bateria.log" | head -10
     exit 1 ;;
esac

# ── the config has to be valid JSON ─────────────────────────────────────────
#
# It's the only file in the package a person edits, and it leaves here with 120
# items already in it. Shipping a broken config would make the plugin refuse to
# come up on the downloader's machine — with a correct message and a terrible
# first impression.
echo
echo "== conferindo o config.json =="
python3 - "$AQUI/config.json" <<'PYEOF'
import json, sys
try:
    d = json.load(open(sys.argv[1], encoding="utf-8"))
except Exception as e:
    print(f"  x config.json NAO e' JSON valido: {e}"); sys.exit(1)
itens = {k: v for k, v in d.get("itens", {}).items() if not k.startswith("_")}
if not itens:
    print("  x config.json sem nenhum item — a loja nao venderia nada"); sys.exit(1)
ruins = [k for k, v in itens.items()
         if not isinstance(v, dict) or int(v.get("template_id", 0)) <= 0]
if ruins:
    print(f"  x {len(ruins)} item(ns) sem template_id valido: {ruins[:5]}"); sys.exit(1)
print(f"  ✅ {len(itens)} itens, todos com template_id")
PYEOF

# ── assemble ────────────────────────────────────────────────────────────────
rm -rf "$SAIDA"
mkdir -p "$SAIDA/$NOME"

cp "$AQUI/$NOME.dll"      "$SAIDA/$NOME/"
cp "$AQUI/config.json"    "$SAIDA/$NOME/"
cp "$AQUI/PluginInfo.json" "$SAIDA/$NOME/"
cp "$AQUI/README-CONANSHOP.md"      "$SAIDA/$NOME/README.md"
cp "$AQUI/README-CONANSHOP.pt.md"   "$SAIDA/$NOME/LEIA-ME.pt.md"
cp "$AQUI/TESTING-WITH-A-PLAYER.md" "$SAIDA/$NOME/" 2>/dev/null || true
cp "$AQUI/TESTE-COM-JOGADOR.pt.md"  "$SAIDA/$NOME/" 2>/dev/null || true

# ── the final check: does the package hold what the loader looks for? ───────
#
# The loader sweeps `Plugins/<folder>/*.dll` and reads `PluginInfo.json`.
# Missing either one, it logs the reason and skips — but the person who
# downloaded it just sees "it doesn't work".
falta=0
for f in "$NOME.dll" "PluginInfo.json" "config.json"; do
    [ -f "$SAIDA/$NOME/$f" ] || { echo "  x FALTA $f no pacote"; falta=1; }
done
[ "$falta" = "1" ] && exit 1

# PluginInfo has to be readable: the loader REFUSES the plugin if it isn't.
python3 -c "import json,sys; json.load(open(sys.argv[1],encoding='utf-8'))" \
    "$SAIDA/$NOME/PluginInfo.json" || { echo "  x PluginInfo.json invalido"; exit 1; }

# ── o .tar.gz ───────────────────────────────────────────────────────────────
VERSAO=$(python3 -c "import json;print(json.load(open('$AQUI/PluginInfo.json',encoding='utf-8')).get('Version','0.0.0'))")
TAR="$SAIDA/$NOME-v$VERSAO.tar.gz"
( cd "$SAIDA" && tar czf "$(basename "$TAR")" "$NOME" )

echo
echo "  ✅ $TAR"
echo "     $(du -h "$TAR" | cut -f1)"
echo
echo "  DENTRO:"
tar tzf "$TAR" | sed 's/^/       /'
echo
echo "  COMO O DONO INSTALA (a pasta tem HIFEN: Conan-Api, nunca ConanApi)"
echo "     1. descompacte"
echo "     2. arraste a pasta $NOME para dentro de:"
echo "        <servidor>/ConanSandbox/Binaries/Win64/Conan-Api/Plugins/"
echo "     3. suba o servidor — o conanshop.db nasce sozinho ao lado"
echo
echo "  EXIGE o ConanPermission.dll instalado (identidade do jogador e VIP)."
