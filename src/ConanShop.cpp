// ConanShop — loja por pontos para Conan Exiles, no espirito do ArkShop.
//
// O QUE ELE FAZ
// -------------
//   · credita pontos a quem esta online, por tempo, com valor por grupo (VIP)
//   · !shop        mostra a lista, paginada, na tela do jogo
//   · !comprar id  debita e entrega o item no inventario
//   · !pontos      mostra o saldo
//   · !shopreload  rele o config.json (so' para quem tem shop.admin)
//
// O QUE FOI FEITO DIFERENTE DO ArkShop, E POR QUE
// ------------------------------------------------
// 1. O DEBITO E' ATOMICO. O ArkShop faz `if (pontos >= preco && Gastar(preco))`
//    e o SQL por baixo e' `UPDATE ... SET Points = Points - ?` sem condicao.
//    Entre ler e gastar cabe outra compra do mesmo jogador. Aqui a condicao vai
//    dentro do UPDATE e quem decide e' o banco (ver Pontos.h).
//
// 2. A CHAVE DE VENDA E' O TemplateId, NAO A BLUEPRINT. No ARK vale "uma
//    blueprint = um item". No Conan nao: Stone (10001) e centenas de outros
//    itens simples compartilham a classe nativa /Script/ConanSandbox.GameItem.
//    Uma loja modelada como a do ARK entregaria o item errado — e funcionando,
//    sem erro nenhum no log.
//
// 3. CONFIG QUEBRADA NAO SUBSTITUI A BOA. Ver Config.h.
//
// O QUE AINDA NAO ESTA PROVADO COM JOGADOR REAL
// ----------------------------------------------
// A entrega (SpawnTemplateItem) e a tela (ClientShowMessageBox) foram escritas
// contra a assinatura que a reflexao desta build declara, e o caminho
// chat->jogador ja' esta provado no ar pelo ExemploJogador. Mas nenhuma das
// duas foi vista funcionando com um jogador de verdade dentro do jogo — e
// neste projeto isso nao conta como pronto. Ver o README.
#include "Conan/ConanPluginApi.h"
#include "Conan/ConanBase.h"
#include "Conan/ConanPermission.h"

#include "Config.h"
#include "Pontos.h"
#include "Comandos.h"
#include "Texto.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

static const ConanApiTabela* g_api = nullptr;
static Shop::Config          g_cfg;
static std::string           g_caminhoConfig;

// ── ler um ponteiro de membro, pelo NOME ────────────────────────────────────
//
// O mesmo padrao do ExemploJogador, e pelo mesmo motivo: gravar 0x308 funciona
// hoje e le' o campo vizinho depois do proximo patch da Funcom, sem erro e sem
// log — so' com dado errado.
static void* MembroPonteiro(void* obj, const char* nome)
{
    if (!obj) return nullptr;
    const int32_t off = g_api->OffsetDoMembro(obj, nome);
    if (off < 0) return nullptr;
    void* p = nullptr;
    if (g_api->LerMembro(obj, uint32_t(off), &p, sizeof(p)) <= 0) return nullptr;
    if (!p || !g_api->Legivel(p, 8)) return nullptr;
    return p;
}

// ── quem e' este jogador ────────────────────────────────────────────────────
//
// A identidade vem do Permission, que ja' resolveu essa pergunta e a provou com
// jogador real (MasterAccountId). Resolver de novo aqui seria uma segunda
// verdade sobre a mesma coisa — e as duas divergiriam no dia em que a chave
// mudasse.
struct Jogador
{
    std::string id;       // a chave da carteira
    std::string nome;     // para falar com ele no chat
    void*       controller = nullptr;
};

static bool Identificar(void* controller, Jogador& j)
{
    if (!controller) return false;
    j.controller = controller;

    if (void* ps = MembroPonteiro(controller, "PlayerState"))
    {
        const int32_t off = g_api->OffsetDoMembro(ps, "PlayerNamePrivate");
        if (off >= 0)
        {
            char n[128] = {0};
            if (g_api->LerTextoDoJogo(ps, uint32_t(off), n, sizeof(n)) > 0) j.nome = n;
        }
    }

    const ConanPermApi* perm = ConanPermObter();
    if (perm && perm->id_do_controller)
    {
        char id[CONAN_PERM_MAX_ID] = {0};
        if (perm->id_do_controller(controller, id, sizeof(id)) > 0 && id[0])
        {
            j.id = id;
            // Identidade PARCIAL: o id resolveu e o nome nao. Isto era o
            // suficiente para a loja funcionar por dentro e nao responder nada
            // ao jogador, porque o caminho antigo de mensagem procurava a
            // pessoa PELO NOME. Hoje a resposta vai pelo controller e nao
            // depende disto — mas continua valendo dizer, uma vez, porque nome
            // vazio tambem estraga a lista de online e as mensagens de admin.
            if (j.nome.empty())
            {
                static bool avisou = false;
                if (!avisou)
                {
                    avisou = true;
                    g_api->Log("[shop] AVISO: li o id do jogador (%s) mas nao o NOME "
                               "(PlayerState.PlayerNamePrivate). A loja funciona; "
                               "mensagens que dependem do nome, nao.", j.id.c_str());
                }
            }
            return true;
        }
    }

    // Sem Permission nao ha' identidade estavel, e uma carteira presa ao NOME
    // do jogador seria pior que nenhuma: o jogador troca o nome de exibicao e
    // os pontos dele somem, sem erro e sem volta. Melhor recusar e dizer.
    return false;
}

// ── responder ao jogador ────────────────────────────────────────────────────
//
// A PRIMEIRA VERSAO DESTA FUNCAO ERA UMA LINHA, E FALHAVA EM SILENCIO:
//
//     if (j.nome.empty() || texto.empty()) return;
//     g_api->MensagemParaJogador(j.nome.c_str(), texto.c_str());
//
// Duas coisas erradas, as duas invisiveis. Primeira: se o nome nao tivesse sido
// lido, ela desistia sem dizer nada — e `Identificar` devolve true com o nome
// VAZIO, desde que o Permission tenha resolvido o id. Segunda: o retorno de
// `MensagemParaJogador` era ignorado, entao uma falha dele tambem sumia.
//
// Medido com jogador real em 20/08/2026: o Andrew digitou `!pontos` tres vezes,
// o hook DISPAROU e CANCELOU a mensagem as tres — e nada apareceu na tela dele.
// O log nao tinha uma linha sobre isso, porque nao havia nenhuma para ter.
//
// E ha um erro de desenho por tras dos dois: procurar o jogador PELO NOME
// quando o controller dele esta na mao. `MensagemParaJogador` percorre o mundo
// atras de um CheatManager e chama `PlayerMessage(nome, texto)` — um caminho
// longo, que depende de um objeto que pode nem existir, para entregar algo a
// quem ja' esta identificado.
//
// Agora vai direto ao controller, e tenta os caminhos em ordem de preferencia.
// O primeiro que a build tiver, ganha; e o log diz QUAL pegou, uma vez — assim
// a proxima build que mexer nisso aparece no log em vez de emudecer a loja.
static void Falar(const Jogador& j, const std::string& texto)
{
    if (texto.empty()) return;
    if (!j.controller)
    {
        g_api->Log("[shop] nao respondi a %s: sem controller.",
                   j.nome.empty() ? j.id.c_str() : j.nome.c_str());
        return;
    }

    // 0 = ainda nao sei · 1..3 = o caminho que funcionou · -1 = nenhum
    static int caminho = 0;

    auto tentar = [&](int qual) -> bool
    {
        switch (qual)
        {
        case 1:
            // A notificacao padrao do HUD: e' o que o proprio jogo usa para
            // avisar o jogador, entao aparece onde ele ja' olha.
            ConanApi::Call<void>(j.controller, "ClientHUDShowNotification",
                                 ConanApi::TextoRico(texto.c_str()),
                                 bool(true),    // positive
                                 bool(false));  // bPlaySound: sem som, e' resposta de comando
            return g_api->UltimaChamadaExecutou() != 0;
        case 2:
            // Mensagem de sistema do PlayerController da Unreal.
            ConanApi::Call<void>(j.controller, "ClientMessage",
                                 ConanApi::Texto(texto.c_str()),
                                 ConanApi::Nome("Event"),
                                 float(8.0f));
            return g_api->UltimaChamadaExecutou() != 0;
        case 3:
            // O caminho antigo, pelo CheatManager. Precisa do NOME, entao so'
            // vale se ele foi lido.
            if (j.nome.empty()) return false;
            return g_api->MensagemParaJogador(j.nome.c_str(), texto.c_str()) != 0;
        }
        return false;
    };

    if (caminho > 0 && tentar(caminho)) return;

    for (int i = 1; i <= 3; ++i)
    {
        if (i == caminho) continue;          // ja' tentei acima
        if (!tentar(i)) continue;
        if (caminho != i)
        {
            static const char* const NOMES[] = { "", "ClientHUDShowNotification",
                                                 "ClientMessage", "MensagemParaJogador" };
            g_api->Log("[shop] respondendo ao jogador por %s", NOMES[i]);
            caminho = i;
        }
        return;
    }

    // Nenhum funcionou. Isto TEM de aparecer: uma loja que nao consegue
    // responder e' uma loja que o jogador acha quebrada, e sem esta linha o
    // dono do servidor nao teria por onde comecar.
    if (caminho != -1)
    {
        caminho = -1;
        g_api->Log("[shop] NAO CONSIGO responder ao jogador: nenhum dos tres "
                   "caminhos funcionou nesta build (ClientHUDShowNotification, "
                   "ClientMessage, PlayerMessage). Jogador: %s / nome=\"%s\"",
                   j.id.c_str(), j.nome.c_str());
    }
}

// Definidas no fim do arquivo (precisam de `Identificar`, que precisa de
// `Jogador`), e usadas no meio. A declaracao antecipada evita reordenar o
// arquivo inteiro so' para agradar o compilador.
namespace Shop
{
    bool ResolverJogadorPorNome(const std::string& nome, std::string& id);
}

// ── a tela ──────────────────────────────────────────────────────────────────
//
// ClientShowMessageBox(Title: FText, Message: FText) — a caixa com botao que o
// jogo ja' usa para avisos. O botao e' do JOGO: ele fecha, e nao ha como
// transforma-lo em "proxima pagina". Por isso a paginacao e' por comando.
static void MostrarNaTela(const Jogador& j, const std::string& titulo,
                          const std::string& corpo)
{
    if (!j.controller) return;
    ConanApi::Call<void>(j.controller, "ClientShowMessageBox",
                         ConanApi::TextoRico(titulo.c_str()),
                         ConanApi::TextoRico(corpo.c_str()));
}

// ── a entrega ───────────────────────────────────────────────────────────────
//
// ConanCharacter::SpawnTemplateItem(TemplateId, Context: FName, quantity,
//                                   durabilityPercentage, durability,
//                                   ShowNotification) -> bool
//
// O `Context` e' um FName, e e' por causa dele que o SDK ganhou ConanApi::Nome
// em 20/08/2026: sem essa ponte, esta chamada — e portanto qualquer loja — era
// impossivel de escrever com o SDK publico.
static bool Entregar(const Jogador& j, const Shop::Item& item, std::string& porque)
{
    void* corpo = MembroPonteiro(j.controller, "Character");
    if (!corpo) corpo = MembroPonteiro(j.controller, "Pawn");
    if (!corpo)
    {
        porque = "sem personagem no mundo";
        return false;
    }

    const bool ok = ConanApi::Call<bool>(
        corpo, "SpawnTemplateItem",
        int32_t(item.templateId),
        ConanApi::Nome(g_cfg.contexto.c_str()),
        int32_t(item.quantidade),
        float(1.0f),      // durabilityPercentage: item novo
        float(0.0f),      // durability: 0 = usa a do proprio item
        bool(true));      // ShowNotification: o jogo avisa "voce recebeu"

    // Duas perguntas diferentes, e as duas importam:
    //   UltimaChamadaExecutou() -> a funcao EXISTE e rodou nesta build
    //   ok                      -> a funcao rodou e disse que conseguiu
    // Sem a primeira, uma funcao renomeada num patch devolveria false e o log
    // culparia o inventario cheio.
    if (!g_api->UltimaChamadaExecutou())
    {
        porque = "SpawnTemplateItem nao respondeu nesta build do jogo";
        return false;
    }
    if (!ok) { porque = "o jogo recusou a entrega (inventario cheio?)"; return false; }
    return true;
}

// ── os comandos ─────────────────────────────────────────────────────────────

static bool PodeComprar(const Jogador& j, const Shop::Item& item)
{
    const ConanPermApi* perm = ConanPermObter();
    if (!perm || !perm->tem) return true;   // sem Permission, sem restricao
    if (!g_cfg.permComprar.empty() &&
        perm->tem(j.id.c_str(), g_cfg.permComprar.c_str()) != 1) return false;
    if (!item.permissao.empty() &&
        perm->tem(j.id.c_str(), item.permissao.c_str()) != 1) return false;
    return true;
}

static void ComandoPontos(const Jogador& j)
{
    const int64_t s = Shop::Saldo(j.id);
    if (s < 0)
    {
        // "voce tem 0 pontos" para quem tem 500 e' pior que dizer que caiu.
        Falar(j, "A loja esta fora do ar (o banco nao respondeu). Avise um administrador.");
        return;
    }
    Falar(j, Shop::Formatar(g_cfg.Msg("seus_pontos", "Voce tem {0} ponto(s)."),
                      { std::to_string(s) }));
}

static void ComandoLoja(const Jogador& j, int pagina)
{
    // Monta so' o que o jogador PODE comprar: listar o que ele nao pode e
    // recusar depois e' fazer a pessoa tentar para descobrir.
    std::vector<const Shop::Item*> visiveis;
    for (const Shop::Item& i : g_cfg.itens)
        if (PodeComprar(j, i)) visiveis.push_back(&i);

    const int porPagina = g_cfg.itensPorPagina;
    const int paginas = int((visiveis.size() + size_t(porPagina) - 1) / size_t(porPagina));
    if (pagina < 1) pagina = 1;

    if (visiveis.empty() || pagina > paginas)
    {
        Falar(j, Shop::Formatar(g_cfg.Msg("pagina_vazia", "Nao ha pagina {0}. A loja tem {1} pagina(s)."),
                          { std::to_string(pagina), std::to_string(paginas) }));
        return;
    }

    const size_t inicio = size_t(pagina - 1) * size_t(porPagina);
    const size_t fim = std::min(inicio + size_t(porPagina), visiveis.size());

    std::string corpo;
    std::string categoriaAtual;
    for (size_t i = inicio; i < fim; ++i)
    {
        const Shop::Item* it = visiveis[i];
        if (it->categoria != categoriaAtual)
        {
            categoriaAtual = it->categoria;
            corpo += "\n[" + categoriaAtual + "]\n";
        }
        char linha[256];
        std::snprintf(linha, sizeof(linha), "  %-22s x%-4d %lld pts   (%s)\n",
                      it->nome.substr(0, 22).c_str(), it->quantidade,
                      (long long)it->preco, it->chave.c_str());
        corpo += linha;
    }

    corpo += "\n";
    corpo += Shop::Formatar(g_cfg.Msg("rodape_da_lista",
                                "!comprar <id>   ·   pagina {0} de {1}   ·   !shop {2} para a proxima"),
                      { std::to_string(pagina), std::to_string(paginas),
                        std::to_string(pagina < paginas ? pagina + 1 : 1) });

    if (g_cfg.usarTela) MostrarNaTela(j, g_cfg.tituloDaTela, corpo);
    else                Falar(j, corpo);
}

static void ComandoComprar(const Jogador& j, const std::string& chave, int quantas)
{
    if (chave.empty())
    {
        Falar(j, g_cfg.Msg("uso_comprar", "Use: !comprar <id> [quantidade]"));
        return;
    }
    const Shop::Item* item = g_cfg.Achar(chave);
    if (!item || !PodeComprar(j, *item))
    {
        Falar(j, Shop::Formatar(g_cfg.Msg("id_desconhecido",
                                    "Nao existe item com id \"{0}\". Veja a lista com !shop"),
                          { chave }));
        return;
    }
    if (quantas < 1) quantas = 1;
    if (quantas > 100) quantas = 100;    // teto de sanidade

    const int64_t custo = item->preco * quantas;

    // O personagem e' conferido ANTES do debito. Debitar e descobrir que nao ha'
    // onde entregar obriga a devolver — e toda devolucao e' uma chance a mais de
    // algo dar errado no meio.
    void* corpo = MembroPonteiro(j.controller, "Character");
    if (!corpo) corpo = MembroPonteiro(j.controller, "Pawn");
    if (!corpo)
    {
        Falar(j, g_cfg.Msg("sem_personagem",
                           "Voce precisa estar com o personagem no mundo para comprar."));
        return;
    }

    const Shop::Gasto r = Shop::Debitar(j.id, custo, ("compra:" + item->chave).c_str());
    if (r == Shop::Gasto::SemSaldo)
    {
        const int64_t s = Shop::Saldo(j.id);
        Falar(j, Shop::Formatar(g_cfg.Msg("sem_pontos", "Pontos insuficientes: custa {0} e voce tem {1}."),
                          { std::to_string(custo), std::to_string(s < 0 ? 0 : s) }));
        return;
    }
    if (r == Shop::Gasto::Erro)
    {
        Falar(j, "A loja esta fora do ar (o banco nao respondeu). NADA foi cobrado.");
        return;
    }

    // Debitado. A partir daqui, qualquer falha DEVOLVE.
    std::string porque;
    Shop::Item pedido = *item;
    pedido.quantidade = item->quantidade * quantas;

    if (!Entregar(j, pedido, porque))
    {
        Shop::Devolver(j.id, custo, ("devolucao:" + item->chave + " (" + porque + ")").c_str());
        g_api->Log("[shop] entrega falhou para %s (%s): %s — %lld ponto(s) devolvidos",
                   j.nome.c_str(), item->chave.c_str(), porque.c_str(), (long long)custo);
        Falar(j, g_cfg.Msg("entrega_falhou",
                           "Nao consegui entregar o item. Seus pontos foram devolvidos."));
        return;
    }

    const int64_t saldo = Shop::Saldo(j.id);
    Falar(j, Shop::Formatar(g_cfg.Msg("comprou", "Comprou {0} x{1} por {2} ponto(s). Saldo: {3}"),
                      { item->nome, std::to_string(pedido.quantidade),
                        std::to_string(custo), std::to_string(saldo < 0 ? 0 : saldo) }));
    g_api->Log("[shop] %s comprou %s x%d por %lld",
               j.nome.c_str(), item->chave.c_str(), pedido.quantidade, (long long)custo);
}

// ── !shopdar <jogador> <qtd> — o admin dando pontos de dentro do jogo ───────
//
// Existe porque a fila por arquivo serve a automacao, e nao a quem esta jogando
// e quer premiar alguem na hora. Protegido pela mesma permissao do reload.
static bool EhAdmin(const Jogador& j)
{
    const ConanPermApi* perm = ConanPermObter();
    if (!perm || !perm->tem) return false;    // sem Permission, ninguem e' admin
    if (g_cfg.permAdmin.empty()) return false;
    return perm->tem(j.id.c_str(), g_cfg.permAdmin.c_str()) == 1;
}

static void ComandoDar(const Jogador& j, const std::string& resto)
{
    if (!EhAdmin(j))
    {
        Falar(j, g_cfg.Msg("sem_permissao", "Voce nao tem permissao para isso."));
        return;
    }

    // "<nome> <qtd>" — o nome pode ter espaco? Nao: o nome do Conan e'
    // "Indio#76973", sem espaco. Partir no ULTIMO espaco cobre os dois casos
    // sem precisar de aspas.
    const size_t esp = resto.rfind(' ');
    if (esp == std::string::npos || esp + 1 >= resto.size())
    {
        Falar(j, "Use: " + g_cfg.cmdDar + " <jogador> <quantidade>");
        return;
    }
    const std::string quem = resto.substr(0, esp);
    const int64_t quanto = std::atoll(resto.substr(esp + 1).c_str());
    if (quanto <= 0)
    {
        Falar(j, "A quantidade precisa ser um numero maior que zero.");
        return;
    }

    std::string id;
    if (!Shop::ResolverJogadorPorNome(quem, id))
    {
        // Pode ser que ele tenha digitado o id direto.
        if (quem.find('#') == std::string::npos) id = quem;
        else
        {
            Falar(j, "\"" + quem + "\" nao esta online. Para quem esta offline, "
                     "use o id da conta.");
            return;
        }
    }

    if (!Shop::Creditar(id, quanto, ("admin " + j.nome).c_str()))
    {
        Falar(j, "O banco recusou. NADA foi creditado.");
        return;
    }
    Falar(j, "Dei " + std::to_string(quanto) + " ponto(s) a " + quem +
             " (saldo " + std::to_string(Shop::Saldo(id)) + ").");
    g_api->Log("[shop] %s deu %lld ponto(s) a %s",
               j.nome.c_str(), (long long)quanto, id.c_str());

    // Avisa quem recebeu, se estiver online — receber pontos em silencio deixa
    // o jogador achando que o admin esqueceu.
    Jogador alvo;
    void* pcs[128];
    const int n = g_api->FindObjects("ConanPlayerController", pcs, 128, 1);
    for (int i = 0; i < n; ++i)
        if (Identificar(pcs[i], alvo) && alvo.id == id)
        {
            Falar(alvo, Shop::Formatar(g_cfg.Msg("recebeu_pontos",
                                           "Voce recebeu {0} ponto(s). Total: {1}"),
                                 { std::to_string(quanto),
                                   std::to_string(Shop::Saldo(id)) }));
            break;
        }
}

static void ComandoRecarregar(const Jogador& j)
{
    const ConanPermApi* perm = ConanPermObter();
    if (perm && perm->tem && !g_cfg.permAdmin.empty() &&
        perm->tem(j.id.c_str(), g_cfg.permAdmin.c_str()) != 1)
    {
        Falar(j, g_cfg.Msg("sem_permissao", "Voce nao tem permissao para isso."));
        return;
    }

    Shop::Config nova;
    std::string erro;
    if (!Shop::LerConfig(g_caminhoConfig.c_str(), nova, erro))
    {
        // A configuracao ANTERIOR continua valendo. Isso e' o ponto.
        Falar(j, Shop::Formatar(g_cfg.Msg("recarregar_falhou",
                                    "O arquivo tem erro e NAO foi aplicado. "
                                    "Continuo com a configuracao anterior. Motivo: {0}"),
                          { erro }));
        g_api->Log("[shop] !shopreload RECUSADO: %s", erro.c_str());
        return;
    }

    // O banco NAO e' reaberto aqui de proposito: trocar de banco com jogadores
    // online trocaria as carteiras no meio de uma compra. Mudanca de banco pede
    // reinicio, e isso esta dito no chat em vez de acontecer calado.
    const bool bancoMudou = (nova.banco.mysql != g_cfg.banco.mysql);
    const size_t quantos = nova.itens.size();
    g_cfg = std::move(nova);

    Falar(j, Shop::Formatar(g_cfg.Msg("recarregado", "Configuracao recarregada: {0} item(ns)."),
                      { std::to_string(quantos) }));
    if (bancoMudou)
        Falar(j, "Atencao: banco.tipo mudou, mas trocar de banco exige reiniciar o servidor. "
                 "Continuo no banco anterior.");

    auto aviso = g_cfg.msg.find("_aviso_itens_recusados");
    if (aviso != g_cfg.msg.end())
        Falar(j, "Aviso: " + aviso->second + " item(ns) do arquivo foram recusados "
                 "(template_id ausente ou zero, preco negativo, ou chave repetida).");

    g_api->Log("[shop] configuracao recarregada por %s: %d itens",
               j.nome.c_str(), int(quantos));
}

// ── o chat ──────────────────────────────────────────────────────────────────

// ChatRpcData::Message fica em 0x068 nesta build. Este e' o UNICO offset cru do
// plugin, e ele esta aqui porque o parametro e' uma struct de RPC que a
// reflexao nao decompoe. Foi medido, esta provado no ar pelo ExemploJogador, e
// tem quem confira: se a build mudar, a API se recusa a carregar.
static const uint32_t OFF_MENSAGEM_DO_CHAT = 0x068;

extern "C" ConanAcao AoFalar(ConanChamada* c)
{
    char texto[256] = {0};
    if (g_api->LerTextoDoJogo(c->Parms, OFF_MENSAGEM_DO_CHAT, texto, sizeof(texto)) <= 0)
        return CONAN_CONTINUAR;
    if (texto[0] != '!' && texto[0] != '/') return CONAN_CONTINUAR;

    const std::string msg = texto;
    std::string resto;

    // A identificacao so' acontece se a mensagem PARECER um comando nosso —
    // ela custa uma consulta ao Permission, e o chat de um servidor cheio passa
    // por aqui a cada linha digitada.
    const bool nosso =
        msg.compare(0, g_cfg.cmdLoja.size(), g_cfg.cmdLoja) == 0 ||
        msg.compare(0, g_cfg.cmdComprar.size(), g_cfg.cmdComprar) == 0 ||
        msg.compare(0, g_cfg.cmdPontos.size(), g_cfg.cmdPontos) == 0 ||
        msg.compare(0, g_cfg.cmdAjuda.size(), g_cfg.cmdAjuda) == 0 ||
        msg.compare(0, g_cfg.cmdRecarregar.size(), g_cfg.cmdRecarregar) == 0 ||
        msg.compare(0, g_cfg.cmdDar.size(), g_cfg.cmdDar) == 0;
    if (!nosso) return CONAN_CONTINUAR;

    Jogador j;
    if (!Identificar(c->Obj, j))
    {
        g_api->Log("[shop] nao consegui identificar quem digitou \"%s\". "
                   "O ConanPermission.dll esta instalado?", texto);
        return CONAN_CONTINUAR;
    }

    // A ordem importa: o mais especifico primeiro, senao "!shopreload" seria
    // engolido por "!shop".
    if (Shop::Prefixo(msg, g_cfg.cmdRecarregar, resto)) { ComandoRecarregar(j); return CONAN_CANCELAR; }
    if (Shop::Prefixo(msg, g_cfg.cmdDar, resto))        { ComandoDar(j, resto);  return CONAN_CANCELAR; }
    if (Shop::Prefixo(msg, g_cfg.cmdComprar, resto))
    {
        std::string chave = resto, qtd;
        const size_t esp = resto.find(' ');
        if (esp != std::string::npos) { chave = resto.substr(0, esp); qtd = resto.substr(esp + 1); }
        ComandoComprar(j, chave, qtd.empty() ? 1 : std::atoi(qtd.c_str()));
        return CONAN_CANCELAR;
    }
    if (Shop::Prefixo(msg, g_cfg.cmdPontos, resto)) { ComandoPontos(j); return CONAN_CANCELAR; }
    if (Shop::Prefixo(msg, g_cfg.cmdAjuda, resto))
    {
        Falar(j, g_cfg.cmdLoja + " [pagina]  ·  " + g_cfg.cmdComprar +
                 " <id> [qtd]  ·  " + g_cfg.cmdPontos);
        return CONAN_CANCELAR;
    }
    if (Shop::Prefixo(msg, g_cfg.cmdLoja, resto))
    {
        ComandoLoja(j, resto.empty() ? 1 : std::atoi(resto.c_str()));
        return CONAN_CANCELAR;
    }
    return CONAN_CONTINUAR;
}

// ── o credito por tempo ─────────────────────────────────────────────────────
//
// Um unico agendamento varre quem esta online, em vez de um temporizador por
// jogador. Com 30 jogadores sao 30 tarefas contra uma — e a lista de online e'
// lida do mundo, entao ninguem fica recebendo pontos depois de sair.
static void CreditarOnline(void*)
{
    if (!g_cfg.pontosLigados || g_cfg.pontosPorGrupo.empty()) return;

    void* pcs[128];
    const int n = g_api->FindObjects("ConanPlayerController", pcs, 128, /*filhas=*/1);
    if (n <= 0) return;

    const ConanPermApi* perm = ConanPermObter();

    for (int i = 0; i < n; ++i)
    {
        Jogador j;
        if (!Identificar(pcs[i], j)) continue;

        // Quanto este jogador ganha: o maior valor entre os grupos dele, ou a
        // soma, conforme "somar". Sem Permission, cai no grupo "default" — e o
        // servidor sem Permission ainda funciona, so' sem VIP.
        int64_t quanto = 0;
        bool achouGrupo = false;

        if (perm && perm->no_grupo)
        {
            for (const auto& par : g_cfg.pontosPorGrupo)
            {
                if (perm->no_grupo(j.id.c_str(), par.first.c_str()) != 1) continue;
                achouGrupo = true;
                if (g_cfg.pontosSomar) quanto += par.second;
                else if (par.second > quanto) quanto = par.second;
            }
        }
        if (!achouGrupo)
        {
            auto d = g_cfg.pontosPorGrupo.find("default");
            if (d != g_cfg.pontosPorGrupo.end()) quanto = d->second;
        }
        if (quanto <= 0) continue;

        if (!Shop::Creditar(j.id, quanto, "tempo online")) continue;

        if (g_cfg.pontosAvisar)
        {
            const int64_t s = Shop::Saldo(j.id);
            Falar(j, Shop::Formatar(g_cfg.Msg("recebeu_pontos",
                                        "Voce recebeu {0} ponto(s). Total: {1}"),
                              { std::to_string(quanto), std::to_string(s < 0 ? 0 : s) }));
        }
    }
}

static void LogDoBanco(const char* linha) { if (g_api) g_api->Log("%s", linha); }

// ── o que o modulo de comandos precisa da loja ──────────────────────────────
//
// Tres funcoes, e nada mais. A fila administra pontos e recarrega config; nao
// precisa (nem deve) alcancar o resto do plugin.
namespace Shop
{
    const ConanApiTabela* ApiDaLoja()   { return g_api; }
    const Config&         ConfigDaLoja(){ return g_cfg; }

    bool RecarregarConfig(std::string& erro, size_t& quantos)
    {
        Config nova;
        if (!LerConfig(g_caminhoConfig.c_str(), nova, erro)) return false;
        quantos = nova.itens.size();
        g_cfg = std::move(nova);
        return true;
    }

    // O dono tem o NOME na mao (e' o que o `listplayers` do RCON mostra); o
    // banco guarda o ID da conta. Esta e' a ponte, e ela so' funciona com o
    // jogador ONLINE — o que a resposta da fila diz, em vez de falhar calado.
    bool ResolverJogadorPorNome(const std::string& nome, std::string& id)
    {
        void* pcs[128];
        const int n = g_api->FindObjects("ConanPlayerController", pcs, 128, 1);
        for (int i = 0; i < n; ++i)
        {
            Jogador j;
            if (!Identificar(pcs[i], j)) continue;
            if (j.nome == nome) { id = j.id; return true; }
        }
        return false;
    }
}

static void AtenderFilaDeComandos(void*) { Shop::AtenderFila(); }

extern "C" __declspec(dllexport)
void ConanPluginCarregar(const ConanApiTabela* api)
{
    if (!api || api->tamanho < sizeof(ConanApiTabela)) return;
    g_api = api;
    ConanApi::UsarTabela(api);

    g_api->Log("=====================================================");
    g_api->Log(" ConanShop — loja por pontos");
    g_api->Log("=====================================================");

    g_caminhoConfig = g_api->CaminhoConfig("ConanShop");
    std::string erro;
    if (!Shop::LerConfig(g_caminhoConfig.c_str(), g_cfg, erro))
    {
        // Nao subir e' proposital. Uma loja com configuracao ruim cobra pontos
        // e nao entrega — pior que loja nenhuma.
        g_api->Log("[shop] NAO SUBI: %s", erro.c_str());
        g_api->Log("[shop] arquivo: %s", g_caminhoConfig.c_str());
        return;
    }
    g_api->Log("[shop] configuracao: %d item(ns), %d grupo(s) de pontos",
               int(g_cfg.itens.size()), int(g_cfg.pontosPorGrupo.size()));

    Shop::DefinirLog(&LogDoBanco);
    if (g_cfg.banco.caminhoLocal.empty())
        g_cfg.banco.caminhoLocal = g_api->CaminhoDados("ConanShop", "conanshop.db");

    if (!Shop::Abrir(g_cfg.banco, erro))
    {
        g_api->Log("[shop] NAO SUBI: o banco nao abriu — %s", erro.c_str());
        return;
    }

    if (g_api->HookProcessEvent("ServerSendChatMessage", AoFalar, nullptr, 100) == 0)
    {
        g_api->Log("[shop] NAO SUBI: nao consegui enganchar o chat. Sem chat nao ha' loja.");
        Shop::Fechar();
        return;
    }

    if (g_cfg.pontosLigados)
    {
        const uint32_t seg = uint32_t(g_cfg.pontosMinutos) * 60u;
        g_api->AgendarNaThreadDoJogo(CreditarOnline, seg, nullptr, /*repetir=*/1);
        g_api->Log("[shop] pontos: a cada %d minuto(s)", g_cfg.pontosMinutos);
    }

    // ── a porta de fora do jogo ─────────────────────────────────────────────
    //
    // O RCON do Conan nao aceita comando de plugin (medido: ver Comandos.h).
    // Esta fila e' o caminho para painel web, script e SSH darem pontos.
    if (const char* raiz = g_api->CaminhoRaiz())
    {
        Shop::DefinirCaminhos(raiz);
        g_api->AgendarNaThreadDoJogo(AtenderFilaDeComandos, 3, nullptr, /*repetir=*/1);
        g_api->Log("[shop] comandos de fora: escreva em %s", Shop::CaminhoDaFila().c_str());
        g_api->Log("[shop]    dar|tirar|definir <jogador> <qtd> [motivo]  ·  saldo <jogador>  ·  recarregar");
        g_api->Log("[shop]    a resposta sai em %s", Shop::CaminhoDasRespostas().c_str());
    }

    const ConanPermApi* perm = ConanPermObter();
    g_api->Log("[shop] Permission: %s", perm ? "presente (VIP e identidade ok)"
                                             : "AUSENTE — sem VIP e sem identidade estavel");
    g_api->Log("[shop] pronto: %s  ·  %s  ·  %s",
               g_cfg.cmdLoja.c_str(), g_cfg.cmdComprar.c_str(), g_cfg.cmdPontos.c_str());
    g_api->Log("=====================================================");
}

extern "C" __declspec(dllexport)
void ConanPluginDescarregar(void)
{
    Shop::Fechar();
}
