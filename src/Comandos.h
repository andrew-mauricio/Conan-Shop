// Comandos — administrar a loja DE FORA DO JOGO.
//
// POR QUE NAO E' UM COMANDO DE RCON
// ----------------------------------
// Porque nao pode ser, e isso foi MEDIDO em 20/08/2026, nao suposto:
//
//   · o RCON do Conan aceita uma lista fixa (`help` mostra: listplayers,
//     broadcast, con, exec, sql, KickPlayer...). Nao ha registro de comando
//     novo — a AsaApi tem AddRconCommand, o Conan nao expoe equivalente;
//   · a esperanca seguinte era pegar carona: `broadcast <texto>` chama
//     ConanCheatManager::BroadcastMessage, que E' UFunction e portanto
//     hookavel. Foi testado com o plugin ProvaDeRcon, com os cinco candidatos
//     enganchados ao mesmo tempo. O servidor respondeu "Message has been
//     broadcast." e NENHUM hook disparou: o caminho do RCON e' nativo e nao
//     passa pela reflexao;
//   · e `exec` sem argumento derruba o servidor inteiro (access violation).
//
// Entao a porta de fora e' um ARQUIVO, que e' o que toda automacao ja' sabe
// escrever: script de shell, painel web, tarefa agendada, FTP, SSH.
//
// COMO FUNCIONA
// -------------
// O dono escreve linhas em  Conan-Api/SHOP-COMANDOS  e o plugin, no proximo
// despertar (a cada 3 s), executa e RESPONDE em  Conan-Api/SHOP-RESPOSTAS.
//
//     dar        <jogador> <quantidade> [motivo]
//     tirar      <jogador> <quantidade> [motivo]
//     definir    <jogador> <quantidade> [motivo]
//     saldo      <jogador>
//     grupo      <jogador> <grupo> [dias]     <- VIP, pelo Permission
//     tirargrupo <jogador> <grupo>
//     recarregar
//
// `grupo` e `tirargrupo` chamam o Permission, e valem para QUALQUER grupo do
// permission.json — nao so' os que dao pontos. Existem aqui porque o Permission
// nao tem porta de fora: quem quisesse dar VIP por script precisaria de um
// plugin so' para chamar a ABI dele, ou de mexer no banco a mao (com WAL e
// cache em memoria, e' pedir para corromper). O `dias` opcional e' como se
// vende VIP de verdade: `grupo Fulano#1 vip 30`.
//
// O <jogador> pode ser o id da conta (o que o Permission usa) ou o nome de
// exibicao de quem esta online — resolvido na hora. Nome so' funciona com o
// jogador conectado, e a resposta diz isso em vez de errar calado.
//
// AS DUAS COISAS QUE ESTE DESENHO PROTEGE
// ----------------------------------------
// 1. O arquivo e' APAGADO antes de executar. Se o servidor cair no meio, o
//    comando nao roda duas vezes na volta — dar pontos duas vezes e' pior que
//    nao dar, porque ninguem reclama e o dono nao descobre.
// 2. Toda linha gera uma linha de resposta, inclusive as que falharam. Uma fila
//    silenciosa e' uma fila em que o dono nao sabe se funcionou.
#pragma once

#include <string>

namespace Shop
{
    // Chamada pelo agendador. Le a fila, executa, escreve as respostas.
    // Nao faz nada (e nao custa nada) quando o arquivo nao existe.
    void AtenderFila();

    // O caminho dos dois arquivos, para o log dizer onde eles ficam.
    const std::string& CaminhoDaFila();
    const std::string& CaminhoDasRespostas();
    void DefinirCaminhos(const std::string& raiz);
}
