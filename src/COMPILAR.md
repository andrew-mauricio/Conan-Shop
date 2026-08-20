# Compilar do fonte

Você não precisa disto para **usar** a loja — o `.tar.gz` dos
[Releases](../../releases) já vem com o DLL pronto. Isto é para quem quer ler,
alterar ou conferir o que está rodando no servidor.

## O que você precisa

**Só o compilador cruzado.** Nada de Visual Studio, nada do editor da Unreal.

```bash
sudo apt install mingw-w64        # Debian/Ubuntu
```

E dois pedaços que **não moram neste repositório**, porque são de outro projeto:

| o quê | de onde | por quê |
|---|---|---|
| `Conan/ConanPluginApi.h`, `ConanBase.h` | [Conan-Api-SDK](https://github.com/andrew-mauricio/Conan-Api-SDK) | a API que o plugin chama |
| `Conan/ConanPermission.h` | idem, em `Exemplos/Permission/include/` | identidade do jogador e VIP |
| `comum/MySqlCliente.*` e `comum/terceiros/sqlite3/` | idem, em `Exemplos/comum/` | banco: MySQL e SQLite |

> **Por que não estão aqui:** duas cópias da mesma coisa divergem. O SQLite
> amalgamado sozinho tem 9,4 MB e 261 mil linhas; mantê-lo em dois repositórios
> significaria consertar um bug em um e esquecer o outro. Este repositório tem o
> que é **da loja**; o resto vem de onde ele é mantido.

## O caminho curto

Baixe o SDK, descompacte e aponte:

```bash
# 1. o SDK (uma vez)
gh release download -R andrew-mauricio/Conan-Api-SDK -p '*.tar.gz'
tar xzf Conan-Api-SDK-*.tar.gz

# 2. compile, apontando para ele
cd src
CONAN_SDK_INCLUDE=/caminho/do/sdk/include ./compilar.sh
```

O script procura sozinho em vários lugares antes de desistir; a variável só é
necessária se ele não achar.

Se faltar alguma peça, ele **diz qual e onde procurou** — não sai com erro de
compilador de 400 linhas.

## O jeito mais simples: dentro da árvore do SDK

Copie a pasta `src` deste repositório para dentro de `Exemplos/` do SDK
descompactado, renomeando para `ConanShop`. Ali o `compilar.sh` acha tudo
sozinho, porque é o layout para o qual ele foi escrito:

```
Conan-Api-SDK/
   include/Conan/…          <- o SDK
   Exemplos/
      comum/                <- MySqlCliente + sqlite3
      Permission/include/…  <- ConanPermission.h
      ConanShop/            <- cole aqui
         compilar.sh
```

```bash
cd Conan-Api-SDK/Exemplos/ConanShop
./compilar.sh
```

## Rodar os testes

```bash
./testes/rodar.sh
```

Precisa de `wine` (o plugin é Windows). Sem wine, o script **para com código 2**
— que quer dizer *não conferi*, e não é aprovação.

A bateria tem quatro programas, e o primeiro é um **controle positivo**: ele
reimplementa o débito do jeito errado e exige que o teste o reprove. Se ele
passar, o instrumento está cego e o resto não vale nada.

## Conferir que o DLL publicado veio deste fonte

O build é **reproduzível** — o carimbo de hora foi removido do binário
(`-Wl,--no-insert-timestamp`). Duas compilações do mesmo fonte dão o **mesmo
md5**:

```bash
./compilar.sh && md5sum ConanShop.dll
```

Compare com o hash publicado no release. Se bater, o DLL que está no seu
servidor é exatamente este código — e não a palavra de alguém.
