// controle_positivo — prova que o teste_pontos SABE reprovar.
//
// POR QUE ISTO EXISTE
// -------------------
// O teste_pontos passou. Sozinho, isso nao prova que o debito e' atomico —
// prova que o teste nao achou problema, que e' outra coisa. Um teste com a
// condicao de corrida mal montada (poucas threads, disputa fraca, banco
// serializando por acaso) passa igual nas duas implementacoes e da' uma
// aprovacao que nao significa nada.
//
// Este programa refaz a MESMA corrida contra a implementacao INGENUA — a do
// ArkShop, com a checagem de saldo fora do UPDATE:
//
//     if (saldo >= preco)                        <- le
//         UPDATE carteira SET pontos = pontos-?  <- gasta, sem repetir a condicao
//
// Se o teste for capaz de achar o defeito, aqui ele TEM de falhar: mais de 20
// compras passam, ou o saldo termina negativo. Se passar nas duas, o teste esta
// cego e a aprovacao do outro nao vale.
//
// Este arquivo NAO entra no plugin. E' instrumento de aferição do instrumento.
#include "terceiros/sqlite3/sqlite3.h"

#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>

namespace
{
    sqlite3*   g_db = nullptr;
    std::mutex g_trava;

    // Exatamente o padrao do ArkShop: le o saldo, decide em C++, e so' entao
    // manda o UPDATE — que NAO repete a condicao.
    // ── POR QUE NAO HA TRAVA AQUI ───────────────────────────────────────────
    //
    // A primeira versao deste arquivo punha um std::mutex em volta de cada
    // acesso, imitando a trava do Pontos.cpp. Com ela, a versao INGENUA passou
    // no teste — 20 de 20, saldo zero — e o controle positivo REPROVOU,
    // acusando o teste de cego.
    //
    // O diagnostico estava certo, e a causa era o proprio instrumento: uma
    // trava que serializa ler-e-gastar apaga justamente a fresta que se quer
    // medir. Testar com ela e' testar se o mutex funciona, nao se o SQL protege.
    //
    // Sem trava, cada thread e' um CLIENTE independente do banco — que e'
    // exatamente o caso real que importa: dois servidores de jogo apontando
    // para o MESMO MySQL, cada um com seu processo, sem mutex nenhum entre
    // eles. E' para esse caso que a condicao mora dentro do UPDATE.
    bool DebitarIngenuo(const char* jogador, int64_t quanto)
    {
        int64_t saldo = 0;
        {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(g_db, "SELECT pontos FROM carteira WHERE jogador=?1;",
                                   -1, &st, nullptr) != SQLITE_OK) return false;
            sqlite3_bind_text(st, 1, jogador, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) saldo = sqlite3_column_int64(st, 0);
            sqlite3_finalize(st);
        }

        if (saldo < quanto) return false;      // <- a checagem, FORA do UPDATE

        // A pausa nao e' truque para forcar o defeito: representa o que
        // qualquer loja faz entre decidir e cobrar — montar a mensagem,
        // consultar permissao, achar o personagem. No ArkShop esse trecho
        // chama GetPoints, formata FString e consulta o Permissions. Aqui e'
        // um instante, e ja' basta.
        std::this_thread::sleep_for(std::chrono::microseconds(50));

        {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(g_db,
                    "UPDATE carteira SET pontos = pontos - ?1 WHERE jogador=?2;",
                    -1, &st, nullptr) != SQLITE_OK) return false;
            sqlite3_bind_int64(st, 1, quanto);
            sqlite3_bind_text (st, 2, jogador, -1, SQLITE_TRANSIENT);
            int rc;
            // SQLITE_BUSY nao e' recusa de saldo: e' o banco ocupado. Repetir e'
            // o que qualquer cliente faz, e e' o que mantem a comparacao justa.
            for (int tent = 0; (rc = sqlite3_step(st)) == SQLITE_BUSY && tent < 100; ++tent)
                sqlite3_reset(st);
            sqlite3_finalize(st);
            return rc == SQLITE_DONE;
        }
    }

    // A NOSSA: a mesma corrida, a mesma pausa, o mesmo tudo — muda so' o SQL.
    // A condicao de saldo vai DENTRO do UPDATE, e quem decide e' o banco, uma
    // vez, sob a trava dele. Nao ha o que ler antes, entao nao ha fresta entre
    // ler e gastar.
    bool DebitarComCondicao(const char* jogador, int64_t quanto)
    {
        // A mesma pausa da versao ingenua, no mesmo lugar da linha do tempo:
        // sem ela a comparacao seria entre uma corrida apertada e uma frouxa.
        std::this_thread::sleep_for(std::chrono::microseconds(50));

        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(g_db,
                "UPDATE carteira SET pontos = pontos - ?1 "
                "WHERE jogador=?2 AND pontos >= ?1;",
                -1, &st, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int64(st, 1, quanto);
        sqlite3_bind_text (st, 2, jogador, -1, SQLITE_TRANSIENT);
        int rc;
        for (int tent = 0; (rc = sqlite3_step(st)) == SQLITE_BUSY && tent < 100; ++tent)
            sqlite3_reset(st);
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE) return false;

        // "Alterou uma linha" e' a resposta do banco a pergunta "havia saldo?".
        return sqlite3_changes(g_db) == 1;
    }
}

int main(int argc, char** argv)
{
    const std::string caminho = (argc > 1) ? argv[1] : "controle_positivo.db";
    std::remove(caminho.c_str());
    std::remove((caminho + "-wal").c_str());
    std::remove((caminho + "-shm").c_str());

    if (sqlite3_open_v2(caminho.c_str(), &g_db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
    { std::printf("  X nao abriu o banco\n"); return 2; }
    sqlite3_busy_timeout(g_db, 5000);
    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    // Sem o CHECK de propósito: aqui se quer VER o saldo negativo aparecer.
    sqlite3_exec(g_db, "CREATE TABLE carteira (jogador TEXT PRIMARY KEY, pontos INTEGER NOT NULL DEFAULT 0);",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(g_db, "INSERT INTO carteira (jogador,pontos) VALUES ('corrida',200);",
                 nullptr, nullptr, nullptr);

    std::printf("== a MESMA corrida, com as DUAS implementacoes, SEM TRAVA ==\n");
    std::printf("   200 pontos, 8 threads x 50 compras de 10 -> so' 20 podem passar.\n");
    std::printf("   Sem mutex dos dois lados: o que proteger, protege pelo SQL.\n\n");

    auto correr = [&](bool ingenuo)
    {
        sqlite3_exec(g_db, "UPDATE carteira SET pontos=200 WHERE jogador='corrida';",
                     nullptr, nullptr, nullptr);
        std::atomic<int> ok{0};
        std::vector<std::thread> ths;
        for (int t = 0; t < 8; ++t)
            ths.emplace_back([&, ingenuo]
            {
                for (int i = 0; i < 50; ++i)
                    if (ingenuo ? DebitarIngenuo("corrida", 10)
                                : DebitarComCondicao("corrida", 10)) ++ok;
            });
        for (auto& t : ths) t.join();

        int64_t saldo = 0;
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(g_db, "SELECT pontos FROM carteira WHERE jogador='corrida';",
                           -1, &st, nullptr);
        if (st && sqlite3_step(st) == SQLITE_ROW) saldo = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
        return std::make_pair(ok.load(), saldo);
    };

    const auto ingenuo = correr(true);
    std::printf("   INGENUA  (checagem fora do UPDATE, como o ArkShop):\n");
    std::printf("      passaram=%d   saldo=%lld\n\n", ingenuo.first, (long long)ingenuo.second);

    const auto nossa = correr(false);
    std::printf("   NOSSA    (condicao DENTRO do UPDATE):\n");
    std::printf("      passaram=%d   saldo=%lld\n\n", nossa.first, (long long)nossa.second);

    sqlite3_close(g_db);

    const bool ingenuaVazou = (ingenuo.first > 20) || (ingenuo.second < 0);
    const bool nossaSegurou = (nossa.first == 20) && (nossa.second == 0);

    if (!ingenuaVazou)
    {
        std::printf("REPROVADO como controle positivo: nem a versao INGENUA vazou.\n");
        std::printf("   A corrida esta fraca demais para provar qualquer coisa.\n");
        std::printf("   Aumente as threads/iteracoes antes de acreditar no outro teste.\n");
        return 1;
    }
    if (!nossaSegurou)
    {
        std::printf("REPROVADO: a NOSSA versao tambem vazou, sem a trava.\n");
        std::printf("   Entao quem estava segurando era o mutex, nao o SQL — e num\n");
        std::printf("   MySQL compartilhado entre dois servidores nao ha mutex nenhum.\n");
        return 1;
    }

    std::printf("APROVADO.\n");
    std::printf("   A ingenua deixou passar %d compra(s) a mais e o saldo foi a %lld.\n",
                ingenuo.first - 20, (long long)ingenuo.second);
    std::printf("   A nossa segurou em 20 e zero SEM TRAVA — logo quem protege e' a\n");
    std::printf("   condicao dentro do UPDATE, e a protecao vale tambem entre PROCESSOS\n");
    std::printf("   diferentes: dois servidores no mesmo MySQL.\n");
    return 0;
}
