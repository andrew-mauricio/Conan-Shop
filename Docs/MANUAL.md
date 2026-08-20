# Conan Shop

Loja por pontos para Conan Exiles Enhanced. O jogador ganha pontos por tempo
online, vê a lista com `!shop` e compra com `!comprar`. VIP ganha mais, e quem
manda nisso é o seu `permission.json`.

É o mesmo desenho do **ArkShop**, que muita gente já conhece do ARK e do ASA —
adaptado ao Conan, que funciona diferente por dentro (veja
[Um item não é uma blueprint](#um-item-não-é-uma-blueprint)).

---

## Instalar

Copie a pasta `ConanShop` inteira para dentro de:

```
<servidor>/ConanSandbox/Binaries/Win64/Conan-Api/Plugins/
```

A pasta tem quatro coisas — e três já vêm no pacote:

```
Conan-Api/Plugins/ConanShop/
   ConanShop.dll        o plugin
   PluginInfo.json      o que a API lê para saber quem ele é
   config.json          o que VOCÊ edita
   conanshop.db         nasce sozinho na primeira vez (é o banco dos pontos)
```

**O Permission é obrigatório.** É dele que vem a identidade do jogador (a chave
da carteira) e a noção de VIP. Sem ele o plugin sobe, avisa no log e não
consegue identificar ninguém.

Pronto. Sobe o servidor e a loja está no ar.

---

## Os comandos

| no chat | quem pode | o que faz |
|---|---|---|
| `!shop` | todos | mostra a lista na tela |
| `!shop 2` | todos | página 2 |
| `!comprar pedra` | todos | compra e entrega no inventário |
| `!comprar pedra 3` | todos | compra 3 vezes |
| `!pontos` | todos | mostra o saldo |
| `!shopajuda` | todos | lembra os comandos |
| `!shopdar Fulano#123 500` | admin | dá pontos a alguém |
| `!shopreload` | admin | relê o `config.json` |

Todos podem ser renomeados no `config.json`, em `"comandos"`.

### A tela do `!shop`

A lista aparece na caixa de mensagem do jogo — aquela com um botão embaixo.

**O botão é do jogo e só fecha.** Não dá para transformá-lo em "próxima
página": ele não é nosso. A paginação é por comando: `!shop 2`, `!shop 3`. O
rodapé da lista lembra disso ao jogador.

Se preferir a lista no chat em vez da tela, ponha `"usar_tela": false`.

---

## Dar pontos de fora do jogo

Painel web, script, tarefa agendada, SSH. Escreva linhas em:

```
<servidor>/ConanSandbox/Binaries/Win64/Conan-Api/SHOP-COMANDOS
```

```
dar     Indio#76973 500 premio do evento
tirar   A-4QR7CRS0F 100 estorno
definir Indio#76973 0
saldo   Indio#76973
recarregar
```

O plugin atende em até 3 segundos e responde, linha por linha, em
`Conan-Api/SHOP-RESPOSTAS`:

```
# ConanShop — respostas de 2026-08-20 04:15:02
linha 1: ok dar A-4QR7CRS0F +500 (saldo 1250)
linha 2: RECUSADO tirar A-9XX -100 — saldo insuficiente (tem 40). NADA foi tirado.
```

O jogador pode ser identificado pelo **nome de exibição** (só funciona com ele
online — a resposta diz isso) ou pelo **id da conta**, que funciona sempre.

### Por que não é um comando de RCON

Porque não pode ser, e isso foi **medido**, não suposto:

- O RCON do Conan aceita uma lista fixa de comandos (`help` mostra:
  `listplayers`, `broadcast`, `con`, `exec`, `sql`, `KickPlayer`…). Não existe
  registro de comando novo — a AsaApi tem `AddRconCommand`, o Conan não expõe
  equivalente.
- A esperança seguinte era pegar carona: `broadcast <texto>` chama
  `ConanCheatManager::BroadcastMessage`, que **é** uma UFunction e portanto
  seria hookável. Foi testado com cinco hooks armados ao mesmo tempo. O servidor
  respondeu *"Message has been broadcast."* e **nenhum disparou**. O caminho do
  RCON é nativo e não passa pela reflexão do jogo.
- E `exec` sem argumento **derruba o servidor** (access violation — bug do
  Conan, não do plugin). Não mande.

O arquivo é a porta que sobrou, e é a que toda automação já sabe abrir.

---

## O `config.json`

O arquivo é comentado por dentro (as chaves `_leia_isto`). O resumo:

### Pontos

```json
"pontos": {
  "ligado": true,
  "minutos": 5,
  "somar": false,
  "grupos": { "default": 5, "vip": 15, "vipplus": 25 }
}
```

Os nomes em `grupos` são as **chaves dos grupos do seu `permission.json`**.

`"somar": false` (padrão) dá ao jogador o **maior** valor entre os grupos dele —
um VIP que também está em `default` ganha 15, não 20. `true` soma.

Com o padrão (5 pontos a cada 5 minutos), um jogador comum faz **60 pontos por
hora**. É a régua para ler os preços.

### Banco

`"tipo": "local"` guarda num arquivo ao lado do plugin. Não precisa instalar
nada e é o certo para quase todo mundo.

`"tipo": "mysql"` só para quem roda **vários servidores** e quer os pontos
valendo em todos. Aí `mysql_usuario` e `mysql_banco` passam a ser obrigatórios —
e se estiverem vazios o plugin **não sobe**, de propósito: cair no local calado
mandaria os pontos dos seus jogadores para um arquivo que você nunca vai
procurar.

> Apagar o banco local exige apagar também `conanshop.db-wal` e
> `conanshop.db-shm`. Sem isso o dado **ressuscita** na próxima abertura e o
> apagamento parece feito.

### Itens

```json
"pedra": {
  "nome": "Pedra",
  "categoria": "recurso",
  "template_id": 10001,
  "quantidade": 100,
  "preco": 10
}
```

A chave (`pedra`) é o que o jogador digita: `!comprar pedra`.

`"permissao": "shop.vip"` (opcional) restringe o item — e ele nem aparece na
lista de quem não pode, em vez de aparecer e ser recusado na hora da compra.

---

## Um item não é uma blueprint

Esta é a diferença que mais dá trabalho para quem vem do ARK.

No ARK vale *"uma blueprint = um item"*, e a loja guarda o caminho da blueprint.
**No Conan não.** O que identifica um item é o **Template ID** — o *Row Name* da
tabela `/Game/Items/ItemTable`, que é a tabela canônica da Funcom.

O campo `ItemClass` daquela linha aponta para a blueprint, mas ele **não é
único**:

| item | ID | ItemClass |
|---|---|---|
| Stone | 10001 | `/Script/ConanSandbox.GameItem` ← classe nativa, compartilhada |
| Brimstone | 14171 | `/Script/ConanSandbox.GameItem` ← **a mesma** |
| Katana | 51091 | `/Game/Items/Weapons/Katana2h/BP_Item_KatanaBase…` |

Centenas de itens simples dividem a mesma classe nativa. Uma loja modelada por
blueprint entregaria o item errado para toda essa família — e **funcionando**,
sem um erro sequer no log.

Por isso o `config.json` vende por `template_id`, sempre.

---

## De onde vem a lista de itens

Do plugin **ExtratorItemTable**, que acompanha a Conan-Api. Com o servidor já
carregado, crie o arquivo:

```
<servidor>/ConanSandbox/Binaries/Win64/Conan-Api/EXTRAIR-ITEMTABLE
```

Ele lê a `/Game/Items/ItemTable` inteira e grava, ao lado do plugin:

```
itemtable-conan.json     para programa
itemtable-conan.csv      para planilha (com BOM: o Excel acerta o acento sozinho)
```

Nesta build (24784646) são **9.121 itens × 120 colunas**, com `Name`,
`GUICategory`, `ItemClass`, `ItemTier`, `DLCPackage`, `MaxStackSize` e o resto.

Ele **não** depende de alguém ter aberto um baú: lê a tabela, não os objetos
carregados. Um extrator que varre o mundo devolve uma lista curta que *parece*
completa — foi o que aconteceu na primeira tentativa deste projeto: 27 itens,
todos com Template ID zero, num mundo de 786.927 objetos.

Para transformar isso num catálogo de loja:

```bash
tools/montar_catalogo_loja.py itemtable-conan.json --max-por-categoria 12 -o itens.json
```

O script **diz o que deixou de fora e por quê** (itens de DLC, decoração,
linhas de teste), e a conta fecha com o total — corte silencioso vira "a loja
está incompleta e não sei por quê".

---

## O que este plugin protege, e como

### Ninguém gasta o que não tem, nem duas vezes

O débito é **um único UPDATE com a condição de saldo dentro dele**:

```sql
UPDATE carteira SET pontos = pontos - ? WHERE jogador = ? AND pontos >= ?
```

Quem decide é o banco, uma vez, sob a trava dele. Não há intervalo entre "ler o
saldo" e "gastar", porque não se lê antes de gastar. Se a linha não mudou, não
havia saldo — e isso é uma **resposta do banco**, não uma suposição.

Isso está medido. Oito clientes simultâneos, 400 tentativas de compra disputando
saldo para exatamente 20:

| implementação | passaram | saldo final |
|---|---|---|
| checagem fora do UPDATE (o padrão do ArkShop) | **26** | **−60** |
| condição dentro do UPDATE (este plugin) | **20** | **0** |

O teste roda com `testes/rodar.sh`, e há um `controle_positivo` que prova que o
teste **sabe reprovar**. Isso não é cerimônia: a primeira versão do teste
passou, e o controle positivo a reprovou — havia um mutex no próprio teste, e
ele apagava justamente a fresta que se queria medir. Com ele, até a
implementação defeituosa passava com nota cheia. Enquanto o controle positivo
não passar, o resto da bateria não significa nada.

Isso importa de verdade quando você aponta dois servidores para o mesmo MySQL:
lá não existe trava em comum entre os processos, e só o SQL segura.

### Nenhum comando de dinheiro sai cortado

Todo SQL passa por `MontarSql`, que **recusa a operação** se o comando não
couber no buffer, em vez de truncar em silêncio como o `snprintf` faz.

O motivo é específico: um corte no lugar errado produz um comando *válido* e
*diferente do pretendido*. Cortar entre `WHERE jogador='...'` e
`AND pontos >= N` deixa um UPDATE que o banco executa com prazer — **sem a
condição de saldo**. No crédito é pior: um corte antes do `WHERE` credita a
carteira de todo mundo.

Com o id de conta limitado a 64 caracteres isso não acontece hoje. Mas "hoje não
acontece" nunca foi garantia, e a proteção custa uma linha.

### Configuração quebrada não substitui a boa

Você edita o `config.json` às 21h, erra uma vírgula e manda `!shopreload`. O
plugin **recusa** e continua com a configuração anterior, dizendo o motivo no
chat.

Sem isso, a loja ficaria vazia no horário de pico — sem erro visível, porque
"zero itens" é um estado válido.

### Se a entrega falhar, o ponto volta

O débito acontece antes da entrega (é a única ordem segura). Se o jogo recusar
o item, o plugin **devolve** e registra o motivo no diário e no log.

### Todo movimento fica no diário

A tabela `diario` guarda cada crédito e débito com data, valor e motivo. Quando
um jogador disser *"comprei e não recebi"*, é ela que responde.

---

## O que este plugin NÃO resolve

Declarado de propósito, porque limite conhecido é melhor que surpresa.

### O servidor caindo entre o débito e a entrega

A ordem é: debita → entrega → se a entrega falhar, devolve. É a única ordem
segura (entregar antes de cobrar deixaria o item de graça se o débito falhasse).

Mas se o **servidor cair exatamente no meio** — depois do débito, antes da
entrega — o ponto sai e o item não chega, e não há nada rodando para devolver.

Resolver isso de verdade exigiria marcar a compra como pendente e reconciliar no
arranque seguinte; é uma máquina de estados que não está aqui. O que existe:

- a janela é de milissegundos (entre duas chamadas seguidas);
- o `diario` registra o débito com `compra:<item>`, então dá para ver o que
  aconteceu e devolver à mão:
  ```
  dar Fulano#1234 50 estorno da queda de 20/08
  ```

O ArkShop também não resolve isso.

### Entrega parcial quando o inventário enche no meio

`SpawnTemplateItem` recebe a quantidade inteira e responde sim ou não. Se o jogo
entregar parte e recusar o resto, o plugin vê "sim" e cobra tudo. Não há como
distinguir pelo retorno.

Mitigação: os itens são vendidos em quantidade que respeita o `MaxStackSize` da
tabela, então a pilha cabe num slot. Ainda assim, comprar com o inventário quase
cheio pode render menos do que o pago.

### Itens de DLC

O catálogo gerado exclui itens de DLC por padrão (`DLCPackage != None`), porque
quem não tem o DLC compra e não recebe. Se você habilitar com `--com-dlc`, essa
responsabilidade passa a ser sua — o plugin não tem como saber quais DLCs cada
jogador possui.

---

## O que está provado, e como

Este projeto não chama de pronto o que não foi visto funcionando. Tudo abaixo
foi medido — nada é "deve funcionar".

| peça | estado |
|---|---|
| carteira, débito atômico, devolução | **provado** — teste automatizado com controle positivo |
| leitura e recusa de `config.json` | **provado** — 8 casos, incluindo JSON válido com itens inválidos |
| roteamento de comando (`!shop` × `!shopreload`) | **provado** — 11 casos de roteamento |
| extração da ItemTable (9.121 itens) | **provado** no servidor, IDs conferidos contra fonte externa |
| fila `SHOP-COMANDOS` | **provado** — 10 casos, incluindo os que devem falhar |
| RCON **não** intercepta | **provado** — 5 hooks armados, nenhum disparou |
| `!pontos` respondendo | **provado com jogador real** (20/08/2026) |
| `!shop` desenhando a tela | **provado com jogador real** |
| `!comprar` entregando o item | **provado com jogador real** — 100 pedras no inventário |
| crédito por tempo | **provado com jogador real** — creditou durante o teste |

### O que o teste com jogador real achou, e que nenhum teste automatizado acharia

A primeira versão **não respondia nada** ao jogador. O hook do chat disparava,
lia o comando e cancelava a mensagem corretamente — e a resposta morria no
caminho, sem uma linha no log.

Duas causas, as duas invisíveis:

1. `Falar()` desistia em silêncio quando o nome do jogador não tinha sido lido —
   e `Identificar()` devolve sucesso com o nome vazio, desde que o Permission
   tenha resolvido o id.
2. Pior, era erro de desenho: a resposta procurava o jogador **pelo nome**, via
   `CheatManager`, tendo o controller dele na mão.

Hoje a resposta vai direto ao controller, tentando três caminhos em ordem
(`ClientHUDShowNotification` → `ClientMessage` → `PlayerMessage`) e registrando
qual funcionou. Na build 24784646, é o primeiro — o mesmo que o jogo usa para
seus próprios avisos.

*200 no curl não prova a tela.* Este defeito passou por bateria de testes,
revisão adversarial e revisão de boa-fé sem aparecer. Só apareceu quando alguém
digitou `!pontos` dentro do jogo.

---

## Licença

Mesma da Conan-Api. Você pode usar em quantos servidores quiser, inclusive em
servidor que cobra dos jogadores; e pode escrever e **vender** plugins seus em
cima da API. O que não pode é revender ou re-hospedar a própria API.
