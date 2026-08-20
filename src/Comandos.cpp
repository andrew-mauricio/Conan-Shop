// Comandos — implementacao. O porque esta no Comandos.h.
#include "Comandos.h"
#include "Pontos.h"
#include "Config.h"

#include "Conan/ConanPluginApi.h"
#include "Conan/ConanPermission.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace Shop
{
// Vem do ConanShop.cpp: a tabela da API e a configuracao viva. Declarados aqui
// em vez de passados por parametro porque este modulo e' chamado pelo
// agendador, que nao carrega contexto.
extern const ConanApiTabela* ApiDaLoja();
extern const Config&         ConfigDaLoja();
extern bool                  RecarregarConfig(std::string& erro, size_t& quantos);
extern bool                  ResolverJogadorPorNome(const std::string& nome, std::string& id);

namespace
{
    std::string g_fila, g_respostas;

    void Aparar(std::string& s)
    {
        while (!s.empty() && (s.back()  == '\r' || s.back()  == '\n' ||
                              s.back()  == ' '  || s.back()  == '\t')) s.pop_back();
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        if (i) s.erase(0, i);
    }

    std::vector<std::string> Partir(const std::string& linha)
    {
        std::vector<std::string> p;
        std::string atual;
        for (char c : linha)
        {
            if (c == ' ' || c == '\t')
            {
                if (!atual.empty()) { p.push_back(atual); atual.clear(); }
                continue;
            }
            atual += c;
        }
        if (!atual.empty()) p.push_back(atual);
        return p;
    }

    // Aceita id de conta OU nome de quem esta online. O id e' o caminho certo
    // (nao muda); o nome existe porque e' o que o dono tem na mao quando olha o
    // `listplayers` do RCON.
    bool Resolver(const std::string& quem, std::string& id, std::string& porque)
    {
        // Um id de conta do Conan comeca com "A-" e nao tem "#". O nome de
        // exibicao tem "#" (Indio#76973). Tentar o id primeiro evita ir ao
        // mundo a toa.
        if (quem.find('#') == std::string::npos)
        {
            id = quem;
            return true;
        }
        if (ResolverJogadorPorNome(quem, id)) return true;
        porque = "jogador nao esta online (por nome so' funciona conectado; "
                 "use o id da conta para quem esta offline)";
        return false;
    }
}

void DefinirCaminhos(const std::string& raiz)
{
    g_fila      = raiz + "\\SHOP-COMANDOS";
    g_respostas = raiz + "\\SHOP-RESPOSTAS";
}

const std::string& CaminhoDaFila()      { return g_fila; }
const std::string& CaminhoDasRespostas(){ return g_respostas; }

void AtenderFila()
{
    if (g_fila.empty()) return;

    FILE* f = std::fopen(g_fila.c_str(), "rb");
    if (!f) return;                       // ninguem pediu nada: o caso comum

    std::string todo;
    {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) todo.append(buf, n);
    }
    std::fclose(f);

    // ── APAGAR ANTES DE EXECUTAR ────────────────────────────────────────────
    //
    // Se o servidor cair no meio de um "dar 500 pontos", a fila nao pode estar
    // la' na volta para dar de novo. Perder um comando e' recuperavel (o dono
    // repete); dar pontos duas vezes nao aparece para ninguem.
    std::remove(g_fila.c_str());

    const ConanApiTabela* api = ApiDaLoja();
    std::vector<std::string> respostas;
    int linhaN = 0;

    size_t ini = 0;
    while (ini <= todo.size())
    {
        size_t fim = todo.find('\n', ini);
        if (fim == std::string::npos) fim = todo.size();
        std::string linha = todo.substr(ini, fim - ini);
        ini = fim + 1;

        Aparar(linha);
        ++linhaN;
        if (linha.empty() || linha[0] == '#') continue;   // vazio e comentario

        const std::vector<std::string> p = Partir(linha);
        if (p.empty()) continue;

        std::string verbo = p[0];
        for (char& c : verbo) c = char(std::tolower((unsigned char)c));

        char resp[512];

        if (verbo == "recarregar")
        {
            std::string erro; size_t quantos = 0;
            if (RecarregarConfig(erro, quantos))
                std::snprintf(resp, sizeof(resp),
                              "linha %d: ok recarregar — %d item(ns)", linhaN, int(quantos));
            else
                std::snprintf(resp, sizeof(resp),
                              "linha %d: ERRO recarregar — %s (a configuracao ANTERIOR continua valendo)",
                              linhaN, erro.c_str());
            respostas.push_back(resp);
            continue;
        }

        if (p.size() < 2)
        {
            std::snprintf(resp, sizeof(resp),
                "linha %d: ERRO \"%s\" — falta o jogador. Use: dar|tirar|definir|saldo <jogador> [quantidade]",
                linhaN, verbo.c_str());
            respostas.push_back(resp);
            continue;
        }

        std::string id, porque;
        if (!Resolver(p[1], id, porque))
        {
            std::snprintf(resp, sizeof(resp), "linha %d: ERRO \"%s\" — %s",
                          linhaN, p[1].c_str(), porque.c_str());
            respostas.push_back(resp);
            continue;
        }

        if (verbo == "saldo")
        {
            const int64_t s = Saldo(id);
            if (s < 0) std::snprintf(resp, sizeof(resp),
                                     "linha %d: ERRO saldo %s — o banco nao respondeu",
                                     linhaN, id.c_str());
            else       std::snprintf(resp, sizeof(resp), "linha %d: ok saldo %s = %lld",
                                     linhaN, id.c_str(), (long long)s);
            respostas.push_back(resp);
            continue;
        }

        if (p.size() < 3)
        {
            std::snprintf(resp, sizeof(resp),
                          "linha %d: ERRO \"%s\" — falta a quantidade", linhaN, verbo.c_str());
            respostas.push_back(resp);
            continue;
        }

        char* fimNum = nullptr;
        const long long quanto = std::strtoll(p[2].c_str(), &fimNum, 10);
        if (!fimNum || *fimNum != 0 || quanto <= 0)
        {
            std::snprintf(resp, sizeof(resp),
                "linha %d: ERRO \"%s\" — quantidade tem de ser um numero inteiro maior que zero (veio \"%s\")",
                linhaN, verbo.c_str(), p[2].c_str());
            respostas.push_back(resp);
            continue;
        }

        // O motivo vai para o diario. Sem ele, uma auditoria de "de onde vieram
        // estes 5.000 pontos" nao tem resposta.
        std::string motivo = "admin";
        for (size_t i = 3; i < p.size(); ++i) { motivo += ' '; motivo += p[i]; }

        if (verbo == "dar")
        {
            if (Creditar(id, quanto, motivo.c_str()))
                std::snprintf(resp, sizeof(resp), "linha %d: ok dar %s +%lld (saldo %lld)",
                              linhaN, id.c_str(), quanto, (long long)Saldo(id));
            else
                std::snprintf(resp, sizeof(resp),
                              "linha %d: ERRO dar %s — o banco recusou. NADA foi creditado.",
                              linhaN, id.c_str());
        }
        else if (verbo == "tirar")
        {
            const Gasto g = Debitar(id, quanto, motivo.c_str());
            if (g == Gasto::Ok)
                std::snprintf(resp, sizeof(resp), "linha %d: ok tirar %s -%lld (saldo %lld)",
                              linhaN, id.c_str(), quanto, (long long)Saldo(id));
            else if (g == Gasto::SemSaldo)
                std::snprintf(resp, sizeof(resp),
                    "linha %d: RECUSADO tirar %s -%lld — saldo insuficiente (tem %lld). "
                    "NADA foi tirado; a carteira nunca fica negativa.",
                    linhaN, id.c_str(), quanto, (long long)Saldo(id));
            else
                std::snprintf(resp, sizeof(resp),
                              "linha %d: ERRO tirar %s — o banco recusou", linhaN, id.c_str());
        }
        else if (verbo == "definir")
        {
            // Definir e' credito ou debito conforme a diferenca. Nao ha um
            // "SET pontos = N" solto de proposito: passar por Creditar/Debitar
            // mantem o diario completo, e e' o diario que responde quando o
            // jogador reclamar.
            const int64_t atual = Saldo(id);
            if (atual < 0)
                std::snprintf(resp, sizeof(resp),
                              "linha %d: ERRO definir %s — nao consegui ler o saldo atual",
                              linhaN, id.c_str());
            else if (atual == quanto)
                std::snprintf(resp, sizeof(resp), "linha %d: ok definir %s = %lld (ja' estava)",
                              linhaN, id.c_str(), quanto);
            else if (atual < quanto)
            {
                const bool ok = Creditar(id, quanto - atual, motivo.c_str());
                std::snprintf(resp, sizeof(resp), "linha %d: %s definir %s = %lld (era %lld)",
                              linhaN, ok ? "ok" : "ERRO", id.c_str(), quanto, (long long)atual);
            }
            else
            {
                const Gasto g = Debitar(id, atual - quanto, motivo.c_str());
                std::snprintf(resp, sizeof(resp), "linha %d: %s definir %s = %lld (era %lld)",
                              linhaN, g == Gasto::Ok ? "ok" : "ERRO",
                              id.c_str(), quanto, (long long)atual);
            }
        }
        else
        {
            std::snprintf(resp, sizeof(resp),
                "linha %d: ERRO verbo \"%s\" nao existe. Use: dar, tirar, definir, saldo, recarregar",
                linhaN, verbo.c_str());
        }
        respostas.push_back(resp);
    }

    if (respostas.empty()) return;

    // ── as respostas ────────────────────────────────────────────────────────
    //
    // Sobrescreve, nao acumula: o arquivo responde ao ULTIMO lote, e quem
    // automatiza le' e sabe que aquilo e' a resposta do que acabou de mandar.
    // O historico permanente e' o diario, no banco.
    FILE* r = std::fopen(g_respostas.c_str(), "wb");
    if (r)
    {
        const std::time_t agora = std::time(nullptr);
        char quando[64];
        std::strftime(quando, sizeof(quando), "%Y-%m-%d %H:%M:%S", std::localtime(&agora));
        std::fprintf(r, "# ConanShop — respostas de %s\n", quando);
        for (const std::string& s : respostas) std::fprintf(r, "%s\n", s.c_str());
        std::fclose(r);
    }

    if (api)
        for (const std::string& s : respostas) api->Log("[shop] fila: %s", s.c_str());
}

}  // namespace Shop
