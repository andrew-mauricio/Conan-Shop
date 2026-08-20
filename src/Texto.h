// Texto — as duas funções que mexem no que o jogador digita e no que ele lê.
//
// POR QUE ELAS MORAM NUM HEADER PRÓPRIO
// --------------------------------------
// Estavam dentro do ConanShop.cpp, como `static`, e portanto FORA do alcance de
// qualquer teste. As duas são pequenas e parecem óbvias — que é exatamente o
// perfil do código que ninguém testa e que quebra em silêncio:
//
//   · `Prefixo` decide qual comando atende. Se ele deixar `!shop` casar com
//     `!shopreload`, o recarregamento vira uma listagem de loja e o dono nunca
//     entende por que o config não atualiza.
//   · `Formatar` monta o que o jogador lê. Se ele errar, o jogador vê
//     "Voce tem {0} pontos" — feio, mas pior: ele NÃO pode virar printf, porque
//     o texto vem do arquivo do dono, e um "%s" digitado por engano lá viraria
//     leitura de memória arbitrária aqui dentro.
//
// São `inline` num header para o plugin e o teste compilarem exatamente o mesmo
// código — não uma cópia parecida.
#pragma once

#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>

namespace Shop
{
    // Troca {0}, {1}, {2}… pelos valores, na ordem. O que não for um número
    // entre chaves fica como está: `{nome}` é texto do dono, não placeholder.
    inline std::string Formatar(const std::string& modelo,
                                const std::vector<std::string>& valores)
    {
        std::string r;
        r.reserve(modelo.size() + 32);
        for (size_t i = 0; i < modelo.size(); ++i)
        {
            if (modelo[i] != '{') { r += modelo[i]; continue; }
            const size_t fim = modelo.find('}', i);
            if (fim == std::string::npos) { r += modelo[i]; continue; }

            const std::string dentro = modelo.substr(i + 1, fim - i - 1);
            bool numero = !dentro.empty();
            for (char c : dentro)
                if (!std::isdigit(static_cast<unsigned char>(c))) { numero = false; break; }
            if (!numero) { r += modelo[i]; continue; }

            const size_t n = static_cast<size_t>(std::atoi(dentro.c_str()));
            // Índice além do que foi passado vira vazio, não "{7}" nem lixo:
            // uma mensagem do dono citando um valor que não existe é erro dele,
            // e mostrar a chave crua ao jogador não ajuda ninguém.
            if (n < valores.size()) r += valores[n];
            i = fim;
        }
        return r;
    }

    // `texto` começa com o comando `cmd`? Devolve o resto já aparado.
    //
    // A REGRA QUE FAZ ISTO NÃO SER UM `starts_with`: depois do comando tem de
    // vir um ESPAÇO ou o fim da linha. Sem isso, `!shop` casaria com
    // `!shopreload` e com `!shopdar`, e o comando mais curto engoliria todos os
    // outros — silenciosamente, porque a listagem da loja é uma resposta
    // plausível.
    inline bool Prefixo(const std::string& texto, const std::string& cmd,
                        std::string& resto)
    {
        if (cmd.empty() || texto.size() < cmd.size()) return false;
        if (texto.compare(0, cmd.size(), cmd) != 0) return false;
        if (texto.size() > cmd.size() && texto[cmd.size()] != ' ') return false;

        resto = (texto.size() > cmd.size()) ? texto.substr(cmd.size() + 1)
                                            : std::string();
        while (!resto.empty() && (resto.front() == ' ' || resto.front() == '\t'))
            resto.erase(resto.begin());
        while (!resto.empty() && (resto.back() == ' ' || resto.back() == '\t' ||
                                  resto.back() == '\r' || resto.back() == '\n'))
            resto.pop_back();
        return true;
    }
}
