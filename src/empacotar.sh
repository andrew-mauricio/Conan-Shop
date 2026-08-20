#!/bin/bash
# Monta o pacote do ConanShop para distribuir — a pasta que o dono do servidor
# arrasta para dentro de Conan-Api/Plugins/ e pronto.
#
# POR QUE UM PACOTE SEPARADO, E NAO DENTRO DO PACOTE DA API
# ---------------------------------------------------------
# O `Plugins/` da distribuicao da Conan-Api leva UM plugin de proposito: o
# Permission, que e' servico dos outros. A razao esta escrita no
# montar-distribuicao.sh — se a pasta ja' vier com oito coisas nossas dentro, o
# dono nao sabe o que e' dele, nao sabe o que pode apagar, e sobe no servidor
# coisa que ninguem pediu.
#
# O ConanShop e' um plugin de VERDADE, nao um exemplo: quem baixa quer a loja
# funcionando, nao o fonte para estudar. Entao ele tem pacote proprio, e o dono
# escolhe instalar.
#
# O QUE VAI DENTRO
#   ConanShop.dll        o plugin
#   config.json          o que o dono edita (120 itens ja' preenchidos)
#   PluginInfo.json      a identidade, que o carregador le'
#   LEIA-ME.md           como instalar e como usar
#   TESTE-COM-JOGADOR.md o roteiro do que so' se prova jogando
#
# O conanshop.db NAO vai: ele nasce sozinho na primeira execucao. Mandar um
# banco pronto levaria junto os pontos dos jogadores DESTE servidor.
set -e
AQUI="$(cd "$(dirname "$0")" && pwd)"
SAIDA="${1:-$AQUI/pacote}"
NOME="ConanShop"

# ── o DLL tem de ser desta compilacao ───────────────────────────────────────
#
# Empacotar um DLL velho e' o erro que este projeto ja' cometeu duas vezes: o
# link falha, o arquivo anterior continua no lugar, e o pacote sai com o binario
# de ontem. Comparar o md5 antes e depois e' o unico jeito de saber.
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

# ── os testes PRECISAM passar antes de empacotar ────────────────────────────
#
# Um pacote com a bateria reprovada e' um pacote que nao deveria existir. E o
# codigo 2 (nao consegui rodar) tambem NAO e' aprovacao — e' o contrario dela.
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

# ── o config tem de ser JSON valido ─────────────────────────────────────────
#
# Ele e' o unico arquivo do pacote que uma pessoa edita, e sai daqui ja' com 120
# itens. Mandar um config quebrado faria o plugin recusar-se a subir na maquina
# de quem baixou — com uma mensagem correta e uma primeira impressao pessima.
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

# ── montar ──────────────────────────────────────────────────────────────────
rm -rf "$SAIDA"
mkdir -p "$SAIDA/$NOME"

cp "$AQUI/$NOME.dll"      "$SAIDA/$NOME/"
cp "$AQUI/config.json"    "$SAIDA/$NOME/"
cp "$AQUI/PluginInfo.json" "$SAIDA/$NOME/"
cp "$AQUI/README-CONANSHOP.md"   "$SAIDA/$NOME/LEIA-ME.md"
cp "$AQUI/TESTE-COM-JOGADOR.md"  "$SAIDA/$NOME/" 2>/dev/null || true

# ── a conferencia final: o pacote tem o que o carregador procura? ───────────
#
# O carregador varre `Plugins/<pasta>/*.dll` e le' `PluginInfo.json`. Sem um dos
# dois ele registra o motivo e pula — mas quem baixou ve' "nao funciona".
falta=0
for f in "$NOME.dll" "PluginInfo.json" "config.json"; do
    [ -f "$SAIDA/$NOME/$f" ] || { echo "  x FALTA $f no pacote"; falta=1; }
done
[ "$falta" = "1" ] && exit 1

# O PluginInfo tem de ser legivel: o carregador RECUSA o plugin se nao for.
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
