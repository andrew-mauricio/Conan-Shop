# Test script: with a real player

*Portuguese translation: [TESTE-COM-JOGADOR.pt.md](TESTE-COM-JOGADOR.pt.md)*

Three things about ConanShop **cannot be proved without somebody inside the
game**. They're written against the signature this build's reflection declares,
and the *chat → player* path is already proved live by ExemploJogador — but on
this project that doesn't count as done. *A 200 from curl doesn't prove the
screen.*

Do these in order: each step depends on the previous one having worked, and
stopping at the first failure saves you the investigation.

Keep the log open in a window:

```bash
tail -f ~/conan/servidor/ConanSandbox/Binaries/Win64/Conan-Api/Logs/ConanApi.log | grep -i shop
```

---

## 1. `!pontos` — proves identity

Join the server and type in chat:

```
!pontos
```

**Expected:** *"Voce tem 0 ponto(s)."*

| what happened | what it means |
|---|---|
| it answered with a number | ✅ identity works; carry on |
| nothing happens, and the log says *"nao consegui identificar quem digitou"* | `ConanPermission.dll` isn't loaded, or `id_do_controller` didn't resolve |
| nothing happens and the log is silent | the chat hook didn't catch the message — check that `[shop] pronto` appears in the log |
| *"A loja esta fora do ar"* | the database didn't answer; the error will be in the log just above |

---

## 2. Earning points — proves the timer

Stay online for **5 minutes** (the default interval).

**Expected:** *"Voce recebeu 5 ponto(s). Total: 5"*

If you're in `permission.json`'s `admin` group it's **25**, not 5 — the plugin
gives the highest value among the player's groups.

To skip the wait, give yourself points from outside (the server doesn't need to
stop):

```bash
echo "dar SEU-NOME#0000 500 teste" | sudo tee \
  ~/conan/servidor/ConanSandbox/Binaries/Win64/Conan-Api/SHOP-COMANDOS
```

then read the answer in `Conan-Api/SHOP-RESPOSTAS` (it comes within 3 s).

---

## 3. `!shop` — proves the SCREEN

```
!shop
```

**Expected:** the game's message box (the same one as "Esqueleto empalado")
opening with the list, grouped by category:

```
[recurso]
  Stone                  x100  10 pts   (stone)
  Wood                   x100  10 pts   (wood)
  ...

!comprar <id>   ·   pagina 1 de 10   ·   !shop 2 para a proxima
```

| what happened | what it means |
|---|---|
| the box opened with the list | ✅ `ClientShowMessageBox` works |
| **nothing appears on screen** | the function exists but didn't draw — try `"usar_tela": false` in the config to see whether the content comes out in chat. If it does, the problem is only the screen |
| the box opened **empty** | the text didn't cross as FText — likely a problem in `ConanApi::TextoRico` with long text |
| text cut off at the end | drop `itens_por_pagina` to 8 or 6 |

Test `!shop 2` and `!shop 99` as well (that last one should say the page doesn't
exist).

> **Remember:** the box's button belongs to the game and only closes. It doesn't
> become "next page" — paging is by command.

---

## 4. `!comprar` — proves DELIVERY

The step that matters most. Buy the cheapest item:

```
!comprar stone
```

**Expected:**
1. the game shows the item-received notification (`SpawnTemplateItem`'s
   `ShowNotification`);
2. **100 stone appear in your inventory**;
3. chat answers *"Comprou Stone x100 por 10 ponto(s). Saldo: 490"*.

| what happened | what it means |
|---|---|
| the item arrived in the inventory | ✅ **the shop is complete** |
| *"Nao consegui entregar o item. Seus pontos foram devolvidos."* | check the log: it says the exact reason. Check the balance with `!pontos` — it has to be back to what it was |
| the log says *"SpawnTemplateItem nao respondeu nesta build"* | the function changed name or signature in a patch |
| the log says *"o jogo recusou a entrega"* | full inventory, or the TemplateId produces no item |
| the points were charged and the item **didn't** arrive, with no message | **this is the worst case.** Write it down and tell me: it means delivery returned success without delivering |

**Always check the balance afterwards** with `!pontos`. If the purchase failed,
the balance has to be exactly what it was before.

---

## 5. `!shopreload` — proves the permission

Edit any price in `config.json` and type:

```
!shopreload
```

**Expected:** *"Configuracao recarregada: 120 item(ns)."*

Then **break the file on purpose** (delete a `}`) and send it again. It has to
answer that it did **NOT** apply and that it's carrying on with the previous
one — and `!shop` has to keep working. That's the behaviour that stops the shop
from vanishing during peak hours.

---

## 6. `!shopdar` — proves the admin command

```
!shopdar OutroJogador#1234 100
```

It only works for somebody with the `shop.admin` permission
(`permission.json`'s `admin` group has `*`, so it already passes). Whoever
receives the points is told in chat.

Ask somebody without the permission to try it: it has to refuse.

---

## If something fails

The log gives the reason for each step, with `[shop]` at the start of the line.
Copy the whole line — it carries what the API answered, not just that "it didn't
work".

The automated tests (`testes/rodar.sh`) cover the wallet, the config and command
routing, and they pass. What fails here is the boundary with the game, which is
exactly what no automated test reaches.
