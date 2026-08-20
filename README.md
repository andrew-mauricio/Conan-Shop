<div align="center">

# Conan Shop, points and shop plugin for Conan Exiles Enhanced servers

**A server-side shop plugin for privately operated Conan Exiles Enhanced
dedicated servers.** Players earn points for time online, open the catalogue
with `!shop`, and buy with `!comprar`. VIP tiers earn more.

Runs entirely on the server, as a native plugin for
**[Conan-Api](https://github.com/andrew-mauricio/Conan-Api)**. Players connect
with an unmodified client and download nothing.

*If you know **ArkShop** or **AsaShop** from ARK, same idea, same commands,
adapted to how Conan Exiles actually identifies items.*

<img src=".github/imagens/loja.png" width="440" alt="Conan Exiles shop plugin catalogue shown in the in-game message box">

**[English](README.md)** ·
[Português](Docs/README.pt.md) ·
[Manual](Docs/MANUAL.md) ·
[Build from source](src/COMPILAR.md) ·
[Download](../../releases)

</div>

---

## How it works, in practice

**For the player:** they play. Every 5 minutes they earn points. Whenever they
want, they type `!shop`, see what is on offer, and buy.

```
!pontos          how many points I have
!shop            the catalogue, on screen
!shop 2          next page
!comprar stone   buy, and the item lands in the inventory
```

**For you, the server operator:** install one folder, edit one file. Who earns
what, what is sold and for how much, all in `config.json`.

<table>
<tr>
<td width="50%" valign="top">

**The player buys…**

<img src=".github/imagens/compra.png" width="100%" alt="Conan Exiles shop plugin confirming a purchase in the game HUD">

</td>
<td width="50%" valign="top">

**…and the item arrives in the inventory**

<img src=".github/imagens/inventario.png" width="100%" alt="Purchased stone delivered into the Conan Exiles player inventory">

</td>
</tr>
</table>

---

## Install

1. Download `ConanShop-v1.0.0.tar.gz` from **[Releases](../../releases)**
2. Extract it
3. Drop the `ConanShop` folder into:
   ```
   <server>\ConanSandbox\Binaries\Win64\Conan-Api\Plugins\
   ```
4. Start the server

That's all. The points database is created on first run, next to the plugin.

> **Requires [Conan-Api](https://github.com/andrew-mauricio/Conan-Api)
> installed**, together with the **Permission** plugin that ships with it.
> Permission provides stable player identity, the key the wallet is stored
> under, and the notion of VIP.

The folder ends up looking like this, and that is the whole of it:

```
Conan-Api/Plugins/ConanShop/
   ConanShop.dll      the plugin
   config.json        the file YOU edit
   PluginInfo.json    name, version, minimum API version
   conanshop.db       created on first run (the players' points)
```

---

## Coming from ArkShop

Same design, deliberately. What differs:

| | ArkShop (ARK and ASA) | Conan Shop |
|---|---|---|
| earning points | over time, per group | **same** |
| opening the shop | `/shop` | `!shop` (configurable) |
| buying | `/buy id` | `!comprar id` |
| checking balance | `/points` | `!pontos` |
| reloading config | `ArkShop.Reload` over RCON | `!shopreload` in chat *(see below)* |
| VIP | Permissions | Permission |
| storage | SQLite or MySQL | **same** |
| what identifies an item | blueprint path | **the Template ID** *(see below)* |

### Two differences that matter

**1. An item isn't a blueprint.** In ARK, "one blueprint = one item" holds. In
Conan it doesn't: Stone (`10001`) and Brimstone (`14171`) share **the same**
`ItemClass` (`/Script/ConanSandbox.GameItem`). Hundreds of simple items share
that one native class, only the **Template ID** tells them apart.

A shop modelled the ARK way would deliver the wrong item for that entire family,
**successfully**, without a single error in the log. That's why everything here
is keyed on `template_id`.

**2. There's no RCON command.** Conan's RCON accepts a fixed list of commands
and doesn't register new ones, measured, not assumed. Riding along on
`broadcast` doesn't work either: five hooks were armed simultaneously, the
server replied *"Message has been broadcast."*, and **none of them fired**. The
RCON path is native and doesn't go through the engine's reflection.

Two paths that do work, instead:

- `!shopreload` in chat, for anyone holding `shop.admin`
- a **command file**, for web panels, scripts and SSH (below)

---

## Administering from outside the game

Write lines into `Conan-Api/SHOP-COMANDOS`:

```
dar        Indio#76973 500 event prize
tirar      A-4QR7CRS0F 100 refund
definir    Indio#76973 0
saldo      Indio#76973
grupo      Indio#76973 vip 30
tirargrupo Indio#76973 vip
recarregar
```

The plugin picks it up within 3 seconds and answers, line by line, in
`Conan-Api/SHOP-RESPOSTAS`:

```
linha 1: ok dar A-4QR7CRS0F +500 (saldo 1250)
linha 2: RECUSADO tirar A-9XX -100 — saldo insuficiente (tem 40). NADA foi tirado.
```

This works for any automation: a web panel writes the file, an event script
rewards the winners, a scheduled task hands out a weekend bonus.

`grupo` and `tirargrupo` reach the Permission plugin, so VIP can be sold and
expired by script — with a duration in days — without restarting the server.

---

## The `config.json`

The file is commented inline. What you will actually change:

```json
"pontos": {
  "minutos": 5,
  "somar": false,
  "grupos": { "default": 5, "vip": 15, "vipplus": 25 }
}
```

With the defaults, a regular player earns **60 points per hour**. That's the
yardstick for reading prices. `"somar": false` grants the **highest** value among
the player's groups (a VIP who is also in `default` earns 15, not 20).

```json
"stone": {
  "nome": "Pedra",
  "categoria": "recurso",
  "template_id": 10001,
  "quantidade": 100,
  "preco": 10
}
```

The key (`stone`) is what the player types: `!comprar stone`.
`"permissao": "shop.vip"` restricts an item, and it won't even appear in the
catalogue of players who can't buy it.

**120 items ship configured**, across 10 categories, picked from the 9,121 the
game exposes. The prices are a starting point; tune them.

> Note: the plugin's own chat commands and messages are Portuguese by default,
> because that is what its first server needed. Every command name and every
> message is configurable in `config.json` — rename them to English, or to any
> language, without touching code.

---

## Where the item list comes from

From the **ExtratorItemTable** plugin that ships with Conan-Api. With the server
running, create the file `Conan-Api/EXTRAIR-ITEMTABLE`. It reads
`/Game/Items/ItemTable` — Funcom's own DataTable — and writes:

```
itemtable-conan.json     for programs
itemtable-conan.csv      for spreadsheets (UTF-8 BOM, so Excel gets accents right)
```

On build `24784646` that is **9,121 rows × 120 columns**: name, category, tier,
DLC, stack size, class. It extracts the complete ItemTable exposed by that server
build, read through the engine's own DataTable accessors, it doesn't depend on
anyone having opened a chest, because it reads the table rather than loaded
objects.

---

## What it protects, and how

### Nobody spends what they don't have, and nobody spends twice

The debit is **a single UPDATE with the balance condition inside it**. The
database decides, under its own lock, there's no window between "read the
balance" and "spend it", because the balance is never read first.

Measured: 8 concurrent clients, 400 attempts competing for exactly 20 affordable
purchases.

| implementation | went through | final balance |
|---|---|---|
| check outside the UPDATE | 26 | **−60** |
| condition inside the UPDATE *(this plugin)* | **20** | **0** |

This matters concretely when two servers point at the same MySQL, where no lock
is shared between the processes.

### A broken config doesn't replace a working one

You edit `config.json` at 9pm, miss a comma, and run `!shopreload`. The plugin
**refuses it** and keeps the previous configuration, saying why. Without that,
the shop would go empty at peak hours with no visible error, because "zero
items" is a valid state.

### If delivery fails, the points come back

And it's recorded in the ledger, with the reason. When a player says *"I bought
it and never got it"*, the ledger answers.

---

## Security and trust model

This plugin is a native DLL running **inside the dedicated server process, with
that process's privileges**. So is every other plugin on the server — Conan-Api
doesn't sandbox plugins from each other.

Practical consequences for you as the operator:

- Any plugin you install can read this plugin's database, including player
  identity and balances.
- A bug in any native plugin, including this one, can crash the server.
- Install what you trust. This plugin's full source is in
  [`src/`](src/), including its tests, and the published binary is
  reproducible, see [Build from source](src/COMPILAR.md).

---

## Tested

Not "should work" — each line below was measured:

- wallet and atomic debit, with a **positive control** proving the test can fail
- `config.json`: 8 cases, including valid JSON containing invalid items
- command routing: 11 cases (`!shop` doesn't swallow `!shopreload`)
- command file: 10 cases, including the ones that must be refused
- **and with a real player in game**: `!pontos`, `!shop` drawing the message box,
  `!comprar` delivering 100 stone into the inventory, and timed points crediting
  during the session

The suite runs with `src/testes/rodar.sh`, under Wine, the real environment.
Exit code 0 means passed, 1 means a real failure, and 2 means *could not verify*,
which is neither.

---

## Licence

**Conan Shop (this repository) is MIT.** Fork it, modify it, ship it, sell it.

The **[SDK](https://github.com/andrew-mauricio/Conan-Api-SDK)** it's built with
is MIT too, nothing from the runtime is linked into your binary.

The **[Conan-Api runtime](https://github.com/andrew-mauricio/Conan-Api)** has its
own, more restrictive licence: run it on as many servers as you like, including
servers that charge players, but don't resell or re-host the API itself.

Full text in [LICENSE](LICENSE).

---

## The three repositories

| repository | for whom |
|---|---|
| **[Conan-Api](https://github.com/andrew-mauricio/Conan-Api)** | server administrators — the loader and the packaged runtime |
| **[Conan-Api-SDK](https://github.com/andrew-mauricio/Conan-Api-SDK)** | plugin developers — headers, examples, reflected catalogue |
| **[Conan-Shop](https://github.com/andrew-mauricio/Conan-Shop)** | a finished shop plugin, and the reference implementation of a real plugin |

---

<div align="center">

**Conan Shop is an independent, community-developed project. It isn't
affiliated with, endorsed by, sponsored by, or supported by Funcom or Inflexion
Games.**

<sub>*Conan Exiles* and all related marks are the property of Funcom.</sub>

</div>
