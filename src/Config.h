// Config — o config.json, lido e VALIDADO antes de valer.
//
// A REGRA QUE ESTE ARQUIVO EXISTE PARA GARANTIR
// ----------------------------------------------
// Uma configuracao so' substitui a anterior se estiver INTEIRA e correta. Nunca
// pela metade.
//
// O modo de falha que isso evita e' concreto: o dono edita o config as 21h,
// erra uma virgula, manda !shopreload, e a loja fica vazia no horario de pico —
// sem erro visivel, porque "0 itens" e' um estado valido. Aqui, config quebrada
// e' RECUSADA e a anterior continua valendo; o dono le' o motivo no chat e
// conserta com a loja funcionando.
//
// POR QUE O JSON E' LIDO PELO SQLITE
// -----------------------------------
// O mesmo motivo do Permission: o json1 do SQLite ja' esta no processo, e
// escrever um parser de JSON a mao para ler arquivo que o dono edita a mao e'
// codigo novo no caminho mais exposto do plugin. json_valid() antes de tudo:
// extrair de JSON quebrado devolve NULL em silencio, e configuracao vazia com
// cara de configuracao lida e' pior que erro.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

#include "Pontos.h"

namespace Shop
{
    struct Item
    {
        std::string chave;        // o que o jogador digita: !comprar pedra
        std::string nome;         // o que ele le' na lista
        std::string categoria;
        std::string permissao;    // vazio = qualquer um pode
        int32_t     templateId = 0;
        int32_t     quantidade = 1;
        int64_t     preco = 0;
    };

    struct Config
    {
        ConfigBanco banco;

        // pontos
        bool     pontosLigados = true;
        int      pontosMinutos = 5;
        bool     pontosSomar   = false;
        bool     pontosAvisar  = true;
        std::map<std::string, int64_t> pontosPorGrupo;

        // loja
        int         itensPorPagina = 12;
        bool        usarTela = true;
        std::string tituloDaTela = "Loja";
        std::string contexto = "ConanShop";

        // comandos
        std::string cmdLoja = "!shop", cmdComprar = "!comprar", cmdPontos = "!pontos",
                    cmdAjuda = "!shopajuda", cmdRecarregar = "!shopreload",
                    cmdDar = "!shopdar";

        // permissoes
        std::string permAdmin = "shop.admin", permComprar;

        // mensagens, por chave
        std::map<std::string, std::string> msg;

        // itens, na ordem do arquivo (a ordem da lista e' a que o dono escreveu)
        std::vector<Item> itens;

        const Item* Achar(const std::string& chave) const;
        const std::string& Msg(const char* chave, const char* padrao) const;
    };

    // Le e VALIDA. Devolve false sem tocar em `destino` se algo estiver errado —
    // e ai `erro` traz uma frase que o dono do servidor consegue agir sobre,
    // nao um codigo.
    bool LerConfig(const char* caminho, Config& destino, std::string& erro);
}
