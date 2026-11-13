// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/localization.h"
#include "app/bindings.h"
#include <array>
#include <stdexcept>

namespace psmodern::app {
namespace {
struct Translation { std::string_view en,pt; };
constexpr Translation catalog[]{
    {"",""},{"OPTIONS","OPCOES"},{"ENCOUNTERS","ENCONTROS"},{"EXPERIENCE / MESETA","EXPERIENCIA / MESETA"},
    {"AUTOMAP","MAPA AUTOMATICO"},{"CONTROLS","CONTROLES"},{"LANGUAGE","IDIOMA"},
    {"ON","LIGADO"},{"OFF (DEFAULT)","DESLIGADO (PADRAO)"},{" (DEFAULT)"," (PADRAO)"},{" (RECOMMENDED)"," (RECOMENDADO)"},
    {"SAVE FAILED - RETRY","FALHA AO SALVAR - REPITA"},{"RECORDING - READ ONLY","GRAVANDO - SO LEITURA"},
    {"Z80 MODE - READ ONLY","MODO Z80 - SO LEITURA"},{"SAVED - WAITING","SALVO - AGUARDANDO"},
    {"SAVES ON EXIT","SALVA AO SAIR"},
    {"MORE ABOVE","MAIS ACIMA"},{"MORE BELOW","MAIS ABAIXO"},
    {"PLAY","JOGAR"},
    {"ADD ROM","ADICIONAR ROM"},{"OPEN SAVES FOLDER","ABRIR PASTA DE SAVES"},{"QUIT","SAIR"},
    {"KEYBOARD","TECLADO"},{"GAMEPAD","CONTROLE"},{"RESTORE DEFAULTS","RESTAURAR PADRAO"},{"SAVE","SALVAR"},{"CANCEL","CANCELAR"},
    {"NONE","NENHUM"},{"CONTROLLER CONNECTED","CONTROLE CONECTADO"},{"NO CONTROLLER FOUND","SEM CONTROLE"},
    {"PRESS A KEY FOR ","TECLA PARA "},{"PRESS BUTTON FOR ","BOTAO PARA "},
    {"KEY NOT AVAILABLE","TECLA INDISPONIVEL"},{"SWAPPED WITH ","TROCADO COM "},{"KEY USED BY ","TECLA USADA POR "},
    {"BUTTON USED BY ","BOTAO USADO POR "},{" NEEDS A KEY"," PRECISA TECLA"},
    {"ALT ENTER IS FIXED","ALT ENTER E FIXO"},{"DEFAULTS - NOT SAVED YET","PADRAO - AINDA NAO SALVO"},{"CLEARED","REMOVIDO"},
    {"MAP","MAPA"},{"FLOOR ","ANDAR "},{"YOU","VOCE"},{"CHEST","BAU"},{"EMPTY","VAZIO"},
    {"UP","SUBIR"},{"DOWN","DESCER"},{"EXIT","SAIDA"},{"DOOR","PORTA"},{"LOCKED","TRANC."},{"MAGIC","MAGIA"},{"PIT","BURACO"},
    {"AUTO (NATIVE)","AUTO (ORIGINAL)"},{"ENGLISH","INGLES"},{"PORTUGUESE","PORTUGUES"},
    {"US ROM REQUIRED","PRECISA ROM US"},{"BR ROM REQUIRED","PRECISA ROM BR"},{"FOR THE WHOLE APP","PARA TODO O APLICATIVO"},
    {"SELECTED","SELECIONADO"},{"CHOOSE A LANGUAGE","ESCOLHA O IDIOMA"},
    {"Choose again","Escolher novamente"},{"Back","Voltar"},
    {"Choose your Phantasy Star ROM","Escolha sua ROM de Phantasy Star"},
    {"Master System ROM (*.sms)","ROM de Master System (*.sms)"},{"PSModern","PSModern"},
    {"The selected language ROM is unavailable. Using the available ROM language; your preference is retained.",
     "A ROM do idioma escolhido esta indisponivel. Usando o idioma da ROM disponivel; sua preferencia foi mantida."},
    {"The language preference file could not be read. Using the native language.","Nao foi possivel ler a preferencia de idioma. Usando o idioma original."},
    {"Language could not be saved. Your current language is unchanged.","Nao foi possivel salvar o idioma. Seu idioma atual foi mantido."},
    {"The save could not be written. Your progress remains in memory; please retry.","Nao foi possivel gravar o save. Seu progresso continua na memoria; tente novamente."},
    {"Cannot open the saves folder.","Nao foi possivel abrir a pasta de saves."},
    {"Cannot remember the chosen ROMs.","Nao foi possivel lembrar as ROMs escolhidas."},
    {"Some remembered ROMs are missing or have changed. Choose them again.","Algumas ROMs lembradas sumiram ou foram alteradas. Escolha os arquivos novamente."},
    {"Some ROMs cannot be read now. They are still remembered; check the drive and try again.","Algumas ROMs nao podem ser lidas agora. Continuam lembradas; verifique a unidade e tente novamente."},
    {"Choose your own uncompressed .sms dump of the US Rev-A or Brazilian Tec Toy cartridge.","Escolha sua propria ROM .sms descompactada do cartucho US Rev-A ou brasileiro Tec Toy."},
    {"This ROM cannot be used.","Esta ROM nao pode ser usada."},
    {"POWER CYCLE DISABLED","REINICIO DESATIVADO"},{"RECORDING","GRAVANDO"},{"Speed","Velocidade"},
    {"CHANGES SAVED, WAITING FOR GAME","ALTERACOES SALVAS, AGUARDANDO O JOGO"},
    {"Language","Idioma"},
    {"PSModern could not start or continue this session. Check the ROM, save-folder access and available audio/video devices.",
     "O PSModern nao conseguiu iniciar ou continuar esta sessao. Verifique a ROM, o acesso a pasta de saves e os dispositivos de audio e video."},
    {"Saved options could not be read. The game will use its original settings.",
     "Nao foi possivel ler as opcoes salvas. O jogo usara as configuracoes originais."},
    {"The saved automap could not be read. Exploration will start with an empty map.",
     "Nao foi possivel ler o automapa salvo. A exploracao comecara com o mapa vazio."},
    {"Audio is unavailable. Continuing silently and retrying the default device.",
     "O audio esta indisponivel. O jogo continuara sem som e tentara reconectar o dispositivo padrao."},
    {"Old saved games","Partidas antigas"},
    {"Two different sets of saved games were found.\nChoose which version's progress to continue.\nBoth originals will be kept. This does not change the language.",
     "Foram encontrados dois conjuntos diferentes de partidas.\nEscolha de qual versao continuar o progresso.\nOs dois originais serao preservados. O idioma nao muda."},
    {"Some old saved games are unreadable or open elsewhere.\nContinue with the available set, or cancel to recover the other.\nAll original files will be kept.",
     "Algumas partidas antigas nao podem ser lidas ou estao em uso.\nContinue com o conjunto disponivel ou cancele para recuperar o outro.\nTodos os arquivos originais serao preservados."},
    {"AMERICAN","AMERICANA"},{"BRAZILIAN","BRASILEIRA"},
    {"Saved games could not be prepared.\nYour old files were preserved.\nClose other game instances and check access to the saves folder,\nthen try again.",
     "Nao foi possivel preparar as partidas salvas.\nSeus arquivos antigos foram preservados.\nFeche outras instancias do jogo e verifique o acesso a pasta de saves.\nDepois, tente novamente."}
};
static_assert(std::size(catalog)==std::size_t(UiText::count));
}
std::string_view ui_text(UiText id,Language language) {
    const auto index=std::size_t(id);
    if(index>=std::size(catalog))throw std::invalid_argument("Unknown UI text identifier.");
    return language==Language::portuguese ? catalog[index].pt : catalog[index].en;
}
std::string_view language_label(LanguagePreference preference,Language language) {
    switch(preference) {
    case LanguagePreference::native:return ui_text(UiText::automatic,language);
    case LanguagePreference::english:return ui_text(UiText::english,language);
    case LanguagePreference::portuguese:return ui_text(UiText::portuguese,language);
    }
    throw std::invalid_argument("Unknown language preference.");
}
std::string_view language_label(Language value,Language language) {
    return ui_text(value==Language::portuguese ? UiText::portuguese : UiText::english,language);
}
std::string_view ui_action_label(Action action,Language language) {
    if(language==Language::english)return action_label(action);
    constexpr std::string_view labels[]{"CIMA","BAIXO","ESQUERDA","DIREITA","BOTAO 1","BOTAO 2","PAUSA","RESET",
        "VELOCIDADE","OPCOES","MAPA","REINICIAR","TELA CHEIA"};
    return action<Action::count ? labels[std::size_t(action)] : std::string_view{};
}
std::string_view ui_key_label(std::uint16_t key,Language language) {
    if(language==Language::portuguese)switch(key) {
    case 44:return "ESPACO";case 73:return "INSERIR";case 74:return "INICIO";case 76:return "APAGAR";case 77:return "FIM";
    case 79:return "DIREITA";case 80:return "ESQUERDA";case 81:return "BAIXO";case 82:return "CIMA";
    default:break;
    }
    return key_token(key);
}
std::string_view ui_pad_label(PadInput input,Language language) {
    if(language==Language::portuguese)switch(input) {
    case PadInput::back:return "VOLTAR";case PadInput::guide:return "GUIA";case PadInput::start:return "INICIO";
    case PadInput::left_stick:return "LS";case PadInput::right_stick:return "RS";
    case PadInput::dpad_up:return "D CIMA";case PadInput::dpad_down:return "D BAIXO";
    case PadInput::dpad_left:return "D ESQ";case PadInput::dpad_right:return "D DIR";
    case PadInput::misc1:return "EXTRA";case PadInput::touchpad:return "TOQUE";
    default:break;
    }
    return pad_label(input);
}
}
