// Pontos — a carteira de cada jogador, e a unica coisa deste plugin que
// PRECISA sobreviver a um desligamento.
//
// O QUE ESTE ARQUIVO PROTEGE
// --------------------------
// Ponto e' dinheiro. As duas coisas que nao podem acontecer sao:
//
//   1. o jogador gastar o que nao tem (saldo negativo)
//   2. o jogador gastar duas vezes o mesmo ponto (duas compras simultaneas
//      lendo o mesmo saldo)
//
// O ArkShop, que este plugin toma como referencia, protege as duas em C++:
//
//     if (points >= price && Points::SpendPoints(price, eos_id))
//
// e o SQL por baixo e' `UPDATE ... SET Points = Points - ? WHERE EosId = ?`.
// A checagem esta ANTES, no processo; o UPDATE nao repete a condicao. Entre o
// GetPoints e o UPDATE cabe outra compra do mesmo jogador — dois clientes, ou
// um clique duplo — e as duas passam. O saldo vai a negativo e ninguem ve.
//
// Aqui a condicao mora DENTRO do UPDATE:
//
//     UPDATE carteira SET pontos = pontos - ?  WHERE jogador = ? AND pontos >= ?
//
// O banco decide, uma vez, sob a trava dele. Se a linha nao foi alterada, nao
// havia saldo — e "nao alterou" e' resposta, nao suposicao: e' o que
// `Mudancas()` devolve. Duas compras simultaneas: a primeira altera 1 linha, a
// segunda altera 0 e e' recusada. Nao ha janela entre ler e gastar porque nao
// se le' antes de gastar.
#pragma once

#include <cstdint>
#include <string>

namespace Shop
{
    // Como o plugin fala com quem chama: sem excecao atravessando a fronteira
    // do jogo, e com o motivo separado do resultado.
    enum class Gasto
    {
        Ok,             // debitado
        SemSaldo,       // nao tinha o bastante — o UPDATE nao alterou nada
        Erro,           // o banco falhou; NADA foi debitado
    };

    struct ConfigBanco
    {
        bool        mysql = false;
        std::string caminhoLocal;      // ja' resolvido
        std::string host = "127.0.0.1";
        int         porta = 3306;
        std::string usuario, senha, banco;
        int         prazoConectarMs = 5000;
        int         prazoOperarMs   = 10000;
    };

    // Onde este modulo escreve o que deu errado. Sem isto, uma falha de banco
    // morre dentro do plugin e o dono do servidor ve' apenas "a loja nao
    // responde", sem uma linha dizendo por que.
    void DefinirLog(void (*log)(const char*));

    // Abre (e cria as tabelas na primeira vez). `erro` recebe o motivo em
    // portugues quando devolve false — o dono do servidor le' isso no log e
    // precisa saber o que fazer, nao um codigo.
    bool Abrir(const ConfigBanco& cfg, std::string& erro);
    void Fechar();

    // Saldo. -1 quando o banco nao respondeu: quem chama PRECISA distinguir
    // "tem zero" de "nao sei", porque mostrar "voce tem 0 pontos" para quem tem
    // 500 e' pior do que dizer que a loja esta fora do ar.
    int64_t Saldo(const std::string& jogador);

    // Credita. Devolve false se o banco falhou (nada foi creditado).
    bool Creditar(const std::string& jogador, int64_t quanto, const char* motivo);

    // Debita SE houver saldo — a decisao e' do banco, num comando so'.
    Gasto Debitar(const std::string& jogador, int64_t quanto, const char* motivo);

    // Para o caso de a entrega falhar depois do debito. E' um credito comum;
    // existe com nome proprio para aparecer no diario como devolucao e nao
    // como premio.
    bool Devolver(const std::string& jogador, int64_t quanto, const char* motivo);
}
