// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/retro_helpers.h"
#include <iterator>

namespace psmodern::app::retro {
namespace {
struct Help {
    std::string_view en, pt;
    std::string_view in(Language language) const { return language == Language::portuguese ? pt : en; }
};
constexpr Help transfer{"Returns to the last church visited.", "Retorna à última igreja visitada."};
constexpr Help escape{"Attempts to escape from battle.", "Tenta fugir da batalha."};
constexpr Help talk{"Tries to speak with an enemy.", "Tenta conversar com um inimigo."};
constexpr Help inspect{"Checks for traps.", "Verifica se há armadilhas."};

// The first slot in the spell lists is empty. The healing spells at
// 00:1E27 and 00:1E2B have different amounts from the food at 00:2333.
constexpr Help spells[]{
    {},
    {"Restores up to 20 HP to one ally.", "Recupera até 20 PF de um aliado."},
    {"Restores up to 80 HP to one ally.", "Recupera até 80 PF de um aliado."},
    {"Guards the party against physical attacks.", "Protege o grupo de golpes físicos."},
    {"Guards against physical and magic attacks.", "Protege de golpes físicos e mágicos."},
    {"Strikes twice, choosing foes at random.", "Atinge inimigos ao acaso duas vezes."},
    {"Strikes the enemy group.", "Atinge o grupo inimigo."},
    {"Strikes three times, choosing foes at random.", "Atinge inimigos ao acaso três vezes."},
    {"May prevent an enemy from acting.", "Pode impedir um inimigo de agir."},
    escape,
    {"Raises one ally’s attack.", "Aumenta o ataque de um aliado."},
    {"May lower an enemy’s attack.", "Pode reduzir o ataque de um inimigo."},
    {"Disarms a trap in a chest or passage.", "Desarma armadilhas em baús ou passagens."},
    {"Takes the party out of a dungeon.", "Leva o grupo para fora da masmorra."},
    {"Opens a sealed magic door.", "Abre uma porta selada por magia."},
    {"Revives an ally with full HP and MP.", "Revive um aliado com todos os PF e PM."},
    talk, talk, transfer
};

Help item(std::uint8_t id) {
    // Equipment usually helps by being worn. Some weapons and shields
    // have another effect, and can be highlighted in both shop and bag.
    switch (id) {
    case 0x04: return {"Can also be used to escape a fight.", "Também permite tentar fugir da luta."};
    case 0x09: case 0x0b: case 0x0d:
        return {"Fires at the enemy group.", "Dispara contra o grupo inimigo."};
    case 0x1f: return {"Can turn aside a petrifying gaze.", "Pode desviar um olhar petrificante."};
    case 0x21: return {"Drives across deserts and rough ground.", "Cruza desertos e terrenos difíceis."};
    case 0x22: return {"Carries the party across water.", "Transporta o grupo pela água."};
    case 0x23: return {"Clears a path through ice.", "Abre caminho pelo gelo."};
    case 0x24: return {"Restores up to 10 HP to one ally.", "Recupera até 10 PF de um aliado."};
    case 0x25: return {"Restores up to 40 HP to one ally.", "Recupera até 40 PF de um aliado."};
    case 0x26: return {"Leads the party out of a dungeon.", "Guia o grupo para fora da masmorra."};
    case 0x27: return {"Lights up a dark dungeon.", "Ilumina uma masmorra escura."};
    case 0x28: return escape;
    case 0x29: return transfer;
    case 0x2a: return talk;
    case 0x2b: return {"Can undo petrification.", "Pode reverter a petrificação."};
    case 0x2c: return {"Dissolves certain materials.", "Dissolve certos materiais."};
    case 0x2d: return {"Unlocks an ordinary locked dungeon door.", "Abre portas de masmorra com fechadura."};
    case 0x2e: return {"Helps with enemy speech or trap inspection.", "Ajuda a falar com inimigos ou detectar armadilhas."};
    case 0x2f: return {"Kindles the light needed to gather fruit.", "Fornece a luz usada para colher frutos."};
    case 0x30: return {"Reveals a hidden castle.", "Revela um castelo oculto."};
    case 0x31: return {"Can transform a certain companion.", "Pode transformar um dos companheiros."};
    case 0x32: return {"Pilots the party’s spacecraft.", "Pilota a nave do grupo."};
    case 0x33: return {"Grants passage past the road guards.", "Permite passar pela guarda da estrada."};
    case 0x34: return {"Allows travel through the spaceport.", "Permite viajar pelo porto espacial."};
    case 0x35: return {"Shows which way you face in a dungeon.", "Mostra sua direção na masmorra."};
    case 0x36: return {"A gift that can open the way.", "Um presente que pode liberar a passagem."};
    case 0x37: return {"Introduces the bearer to a new ally.", "Apresenta o portador a um novo aliado."};
    case 0x38: return {"A vessel for gathering rare fruit.", "Recipiente para colher frutos raros."};
    case 0x39: return {"Lights dark dungeons while carried.", "Ilumina masmorras enquanto estiver na bolsa."};
    case 0x3a: return {"Can be traded for a torch.", "Pode ser trocado por uma tocha."};
    case 0x3b: return {"Protects the party from poison gas.", "Protege o grupo do gás venenoso."};
    case 0x3c: return {"Wards off a deadly enemy attack.", "Protege de um ataque inimigo mortal."};
    case 0x3d: case 0x3f: return {"A keepsake.", "Uma lembrança."};
    case 0x3e: return {"Opens locked and magic dungeon doors.", "Abre portas trancadas ou mágicas."};
    case 0x40: return {"Ask the merchant about this.", "Pergunte ao comerciante."};
    default:
        if (id >= 1 && id <= 0x0f) return {"Equip to change attack power.", "Equipe para mudar o poder de ataque."};
        if (id >= 0x10 && id <= 0x20) return {"Equip to change defense.", "Equipe para mudar a defesa."};
        return {};
    }
}
} // namespace

std::string_view spell_help(std::uint8_t id, Language language, SpellScene scene) {
    if (id >= std::size(spells)) return {};
    if ((id == 0x10 || id == 0x11) && scene == SpellScene::field) return inspect.in(language);
    return spells[id].in(language);
}
std::string_view item_help(std::uint8_t id, Language language) { return item(id).in(language); }
}
