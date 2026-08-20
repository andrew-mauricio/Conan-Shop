// Pontos — implementacao. O porque das decisoes esta no Pontos.h.
//
// DOIS BANCOS, UM COMPORTAMENTO
// -----------------------------
// SQLite e MySQL entram pelas mesmas quatro funcoes, e o codigo da loja nao
// sabe qual esta ligado. O que NAO e' negociavel entre os dois:
//
//   · o debito e' um UPDATE unico com a condicao de saldo dentro;
//   · "quantas linhas mudaram" e' lido do banco, nunca inferido;
//   · toda operacao que falha devolve falha — nenhuma devolve "deu certo"
//     quando o banco nao respondeu.
//
// O terceiro item parece obvio e e' onde plugins de loja costumam vazar: um
// `Executar()` que devolve void, ou um erro engolido, viram pontos que somem
// ou dobram sem deixar rastro.
#include "Pontos.h"

#include "terceiros/sqlite3/sqlite3.h"
#include "MySqlCliente.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <mutex>

namespace Shop
{
namespace
{
    // ── a trava ─────────────────────────────────────────────────────────────
    //
    // O credito por tempo roda na thread do jogo; a compra tambem. Hoje sao a
    // mesma thread, e por isso esta trava quase nunca disputa. Ela existe
    // porque "hoje sao a mesma" e' uma propriedade do agendador da API, nao
    // uma garantia desta classe — e o dia em que deixar de ser, o sintoma seria
    // saldo errado sob carga, que e' o defeito mais caro de diagnosticar.
    std::mutex          g_trava;

    bool                g_mysql = false;
    sqlite3*            g_db    = nullptr;
    Perm::MySqlCliente* g_my    = nullptr;
    std::string         g_erroUltimo;

    // Log opcional — quem chama instala. Sem isto, uma falha de banco morre em
    // silencio dentro do plugin.
    void (*g_log)(const char*) = nullptr;
    void Registrar(const char* fmt, ...)
    {
        if (!g_log) return;
        char linha[512];
        va_list a; va_start(a, fmt);
        std::vsnprintf(linha, sizeof(linha), fmt, a);
        va_end(a);
        g_log(linha);
    }

    // ── o esquema ───────────────────────────────────────────────────────────
    //
    // `pontos` com CHECK >= 0 no SQLite: se algum caminho futuro tentar deixar
    // o saldo negativo, o banco RECUSA em vez de gravar. E' a rede sob a rede —
    // a condicao no UPDATE ja' impede, e esta linha e' o que sobra se alguem
    // acrescentar um caminho novo e esquecer da condicao.
    //
    // O diario nao e' enfeite: quando um jogador disser "comprei e nao recebi",
    // ele e' a unica forma de saber o que aconteceu.
    const char* const ESQUEMA_SQLITE[] = {
        "CREATE TABLE IF NOT EXISTS carteira ("
        "  jogador TEXT PRIMARY KEY,"
        "  pontos  INTEGER NOT NULL DEFAULT 0 CHECK (pontos >= 0),"
        "  gasto   INTEGER NOT NULL DEFAULT 0,"
        "  visto   INTEGER NOT NULL DEFAULT 0);",
        "CREATE TABLE IF NOT EXISTS diario ("
        "  id      INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  quando  INTEGER NOT NULL,"
        "  jogador TEXT NOT NULL,"
        "  delta   INTEGER NOT NULL,"
        "  motivo  TEXT);",
        "CREATE INDEX IF NOT EXISTS diario_jogador ON diario(jogador, quando);",
        nullptr
    };

    const char* const ESQUEMA_MYSQL[] = {
        "CREATE TABLE IF NOT EXISTS carteira ("
        "  jogador VARCHAR(64) NOT NULL PRIMARY KEY,"
        "  pontos  BIGINT NOT NULL DEFAULT 0,"
        "  gasto   BIGINT NOT NULL DEFAULT 0,"
        "  visto   BIGINT NOT NULL DEFAULT 0"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;",
        "CREATE TABLE IF NOT EXISTS diario ("
        "  id      BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "  quando  BIGINT NOT NULL,"
        "  jogador VARCHAR(64) NOT NULL,"
        "  delta   BIGINT NOT NULL,"
        "  motivo  VARCHAR(190),"
        "  KEY diario_jogador (jogador, quando)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;",
        nullptr
    };

    int64_t Agora()
    {
        // time() basta: o diario e' para leitura humana, nao para ordenacao
        // com precisao de milissegundo.
        return int64_t(std::time(nullptr));
    }

    // ── montar SQL sem truncar em silencio ──────────────────────────────────
    //
    // POR QUE ISTO E' UMA FUNCAO, E NAO UM snprintf SOLTO EM CADA LUGAR
    //
    // snprintf trunca calado e devolve o tamanho que PRECISARIA. Num comando de
    // dinheiro, o corte mais caro possivel cai entre a tabela e a condicao:
    //
    //     UPDATE carteira SET pontos = pontos - 10 WHERE jogador='xxx
    //                                              ^ cortado aqui
    //
    // Se o corte deixar aspas abertas, o MySQL recusa — sorte. Se cair logo
    // DEPOIS do fecha-aspas e antes do `AND pontos >= N`, sai um UPDATE valido
    // SEM a condicao de saldo, e o banco executa com prazer. Pior ainda no
    // credito: um corte antes do WHERE credita a TABELA INTEIRA.
    //
    // Com o id da conta limitado a 64 caracteres isso nao acontece hoje — o
    // pior caso fica perto de 230 bytes num buffer de 512. Mas "hoje nao
    // acontece" nunca foi garantia: basta o id crescer ou alguem trocar a chave
    // da carteira, e o defeito volta sem uma linha de aviso.
    //
    // Devolve false quando nao coube. Quem chama TEM de tratar como falha e nao
    // executar nada.
    bool MontarSql(char* destino, size_t tam, const char* fmt, ...)
    {
        va_list a;
        va_start(a, fmt);
        const int n = std::vsnprintf(destino, tam, fmt, a);
        va_end(a);
        if (n >= 0 && size_t(n) < tam) return true;
        Registrar("[shop] comando SQL NAO coube (%d bytes, cabem %d). "
                  "Operacao recusada — nada foi alterado.", n, int(tam) - 1);
        if (tam) destino[0] = 0;
        return false;
    }

    // ── SQLite ──────────────────────────────────────────────────────────────

    bool SqliteExecutar(const char* sql)
    {
        char* err = nullptr;
        if (sqlite3_exec(g_db, sql, nullptr, nullptr, &err) == SQLITE_OK) return true;
        g_erroUltimo = err ? err : "erro sem mensagem do sqlite";
        sqlite3_free(err);
        return false;
    }

    bool AbrirSqlite(const ConfigBanco& cfg, std::string& erro)
    {
        if (cfg.caminhoLocal.empty()) { erro = "caminho do banco local vazio"; return false; }

        const int rc = sqlite3_open_v2(cfg.caminhoLocal.c_str(), &g_db,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        if (rc != SQLITE_OK)
        {
            char m[512];
            std::snprintf(m, sizeof(m),
                "nao consegui abrir o banco em '%s' (codigo %d: %s). "
                "Quase sempre e' disco cheio, pasta somente-leitura, ou o caminho nao existir.",
                cfg.caminhoLocal.c_str(), rc, g_db ? sqlite3_errmsg(g_db) : "sem handle");
            erro = m;
            if (g_db) { sqlite3_close(g_db); g_db = nullptr; }
            return false;
        }

        // busy_timeout: o Conan grava o mundo em SQLite tambem, e um disco
        // ocupado nao pode virar compra recusada. 5 s de espera antes de
        // desistir.
        sqlite3_busy_timeout(g_db, 5000);

        // WAL para o leitor nao bloquear o escritor. Quem apagar o .db precisa
        // apagar -wal e -shm junto, e isso esta dito no config.json.
        if (!SqliteExecutar("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;"))
        { erro = g_erroUltimo; return false; }

        for (int i = 0; ESQUEMA_SQLITE[i]; ++i)
            if (!SqliteExecutar(ESQUEMA_SQLITE[i])) { erro = g_erroUltimo; return false; }

        return true;
    }

    // ── MySQL ───────────────────────────────────────────────────────────────

    bool AbrirMysql(const ConfigBanco& cfg, std::string& erro)
    {
        // Estas recusas sao deliberadas. Cair num padrao aqui — 'root', ou um
        // nome de banco inventado — grava os pontos dos jogadores num lugar que
        // o dono nao escolheu e nao vai procurar.
        if (cfg.usuario.empty())
        { erro = "com banco mysql, mysql_usuario nao pode ficar vazio"; return false; }
        if (cfg.banco.empty())
        { erro = "com banco mysql, mysql_banco nao pode ficar vazio"; return false; }

        // Espaco na ponta nao aparece na tela e faz o valor parecer certo. Os
        // colchetes na mensagem sao o que torna o espaco visivel.
        auto temEspacoNaPonta = [](const std::string& s)
        { return !s.empty() && (std::isspace((unsigned char)s.front()) ||
                                std::isspace((unsigned char)s.back())); };
        for (auto par : { std::make_pair("mysql_host", &cfg.host),
                          std::make_pair("mysql_usuario", &cfg.usuario),
                          std::make_pair("mysql_banco", &cfg.banco) })
        {
            if (temEspacoNaPonta(*par.second))
            {
                char m[256];
                std::snprintf(m, sizeof(m),
                    "%s tem espaco no comeco ou no fim: [%s]. Copiar e colar traz espaco junto.",
                    par.first, par.second->c_str());
                erro = m;
                return false;
            }
        }

        g_my = new Perm::MySqlCliente();
        g_my->DefinirTempos(cfg.prazoConectarMs, cfg.prazoOperarMs);

        char err[512] = {0};
        if (!g_my->Conectar(cfg.host.c_str(), uint16_t(cfg.porta), cfg.usuario.c_str(),
                            cfg.senha.c_str(), cfg.banco.c_str(), err, sizeof(err)))
        {
            erro = err[0] ? err : "nao consegui conectar ao MySQL";
            delete g_my; g_my = nullptr;
            return false;
        }

        for (int i = 0; ESQUEMA_MYSQL[i]; ++i)
            if (!g_my->Executar(ESQUEMA_MYSQL[i], err, sizeof(err)))
            { erro = err[0] ? err : "falha ao criar as tabelas"; return false; }

        return true;
    }

    // Garante a linha do jogador. Sem isso, o UPDATE do debito nao encontra
    // nada e "sem saldo" e "jogador novo" ficariam indistinguiveis.
    bool GarantirJogador(const std::string& jogador)
    {
        if (g_mysql)
        {
            char sql[512], err[512];
            if (!MontarSql(sql, sizeof(sql),
                    "INSERT IGNORE INTO carteira (jogador, pontos, gasto, visto) VALUES (%s,0,0,%lld);",
                    Perm::MySqlCliente::Citar(jogador.c_str()).c_str(), (long long)Agora()))
                return false;
            return g_my->Executar(sql, err, sizeof(err));
        }
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(g_db,
                "INSERT OR IGNORE INTO carteira (jogador, pontos, gasto, visto) VALUES (?1,0,0,?2);",
                -1, &st, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(st, 1, jogador.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, Agora());
        const bool ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
        return ok;
    }

    void Anotar(const std::string& jogador, int64_t delta, const char* motivo)
    {
        // O diario nunca derruba a operacao: perder uma linha de historico e'
        // ruim, recusar um credito porque o historico falhou seria pior.
        if (g_mysql)
        {
            char sql[768], err[512];
            if (MontarSql(sql, sizeof(sql),
                    "INSERT INTO diario (quando, jogador, delta, motivo) VALUES (%lld,%s,%lld,%s);",
                    (long long)Agora(), Perm::MySqlCliente::Citar(jogador.c_str()).c_str(),
                    (long long)delta, Perm::MySqlCliente::Citar(motivo ? motivo : "").c_str()))
                g_my->Executar(sql, err, sizeof(err));
            return;
        }
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(g_db,
                "INSERT INTO diario (quando, jogador, delta, motivo) VALUES (?1,?2,?3,?4);",
                -1, &st, nullptr) != SQLITE_OK) return;
        sqlite3_bind_int64(st, 1, Agora());
        sqlite3_bind_text (st, 2, jogador.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 3, delta);
        sqlite3_bind_text (st, 4, motivo ? motivo : "", -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
}  // namespace

void DefinirLog(void (*log)(const char*)) { g_log = log; }

bool Abrir(const ConfigBanco& cfg, std::string& erro)
{
    std::lock_guard<std::mutex> t(g_trava);
    g_mysql = cfg.mysql;
    const bool ok = g_mysql ? AbrirMysql(cfg, erro) : AbrirSqlite(cfg, erro);
    if (ok)
        Registrar("[shop] banco %s pronto.", g_mysql ? "mysql" : "local (sqlite)");
    return ok;
}

void Fechar()
{
    std::lock_guard<std::mutex> t(g_trava);
    if (g_db) { sqlite3_close(g_db); g_db = nullptr; }
    if (g_my) { g_my->Desconectar(); delete g_my; g_my = nullptr; }
}

int64_t Saldo(const std::string& jogador)
{
    std::lock_guard<std::mutex> t(g_trava);
    if (jogador.empty()) return -1;

    if (g_mysql)
    {
        if (!g_my) return -1;
        struct Ctx { int64_t v; bool achou; } ctx{0, false};
        char sql[384], err[512];
        if (!MontarSql(sql, sizeof(sql), "SELECT pontos FROM carteira WHERE jogador=%s;",
                       Perm::MySqlCliente::Citar(jogador.c_str()).c_str()))
            return -1;
        const bool ok = g_my->Consultar(sql,
            [](void* c, int n, const char* const* v)
            {
                Ctx* x = static_cast<Ctx*>(c);
                if (n > 0 && v[0]) { x->v = std::strtoll(v[0], nullptr, 10); x->achou = true; }
            }, &ctx, err, sizeof(err));
        if (!ok) { Registrar("[shop] saldo: %s", err); return -1; }
        return ctx.achou ? ctx.v : 0;   // jogador ainda sem linha tem zero
    }

    if (!g_db) return -1;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(g_db, "SELECT pontos FROM carteira WHERE jogador=?1;",
                           -1, &st, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, jogador.c_str(), -1, SQLITE_TRANSIENT);
    int64_t v = 0;
    const int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) v = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) return -1;
    return v;
}

bool Creditar(const std::string& jogador, int64_t quanto, const char* motivo)
{
    if (jogador.empty() || quanto <= 0) return false;
    std::lock_guard<std::mutex> t(g_trava);
    if (!GarantirJogador(jogador)) return false;

    if (g_mysql)
    {
        char sql[384], err[512];
        // Um corte ANTES do WHERE creditaria a carteira de TODO MUNDO.
        if (!MontarSql(sql, sizeof(sql),
                "UPDATE carteira SET pontos = pontos + %lld WHERE jogador=%s;",
                (long long)quanto, Perm::MySqlCliente::Citar(jogador.c_str()).c_str()))
            return false;
        if (!g_my->Executar(sql, err, sizeof(err)))
        { Registrar("[shop] creditar: %s", err); return false; }
    }
    else
    {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(g_db,
                "UPDATE carteira SET pontos = pontos + ?1 WHERE jogador=?2;",
                -1, &st, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int64(st, 1, quanto);
        sqlite3_bind_text (st, 2, jogador.c_str(), -1, SQLITE_TRANSIENT);
        const bool ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
        if (!ok) return false;
    }

    Anotar(jogador, quanto, motivo);
    return true;
}

// ── o debito: a operacao que este arquivo existe para acertar ───────────────
Gasto Debitar(const std::string& jogador, int64_t quanto, const char* motivo)
{
    if (jogador.empty() || quanto <= 0) return Gasto::Erro;
    std::lock_guard<std::mutex> t(g_trava);
    if (!GarantirJogador(jogador)) return Gasto::Erro;

    if (g_mysql)
    {
        char sql[512], err[512];
        // A condicao de saldo vai DENTRO do UPDATE. Ver o cabecalho do .h.
        if (!MontarSql(sql, sizeof(sql),
                "UPDATE carteira SET pontos = pontos - %lld, gasto = gasto + %lld "
                "WHERE jogador=%s AND pontos >= %lld;",
                (long long)quanto, (long long)quanto,
                Perm::MySqlCliente::Citar(jogador.c_str()).c_str(), (long long)quanto))
            return Gasto::Erro;

        // O truncamento e' recusado dentro do MontarSql, acima — e ele diz no
        // log qual comando nao coube. O porque de isso ser fatal aqui esta na
        // definicao dele.
        if (!g_my->Executar(sql, err, sizeof(err)))
        { Registrar("[shop] debitar: %s", err); return Gasto::Erro; }

        // "Quantas linhas mudaram" vem do BANCO. Zero significa que a condicao
        // de saldo nao passou — e nao ha nada a desfazer, porque nada mudou.
        if (g_my->LinhasAfetadas() == 0) return Gasto::SemSaldo;
    }
    else
    {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(g_db,
                "UPDATE carteira SET pontos = pontos - ?1, gasto = gasto + ?1 "
                "WHERE jogador=?2 AND pontos >= ?1;",
                -1, &st, nullptr) != SQLITE_OK) return Gasto::Erro;
        sqlite3_bind_int64(st, 1, quanto);
        sqlite3_bind_text (st, 2, jogador.c_str(), -1, SQLITE_TRANSIENT);
        const bool ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
        if (!ok) return Gasto::Erro;
        if (sqlite3_changes(g_db) == 0) return Gasto::SemSaldo;
    }

    Anotar(jogador, -quanto, motivo);
    return Gasto::Ok;
}

bool Devolver(const std::string& jogador, int64_t quanto, const char* motivo)
{
    // Devolucao tambem abate o `gasto`, senao a estatistica de quanto o jogador
    // gastou passa a contar compras que nao aconteceram.
    if (jogador.empty() || quanto <= 0) return false;
    {
        std::lock_guard<std::mutex> t(g_trava);
        if (g_mysql && g_my)
        {
            char sql[384], err[512];
            if (MontarSql(sql, sizeof(sql),
                    "UPDATE carteira SET gasto = GREATEST(gasto - %lld, 0) WHERE jogador=%s;",
                    (long long)quanto, Perm::MySqlCliente::Citar(jogador.c_str()).c_str()))
                g_my->Executar(sql, err, sizeof(err));
        }
        else if (g_db)
        {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(g_db,
                    "UPDATE carteira SET gasto = MAX(gasto - ?1, 0) WHERE jogador=?2;",
                    -1, &st, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int64(st, 1, quanto);
                sqlite3_bind_text (st, 2, jogador.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(st);
                sqlite3_finalize(st);
            }
        }
    }
    return Creditar(jogador, quanto, motivo);
}

}  // namespace Shop
