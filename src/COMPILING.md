# Building from source

*Portuguese translation: [COMPILAR.pt.md](COMPILAR.pt.md)*

You don't need any of this to **use** the shop — the `.tar.gz` on
[Releases](../../releases) already carries the built DLL. This is for anyone who
wants to read it, change it, or check what's running on their server.

## What you need

A C++ compiler that produces a 64-bit Windows DLL. You don't need the Unreal
editor.

Most people doing this are on Windows: **Visual Studio 2017 or newer** (the free
Community edition is enough), with the "Desktop development with C++" workload.
Set *C/C++ → Code Generation → Runtime Library* to **/MT** and the platform to
**x64** — the SDK's `Docs/DEVELOPERS.md` explains why both matter.

If you'd rather build from Linux or WSL, mingw-w64 does the same job:

```bash
sudo apt install mingw-w64        # Debian/Ubuntu
```

And two pieces that **don't live in this repository**, because they belong to
another project:

| what | from where | why |
|---|---|---|
| `Conan/ConanPluginApi.h`, `ConanBase.h` | [Conan-Api-SDK](https://github.com/andrew-mauricio/Conan-Api-SDK) | the API the plugin calls |
| `Conan/ConanPermission.h` | same, in `Exemplos/Permission/include/` | player identity and VIP |
| `comum/MySqlCliente.*` and `comum/terceiros/sqlite3/` | same, in `Exemplos/comum/` | the databases: MySQL and SQLite |

> **Why they aren't here:** two copies of the same thing drift apart. The SQLite
> amalgamation alone is 9.4 MB and 261,000 lines; keeping it in two
> repositories would mean fixing a bug in one and forgetting the other. This
> repository holds what belongs to **the shop**; the rest comes from where it's
> maintained.

## The short path

Download the SDK, unpack it, and point at it:

```bash
# 1. the SDK (once)
gh release download -R andrew-mauricio/Conan-Api-SDK -p '*.tar.gz'
tar xzf Conan-Api-SDK-*.tar.gz

# 2. build, pointing at it
cd src
CONAN_SDK_INCLUDE=/path/to/sdk/include ./compilar.sh
```

The script looks in several places on its own before giving up; the variable is
only needed if it can't find them.

If a piece is missing, it **says which one and where it looked** — it doesn't
bail out with 400 lines of compiler errors.

## The simplest way: inside the SDK's tree

Copy this repository's `src` folder into the unpacked SDK's `Exemplos/`,
renaming it to `ConanShop`. There `compilar.sh` finds everything by itself,
because that's the layout it was written for:

```
Conan-Api-SDK/
   include/Conan/…          <- the SDK
   Exemplos/
      comum/                <- MySqlCliente + sqlite3
      Permission/include/…  <- ConanPermission.h
      ConanShop/            <- paste it here
         compilar.sh
         compilar.bat
```

On Windows, open the *x64 Native Tools Command Prompt for VS* from the Start
menu and run `compilar.bat`. On Linux or WSL:

```bash
cd Conan-Api-SDK/Exemplos/ConanShop
./compilar.sh
```

## Running the tests

```bash
./testes/rodar.sh
```

It needs `wine` (the plugin is Windows). Without wine the script **stops with
exit code 2** — which means *I didn't check*, and that isn't a pass.

The suite has four programs, and the first is a **positive control**: it
reimplements the debit the wrong way and demands that the test fail it. If it
passes, the instrument is blind and the rest is worth nothing.

## Checking that the published DLL came from this source

The build is **reproducible** — the timestamp was removed from the binary
(`-Wl,--no-insert-timestamp`). Two builds of the same source give the **same
md5**:

```bash
./compilar.sh && md5sum ConanShop.dll
```

Compare it with the hash published on the release. If they match, the DLL on
your server is exactly this code — not somebody's word for it.
