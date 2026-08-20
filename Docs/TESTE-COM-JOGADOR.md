# Roteiro do teste com jogador real

Três coisas do ConanShop **não podem ser provadas sem alguém dentro do jogo**.
Elas estão escritas contra a assinatura que a reflexão desta build declara, e o
caminho *chat → jogador* já está provado no ar pelo ExemploJogador — mas neste
projeto isso não conta como pronto. *200 no curl não prova a tela.*

Faça na ordem abaixo: cada passo depende do anterior ter funcionado, e parar no
primeiro que falhar economiza a investigação.

Deixe o log aberto numa janela:

```bash
tail -f ~/conan/servidor/ConanSandbox/Binaries/Win64/Conan-Api/Logs/ConanApi.log | grep -i shop
```

---

## 1. `!pontos` — prova a identidade

Entre no servidor e digite no chat:

```
!pontos
```

**Esperado:** *"Voce tem 0 ponto(s)."*

| o que aconteceu | o que significa |
|---|---|
| respondeu com um número | ✅ a identidade funciona; siga |
| nada acontece, e o log diz *"nao consegui identificar quem digitou"* | o `ConanPermission.dll` não está carregado, ou o `id_do_controller` não resolveu |
| nada acontece e o log fica mudo | o hook do chat não pegou a mensagem — confira se `[shop] pronto` aparece no log |
| *"A loja esta fora do ar"* | o banco não respondeu; o erro estará no log logo acima |

---

## 2. Ganhar pontos — prova o timer

Fique online **5 minutos** (o intervalo padrão).

**Esperado:** *"Voce recebeu 5 ponto(s). Total: 5"*

Se você estiver no grupo `admin` do `permission.json`, são **25**, não 5 — o
plugin dá o maior valor entre os grupos do jogador.

Para não esperar, dê pontos a si mesmo por fora (o servidor não precisa parar):

```bash
echo "dar SEU-NOME#0000 500 teste" | sudo tee \
  ~/conan/servidor/ConanSandbox/Binaries/Win64/Conan-Api/SHOP-COMANDOS
```

e leia a resposta em `Conan-Api/SHOP-RESPOSTAS` (sai em até 3 s).

---

## 3. `!shop` — prova a TELA

```
!shop
```

**Esperado:** a caixa de mensagem do jogo (a mesma do "Esqueleto empalado")
abrindo com a lista, agrupada por categoria:

```
[recurso]
  Stone                  x100  10 pts   (stone)
  Wood                   x100  10 pts   (wood)
  ...

!comprar <id>   ·   pagina 1 de 10   ·   !shop 2 para a proxima
```

| o que aconteceu | o que significa |
|---|---|
| a caixa abriu com a lista | ✅ `ClientShowMessageBox` funciona |
| **nada aparece na tela** | a função existe mas não desenhou — teste `"usar_tela": false` no config para ver se o conteúdo sai pelo chat. Se sair, o problema é só a tela |
| a caixa abriu **vazia** | o texto não atravessou como FText — provável problema no `ConanApi::TextoRico` com texto longo |
| texto cortado no fim | reduza `itens_por_pagina` para 8 ou 6 |

Teste também `!shop 2` e `!shop 99` (este deve dizer que a página não existe).

> **Lembre:** o botão da caixa é do jogo e só fecha. Não vira "próxima página" —
> a paginação é por comando.

---

## 4. `!comprar` — prova a ENTREGA

O passo que mais importa. Compre o item mais barato:

```
!comprar stone
```

**Esperado:**
1. o jogo mostra a notificação de item recebido (o `ShowNotification` do
   `SpawnTemplateItem`);
2. **100 pedras aparecem no seu inventário**;
3. o chat responde *"Comprou Stone x100 por 10 ponto(s). Saldo: 490"*.

| o que aconteceu | o que significa |
|---|---|
| o item chegou no inventário | ✅ **a loja está completa** |
| *"Nao consegui entregar o item. Seus pontos foram devolvidos."* | veja o log: ele diz o motivo exato. Confira o saldo com `!pontos` — tem de ter voltado ao valor de antes |
| log diz *"SpawnTemplateItem nao respondeu nesta build"* | a função mudou de nome ou assinatura num patch |
| log diz *"o jogo recusou a entrega"* | inventário cheio, ou o TemplateId não produz item |
| o ponto foi cobrado e o item **não** veio, sem mensagem | **isto é o pior caso.** Anote e me avise: significa que a entrega devolveu sucesso sem entregar |

**Confira sempre o saldo depois**, com `!pontos`. Se a compra falhou, o saldo
tem de estar exatamente como antes.

---

## 5. `!shopreload` — prova a permissão

Edite qualquer preço no `config.json` e digite:

```
!shopreload
```

**Esperado:** *"Configuracao recarregada: 120 item(ns)."*

Depois, **quebre o arquivo de propósito** (apague uma chave `}`) e mande de
novo. Tem de responder que **NÃO** aplicou e que continua com a anterior — e
`!shop` tem de seguir funcionando. Esse é o comportamento que impede a loja de
sumir no horário de pico.

---

## 6. `!shopdar` — prova o comando de admin

```
!shopdar OutroJogador#1234 100
```

Só funciona para quem tem a permissão `shop.admin` (o grupo `admin` do
`permission.json` tem `*`, então já passa). Quem recebe é avisado no chat.

Peça a alguém sem permissão para tentar: tem de recusar.

---

## Se algo falhar

O log diz o motivo de cada passo, com `[shop]` no começo da linha. Copie a linha
inteira — ela traz o que a API respondeu, e não só que "não funcionou".

Os testes automatizados (`testes/rodar.sh`) cobrem carteira, config e
roteamento de comando, e passam. O que falha aqui é a fronteira com o jogo, que
é justamente o que nenhum teste automatizado alcança.
