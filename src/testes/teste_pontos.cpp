// teste_pontos — prova que a carteira nao deixa gastar o que nao existe,
// nem gastar duas vezes o mesmo ponto.
//
// POR QUE ESTE TESTE EXISTE
// -------------------------
// A regra "nao pode ficar negativo" e' facil de escrever e facil de acreditar
// que esta valendo. O ArkShop, que serviu de referencia, acredita nela: a
// checagem esta em C++, antes do UPDATE, e o UPDATE nao a repete. Sob duas
// compras simultaneas isso vaza — e o vazamento nao aparece em uso normal, so'
// no clique duplo e no jogador com dois clientes.
//
// Um teste que apenas credita e debita em sequencia NAO acharia esse defeito:
// passaria igual nas duas implementacoes. Por isso o caso 4 usa threads de
// verdade, disputando a mesma carteira.
//
// RODA SOB WINE porque e' o ambiente de verdade: o plugin roda no servidor
// Windows do Conan, sob Wine nesta VPS. Testar em Linux nativo provaria a
// logica num lugar onde ela nao vai rodar.
#include "../Pontos.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

static int g_falhas = 0;

static void Conferir(bool ok, const char* oque, const char* detalhe = "")
{
    std::printf("  [%s] %s%s%s\n", ok ? "ok" : " X", oque,
                detalhe[0] ? " — " : "", detalhe);
    if (!ok) ++g_falhas;
}

static void LogDoBanco(const char* linha) { std::printf("      (banco) %s\n", linha); }

int main(int argc, char** argv)
{
    const std::string caminho = (argc > 1) ? argv[1] : "teste_pontos.db";
    // Comeca limpo. O -wal e o -shm tambem: sem apagar os tres, o dado do teste
    // anterior RESSUSCITA e o teste passa medindo a corrida passada.
    std::remove(caminho.c_str());
    std::remove((caminho + "-wal").c_str());
    std::remove((caminho + "-shm").c_str());

    Shop::DefinirLog(&LogDoBanco);

    Shop::ConfigBanco cfg;
    cfg.mysql = false;
    cfg.caminhoLocal = caminho;

    std::string erro;
    if (!Shop::Abrir(cfg, erro))
    {
        std::printf("  X nao abriu o banco: %s\n", erro.c_str());
        return 1;
    }
    std::printf("\n== 1. carteira nova ==\n");
    const std::string J = "jogador-de-teste";
    Conferir(Shop::Saldo(J) == 0, "quem nunca jogou tem saldo zero");

    std::printf("\n== 2. creditar e gastar ==\n");
    Conferir(Shop::Creditar(J, 100, "teste"), "creditou 100");
    Conferir(Shop::Saldo(J) == 100, "saldo virou 100");
    Conferir(Shop::Debitar(J, 30, "teste") == Shop::Gasto::Ok, "gastou 30");
    Conferir(Shop::Saldo(J) == 70, "sobrou 70");

    std::printf("\n== 3. gastar o que nao tem ==\n");
    const Shop::Gasto g = Shop::Debitar(J, 1000, "teste");
    Conferir(g == Shop::Gasto::SemSaldo, "recusou 1000 com saldo 70");
    Conferir(Shop::Saldo(J) == 70, "e NAO mexeu no saldo",
             ("saldo=" + std::to_string(Shop::Saldo(J))).c_str());

    std::printf("\n== 4. A CORRIDA: 8 threads x 50 compras de 10, com saldo para 20 ==\n");
    // Saldo exato para 20 compras de 10. As 400 tentativas disputam as mesmas
    // 20 vagas; se a condicao nao estivesse dentro do UPDATE, mais de 20
    // passariam e o saldo terminaria negativo.
    const std::string C = "corrida";
    Shop::Creditar(C, 200, "preparo");
    Conferir(Shop::Saldo(C) == 200, "saldo inicial 200");

    std::atomic<int> ok{0}, semSaldo{0}, erros{0};
    std::vector<std::thread> ths;
    for (int t = 0; t < 8; ++t)
        ths.emplace_back([&]
        {
            for (int i = 0; i < 50; ++i)
            {
                switch (Shop::Debitar(C, 10, "corrida"))
                {
                    case Shop::Gasto::Ok:       ++ok; break;
                    case Shop::Gasto::SemSaldo: ++semSaldo; break;
                    default:                    ++erros; break;
                }
            }
        });
    for (auto& t : ths) t.join();

    const int64_t sobrou = Shop::Saldo(C);
    std::printf("      passaram=%d  recusadas=%d  erros=%d  saldo=%lld\n",
                ok.load(), semSaldo.load(), erros.load(), (long long)sobrou);

    Conferir(erros.load() == 0, "nenhuma operacao devolveu erro");
    Conferir(ok.load() == 20, "exatamente 20 compras passaram",
             ("passaram " + std::to_string(ok.load())).c_str());
    Conferir(sobrou == 0, "o saldo terminou em zero",
             ("saldo=" + std::to_string(sobrou)).c_str());
    Conferir(sobrou >= 0, "e NUNCA ficou negativo");

    std::printf("\n== 5. devolucao ==\n");
    Conferir(Shop::Devolver(C, 10, "entrega falhou"), "devolveu 10");
    Conferir(Shop::Saldo(C) == 10, "saldo voltou a 10");

    std::printf("\n== 6. o que nao pode passar ==\n");
    Conferir(Shop::Debitar(C, 0, "zero")   == Shop::Gasto::Erro, "debitar 0 e' recusado");
    Conferir(Shop::Debitar(C, -5, "menos") == Shop::Gasto::Erro, "debitar negativo e' recusado");
    Conferir(!Shop::Creditar(C, -5, "menos"), "creditar negativo e' recusado");
    Conferir(Shop::Saldo(C) == 10, "e o saldo continua 10 depois de tudo isso");

    Shop::Fechar();

    std::printf("\n%s  (%d falha(s))\n",
                g_falhas ? "REPROVADO" : "APROVADO", g_falhas);
    return g_falhas ? 1 : 0;
}
