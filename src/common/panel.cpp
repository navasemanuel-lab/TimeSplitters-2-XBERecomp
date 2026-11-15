// The settings panel: F1, or Back and Start together on a pad, opens it over the game. Controls: each key or mouse
// button of the keyboard's pad, rebound by clicking it and pressing the new one (Delete unbinds, Esc cancels). Mouse:
// the turn's speed and direction. A change applies at once and the settings file is written when the panel closes.
// While it is open the cursor is free and the game sees idle pads; after it closes, what closed it reaches the game
// only once it is let go.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "common/button_icons.h"
#include "common/controls.h"
#include "common/lang.h"
#include "common/mouse.h"
#include "common/options.h"
#include "common/panel.h"
#include "common/weapon_bar.h"
#include "core/diag.h"
#include "core/input.h"
#include "gpu/backend.h"

void SetXmasTextures(bool on);  // game/pak_replace.cpp

namespace panel {

namespace {

constexpr int kToggleKey = VK_F1;
std::atomic<bool> g_open{false};
std::atomic<int> g_tab{-1};  // a tab to show when the panel next draws
std::atomic<bool> g_hold[4];
std::atomic<uint64_t> g_closed_ms{0};  // when it closed: the hold on the game's input ends within a second and a half
int g_wait = -1;       // the binding waiting for its key
int g_wait_slot = 0;   // which of its two keys
bool g_armed = false;  // the keys that started the wait are up
std::string g_note;    // what the last change did to another action

void SetOpen(bool on) {
    if (g_open.exchange(on) == on) return;
    g_wait = -1;
    mouse::SetPanel(on);
    if (!on) {
        g_closed_ms = GetTickCount64();
        for (auto& h : g_hold) h = true;
        app::Save();
    }
}

bool AnyKeyDown() {
    for (int vk = 1; vk < 255; vk++)
        if (GetAsyncKeyState(vk) < 0) return true;
    return false;
}

// the key, mouse button or wheel notch pressed now, 0 none; Shift and Ctrl are the generic keys, Alt, the Windows keys and
// the panel's own are not bindable
int PressedKey() {
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel > 0) return controls::kWheelUp;
    if (wheel < 0) return controls::kWheelDown;
    for (int vk = 1; vk < 255; vk++) {
        if ((vk >= 0xA0 && vk <= 0xA5) || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN || vk == kToggleKey) continue;
        if (GetAsyncKeyState(vk) < 0) return vk;
    }
    return 0;
}

bool Contains(const std::string& hay, const char* needle) {
    auto low = [](std::string t) {
        for (char& c : t) c = char(std::tolower((unsigned char)c));
        return t;
    };
    return low(hay).find(low(needle)) != std::string::npos;
}

// a combo whose entries are translated as they are shown (the items are the source language's)
bool ComboTr(const char* label, int* cur, const char* const* items, int n) {
    bool changed = false;
    if (ImGui::BeginCombo(label, Tr(items[std::clamp(*cur, 0, n - 1)]))) {
        for (int i = 0; i < n; i++)
            if (ImGui::Selectable(Tr(items[i]), i == *cur)) {
                *cur = i;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

// the panel's language, at the window's top
void LanguageBar() {
    game::Options& o = game::Opt();
    int n = lang::Count();
    o.ui_lang = std::min<uint32_t>(o.ui_lang, uint32_t(n - 1));
    ImGui::SetNextItemWidth(220.0f * std::max(1.0f, ImGui::GetIO().FontGlobalScale));
    if (ImGui::BeginCombo("Language / Idioma", lang::Name(int(o.ui_lang)))) {
        for (int i = 0; i < n; i++)
            if (ImGui::Selectable(lang::Name(i), i == int(o.ui_lang))) o.ui_lang = uint32_t(i);
        ImGui::EndCombo();
    }
}

// the prompts' buttons as the input in use (button_icons.cpp): the option, which set, and the ones shown now
void ButtonIcons() {
    game::Options& o = game::Opt();
    ImGui::Checkbox(Tr("Iconos de botones del juego segun el mando o el teclado"), &o.button_icons);
    ImGui::TextWrapped("%s", Tr("Los avisos de los menus y la pausa (A Aceptar, B Atras...) muestran los botones del mando que uses "
                                "(Xbox, PlayStation 4 y 5, Switch) o las teclas que tengas asignadas, y cambian al momento si cambias una tecla."));
    if (o.button_icons) {
        static const char* const kStyles[] = {"Automatico (segun el ultimo mando o tecla usado)", "Xbox", "PlayStation 4", "PlayStation 5", "Switch", "Teclado y raton"};
        int cur = int(std::min<uint32_t>(o.button_icons_style, 5));
        ImGui::SetNextItemWidth(320.0f * std::max(1.0f, ImGui::GetIO().FontGlobalScale));
        if (ComboTr(Tr("Iconos"), &cur, kStyles, 6)) o.button_icons_style = uint32_t(cur);
        // the six buttons of the set shown now: B A Y X Back Start
        ImGui::TextUnformatted(Tr("Ahora:"));
        float h = ImGui::GetFrameHeight() * 1.3f;
        button_icons::Style shown = button_icons::Shown();
        for (int b : {2, 1, 4, 3, 5, 6}) {
            ImGui::SameLine();
            if (shown == button_icons::kKeyboard) {
                static const char* const ids[7] = {"", "btn_b", "btn_a", "btn_y", "btn_x", "back", "start"};
                for (int i = 0; i < controls::kBindingCount; i++)
                    if (!std::strcmp(controls::kBindings[i].id, ids[b])) {
                        int vk = controls::KeyOf(i) ? controls::KeyOf(i) : controls::Key2Of(i);
                        if (vk) button_icons::DrawKey(vk, h);
                    }
            } else {
                button_icons::DrawPad(shown, b, h);
            }
        }
    }
    ImGui::Separator();
}

void Controls() {
    static char filter[64] = "";
    ImGui::TextWrapped("%s", Tr("Cada accion tiene una tecla principal y otra alterna. Pulsa una casilla y despues la tecla, el boton del raton "
        "o la rueda que quieras. Clic derecho, o Suprimir mientras esperas, la deja sin asignar; Esc cancela. Si la tecla "
        "ya era de otra accion, esa se queda sin ella. Sin nada asignado a la rueda, cambia de arma y hace zoom con el "
        "visor puesto."));
    ButtonIcons();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", Tr("Filtrar acciones o teclas..."), filter, sizeof filter);
    ImGui::Separator();
    if (g_wait >= 0) {
        if (!g_armed) {
            if (!AnyKeyDown() && ImGui::GetIO().MouseWheel == 0) g_armed = true;
        } else if (int vk = PressedKey()) {
            if (vk == VK_DELETE) {
                controls::SetKey(g_wait, g_wait_slot, 0);
                g_note.clear();
            } else if (vk != VK_ESCAPE) {
                int lost = controls::SetKey(g_wait, g_wait_slot, vk);
                g_note = lost >= 0 ? std::string(controls::KeyName(vk)) + Tr(" ya era de ") + "\"" +
                                         Tr(controls::kBindings[lost].label) + "\"" + Tr(": se quedo sin ella.")
                                   : "";
            }
            g_wait = -1;
        }
    }
    if (!g_note.empty()) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", g_note.c_str());
    int shown = 0;
    if (ImGui::BeginTable("keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn(Tr("Accion"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn(Tr("Principal"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(Tr("Alterna"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < controls::kBindingCount; i++) {
            std::string k1 = controls::KeyName(controls::KeyOf(i)), k2 = controls::KeyName(controls::Key2Of(i));
            if (filter[0] && !Contains(Tr(controls::kBindings[i].label), filter) && !Contains(k1, filter) &&
                !Contains(k2, filter))
                continue;
            shown++;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Tr(controls::kBindings[i].label));
            for (int slot = 0; slot < 2; slot++) {
                ImGui::TableNextColumn();
                ImGui::PushID(i * 2 + slot);
                bool waiting = g_wait == i && g_wait_slot == slot;
                std::string name = waiting ? Tr("Pulsa una tecla...") : (slot == 0 ? k1 : k2);
                // the picture of the key beside its name (the prompts of the game show the same: button_icons.cpp)
                if (int vk = slot == 0 ? controls::KeyOf(i) : controls::Key2Of(i); vk && !waiting)
                    if (button_icons::DrawKey(vk, ImGui::GetFrameHeight())) ImGui::SameLine();
                if (ImGui::Button(name.c_str(), ImVec2(-FLT_MIN, 0))) {
                    g_wait = i;
                    g_wait_slot = slot;
                    g_armed = false;
                    g_note.clear();
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    controls::SetKey(i, slot, 0);
                    g_wait = -1;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (!shown) ImGui::TextUnformatted(Tr("Ninguna accion coincide."));
    if (ImGui::Button(Tr("Restablecer teclas"))) {
        controls::ResetKeys();
        g_note.clear();
    }
}

// a speed as a multiplier of the game's own, 0.25x to 3x
void Speed(const char* label, uint32_t& percent) {
    float v = float(percent) / 100.0f;
    if (ImGui::SliderFloat(label, &v, 0.25f, 3.0f, "%.2fx")) percent = uint32_t(v * 100.0f + 0.5f);
}

void Mouse() {
    game::Options& o = game::Opt();
    ImGui::TextUnformatted(Tr("MANDO"));
    Speed(Tr("Velocidad de giro horizontal"), o.pad_h);
    Speed(Tr("Velocidad de giro vertical"), o.pad_v);
    Speed(Tr("Velocidad horizontal al apuntar"), o.pad_aim_h);
    Speed(Tr("Velocidad vertical al apuntar"), o.pad_aim_v);
    ImGui::Separator();
    ImGui::TextUnformatted(Tr("RATON Y TECLADO"));
    uint32_t sens = o.mouse_sensitivity;
    Speed(Tr("Velocidad de giro horizontal##m"), sens);
    o.mouse_sensitivity = sens;
    uint32_t vert = o.mouse_vertical;
    Speed(Tr("Velocidad vertical (respecto a la horizontal)"), vert);
    o.mouse_vertical = vert;
    Speed(Tr("Velocidad horizontal al apuntar##m"), o.mouse_aim_h);
    Speed(Tr("Velocidad vertical al apuntar##m"), o.mouse_aim_v);
    ImGui::Checkbox(Tr("Invertir eje vertical"), &o.invert_mouse);
    {
        int sway = int(o.turn_sway);
        if (ImGui::SliderInt(Tr("Sway de la mira al girar (estilo GoldenEye)"), &sway, 0, 100, "%d%%")) o.turn_sway = uint32_t(sway);
    }
    ImGui::Checkbox(Tr("Auto Lookahead (nivelar la vista al horizonte)"), &o.auto_lookahead);
    if (ImGui::Button(Tr("Restablecer a 1x"))) {
        o.pad_h = o.pad_v = o.pad_aim_h = o.pad_aim_v = o.mouse_aim_h = o.mouse_aim_v = 100;
        o.mouse_sensitivity = o.mouse_vertical = 100;
    }
    ImGui::TextWrapped("%s", Tr("1x del raton son 0,1 grados por cuenta; con el visor puesto gira menos para cubrir lo mismo."));
}

void Screen() {
    static const char* const kAspects[] = {"Automatica (la de la pantalla)", "4:3", "16:9", "16:10", "21:9", "32:9"};
    static const char* const kModes[] = {"Pantalla completa", "Ventana"};
    static const char* const kEdge[] = {"Desactivado", "FXAA", "SMAA"};
    static const char* const kMsaa[] = {"Desactivado", "2x", "4x", "8x"};
    static const char* const kAniso[] = {"Como pide el juego", "2x", "4x", "8x", "16x"};
    struct Res { uint32_t w, h; const char* name; };
    static const Res kRes[] = {{0, 0, "Escritorio"},   {1280, 720, "1280x720"},   {1600, 900, "1600x900"},
                               {1920, 1080, "1920x1080"}, {2560, 1080, "2560x1080"}, {2560, 1440, "2560x1440"},
                               {3440, 1440, "3440x1440"}, {3840, 2160, "3840x2160"}};
    app::Settings& st = app::Get();

    int mode = st.display_mode == app::DisplayMode::Windowed ? 1 : 0;
    if (ComboTr(Tr("Modo de pantalla"), &mode, kModes, 2)) {
        st.display_mode = mode ? app::DisplayMode::Windowed : app::DisplayMode::Fullscreen;
        app::ApplyDisplay();
    }
    int res = 0;
    for (int i = 0; i < int(std::size(kRes)); i++)
        if (kRes[i].w == st.width && kRes[i].h == st.height) res = i;
    if (ImGui::BeginCombo(Tr("Resolucion"), Tr(kRes[res].name))) {
        for (int i = 0; i < int(std::size(kRes)); i++)
            if (ImGui::Selectable(Tr(kRes[i].name), i == res)) {
                st.width = kRes[i].w;
                st.height = kRes[i].h;
                app::ApplyDisplay();
            }
        ImGui::EndCombo();
    }
    int cur = int(std::min<uint32_t>(st.aspect, 5));
    if (ComboTr(Tr("Relacion de aspecto"), &cur, kAspects, int(std::size(kAspects)))) st.aspect = uint32_t(cur);
    {
        // The internal resolution, lowest to highest, as lines drawn: the console's own 480, the window's own pixels
        // (Native), then 720p up to 4320p. It is the render scale, set from the output's height.
        float now = app::RenderScale(640, 480).y * 480.0f;           // lines drawn now
        float out_h = now * 100.0f / float(std::max<uint32_t>(st.render_percent, 1));  // the output's lines
        struct Step { float lines; const char* name; };
        const Step steps[] = {{480.0f, "Original"}, {out_h, "Nativa"}, {720.0f, "720p"}, {1080.0f, "1080p"},
                              {1440.0f, "1440p"}, {2160.0f, "2160p"}, {2880.0f, "2880p"}, {4320.0f, "4320p"}};
        char label[48];
        std::snprintf(label, sizeof(label), "%.0fp", now);
        for (const Step& s : steps)
            if (std::fabs(s.lines - now) < 1.5f) std::snprintf(label, sizeof(label), "%s", s.name[0] == 'N' || s.name[0] == 'O' ? Tr(s.name[0] == 'N' ? "Nativa" : "Original") : s.name);
        if (ImGui::BeginCombo(Tr("Resolucion interna"), label)) {
            std::vector<Step> sorted(std::begin(steps), std::end(steps));
            std::sort(sorted.begin(), sorted.end(), [](const Step& a, const Step& b) { return a.lines < b.lines; });
            for (const Step& s : sorted) {
                char name[48];
                bool special = s.name[0] == 'N' || s.name[0] == 'O';
                std::snprintf(name, sizeof(name), "%s (%.0fp)", special ? Tr(s.name[0] == 'N' ? "Nativa" : "Original") : s.name, s.lines);
                if (ImGui::Selectable(name, std::fabs(s.lines - now) < 1.5f) && out_h > 0.0f) {
                    st.render_percent = std::clamp<uint32_t>(uint32_t(s.lines / out_h * 100.0f + 0.5f), 1, 1600);
                    st.render_fitted = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("%s", Tr("Nativa iguala los pixeles de la ventana; Original dibuja las 480 lineas de la consola. Mas alta afina la imagen y gasta mas GPU."));
    }
    int msaa = st.msaa >= 8 ? 3 : st.msaa >= 4 ? 2 : st.msaa >= 2 ? 1 : 0;
    if (ComboTr(Tr("MSAA"), &msaa, kMsaa, 4)) st.msaa = msaa == 0 ? 0u : (2u << (msaa - 1));
    int edge = int(std::min<uint32_t>(st.edge_aa, 2));
    if (ComboTr(Tr("Suavizado de bordes"), &edge, kEdge, 3)) st.edge_aa = uint32_t(edge);
    int aniso = st.aniso >= 16 ? 4 : st.aniso >= 8 ? 3 : st.aniso >= 4 ? 2 : st.aniso >= 2 ? 1 : 0;
    if (ComboTr(Tr("Filtrado anisotropico"), &aniso, kAniso, 5)) st.aniso = aniso == 0 ? 0u : (1u << aniso);
    ImGui::Checkbox(Tr("Sincronizacion vertical"), &st.vsync);
    int bright = int(st.brightness);
    if (ImGui::SliderInt(Tr("Brillo"), &bright, 50, 150, "%d%%")) st.brightness = uint32_t(bright);
    int sat = int(st.saturation);
    if (ImGui::SliderInt(Tr("Saturacion"), &sat, 50, 200, "%d%%")) st.saturation = uint32_t(sat);
    int con = int(st.contrast);
    if (ImGui::SliderInt(Tr("Contraste"), &con, 50, 150, "%d%%")) st.contrast = uint32_t(con);
    ImGui::Checkbox(Tr("Distorsion de calor del fuego"), &st.heat_haze);
    ImGui::Checkbox(Tr("Neblina de atmosfera (el filtro suave de la Xbox sobre la imagen)"), &st.atmosphere_haze);
    ImGui::Checkbox(Tr("Estelas de movimiento (la imagen anterior mezclada)"), &st.motion_trails);
    ImGui::Checkbox(Tr("Estelas de clima (las que anaden la lluvia y la nieve)"), &st.weather_trails);
    ImGui::Checkbox(Tr("Luz de fuego y explosiones con bordes suaves en el nivel"), &st.smooth_lighting);
    game::Options& o = game::Opt();
    int fov = int(o.fov);
    if (ImGui::SliderInt(Tr("Campo de vision (original: 60)"), &fov, 60, 100, Tr("%d grados"))) o.fov = uint32_t(fov);
    ImGui::TextWrapped("%s", Tr("Todo se aplica al momento. La vista del juego se ensancha con la relacion de aspecto, con el mismo angulo "
        "vertical; reinicia si quieres que el juego use sus pantallas de 16:9 desde el principio."));
}

// the Graphics tab's second half: HDR and the frame rate
void GraphicsExtra() {
    app::Settings& st = app::Get();
    game::Options& o = game::Opt();
    ImGui::Separator();
    ImGui::Checkbox(Tr("HDR10 (pantalla con HDR activado en Windows)"), &st.hdr);
    if (gpu::g_hdr_output == 1) ImGui::SameLine(), ImGui::TextUnformatted(Tr("activo"));
    else if (gpu::g_hdr_output == -1) ImGui::SameLine(), ImGui::TextUnformatted(Tr("HDR desactivado en Windows"));
    if (st.hdr) {
        static const uint32_t kWhite[] = {0, 100, 150, 200, 250, 300, 400, 500};
        static const uint32_t kPeak[] = {0, 400, 600, 800, 1000, 1500, 2000, 4000};
        auto pick = [](const char* label, uint32_t& v, const uint32_t* list, int n, const char* zero) {
            std::string cur = v ? std::to_string(v) + " nits" : zero;
            if (ImGui::BeginCombo(label, cur.c_str())) {
                for (int i = 0; i < n; i++) {
                    std::string name = list[i] ? std::to_string(list[i]) + " nits" : zero;
                    if (ImGui::Selectable(name.c_str(), list[i] == v)) v = list[i];
                }
                ImGui::EndCombo();
            }
        };
        pick(Tr("Blanco HDR"), st.hdr_white, kWhite, 8, Tr("El de Windows"));
        pick(Tr("Brillos HDR"), st.hdr_peak, kPeak, 8, Tr("El de la pantalla"));
    }
    ImGui::Checkbox(Tr("Mostrar estadisticas de rendimiento"), &o.show_stats);
    ImGui::TextWrapped("%s", Tr("En la esquina superior izquierda: adonde va el tiempo de cada fotograma (hilos del juego, hilo de dibujo, "
        "entrega a la pantalla, trabajo de la GPU por tipo, dibujos, mezclador de audio y los hilos mas ocupados). "
        "Medir cuesta un poco."));
    ImGui::Checkbox(Tr("Guardar rendimiento en el log"), &o.log_stats);
    ImGui::TextWrapped("%s", Tr("Los mismos numeros en logs/game.log: un promedio cada diez segundos (con el 1% de fotogramas mas lentos), una "
        "linea por cada tiron (shaders creados, texturas cargadas, el juego esperando) y un resumen cada cinco minutos."));
}

void Audio() {
    app::Settings& st = app::Get();
    game::Options& o = game::Opt();
    static const char* const kOut[] = {"Automatico (segun tus altavoces)", "Mono", "Estereo 2.0", "Surround 5.1"};
    int out = int(std::min<uint32_t>(st.audio_out, 3));
    if (ComboTr(Tr("Salida de audio (al reiniciar)"), &out, kOut, 4)) st.audio_out = uint32_t(out);
    ImGui::Separator();  // the gain (audio_boost_db in settings.toml) has no slider: it keeps its setting, 6 dB by default
    ImGui::TextWrapped("%s", Tr("El volumen de la musica y de los efectos se ajusta en Options > Audio Options del juego."));
}

void Cheats() {
    game::Options& o = game::Opt();
    ImGui::Checkbox(Tr("Invulnerabilidad"), &o.infinite_health);
    ImGui::TextWrapped("%s", Tr("No recibes dano. Se aplica a todos los jugadores locales."));
    ImGui::Checkbox(Tr("Lanzacohetes en todos los niveles de Historia"), &o.story_rockets);
    ImGui::TextWrapped("%s", Tr("Se da al empezar o reiniciar un nivel de Historia, con la municion llena."));
    ImGui::Checkbox(Tr("Un solo golpe en Historia"), &o.one_hit);
    ImGui::TextWrapped("%s", Tr("Cualquier dano mata, al jugador y a los enemigos, en los niveles de Historia."));
    ImGui::Separator();
    ImGui::TextWrapped("%s", Tr("Desbloquear todo (niveles, personajes y retos del perfil) esta en el menu del juego: Options > Player Options > Unlock Everything."));
}

void Gameplay() {
    game::Options& o = game::Opt();
    ImGui::Checkbox(Tr("Barras de vida y escudo estilo TimeSplitters 1 (en lugar de los arcos)"), &o.ts1_bars);
    ImGui::TextWrapped("%s", Tr("Dos barras arriba a la izquierda en vez de los arcos de vida y escudo. Solo con un jugador."));
    ImGui::Checkbox(Tr("Cinematicas sin barras negras"), &o.no_letterbox);
    ImGui::TextWrapped("%s", Tr("Recorta las barras negras de arriba y abajo de los videos de las cinematicas en pantallas anchas."));
    ImGui::Checkbox(Tr("Mostrar siempre la reticula (sin apuntar)"), &o.reticle_always);
    ImGui::TextWrapped("%s", Tr("La mira se ve siempre, no solo al apuntar o con un objetivo cerca. Necesita la mira activada en las opciones del juego."));
    ImGui::Checkbox(Tr("Arcos de vida y escudo altos, como en TS2 Redux (apagado: tamano original)"), &o.arcs_tall);
    ImGui::Checkbox(Tr("TS2 Beta Weapons (resourcepacks/beta_weapons)"), &o.beta_reborn);
    ImGui::TextWrapped("%s", Tr("Cambia los modelos de las armas por los del mod Beta Reborn. Se aplica al cargar el siguiente nivel."));
    {
        // the disc's five text languages, by the console's setting (XC_LANGUAGE) that picks each: the game's tables are
        // English, French, German, Italian, Spanish (0x335EA8); the voices are the English ones
        static const uint32_t kXc[] = {1, 4, 3, 6, 5};
        static const char* const kNames[] = {"English", "Francais", "Deutsch", "Italiano", "Espanol"};
        uint32_t& language = app::Get().language;
        int cur = 0;
        for (int i = 0; i < 5; i++)
            if (kXc[i] == language) cur = i;
        if (ImGui::Combo(Tr("Idioma del juego (al reiniciar)"), &cur, kNames, 5)) language = kXc[cur];
        ImGui::TextWrapped("%s", Tr("El texto de los menus y del juego. Las voces siguen en ingles."));
    }
    ImGui::Separator();
    ImGui::Checkbox(Tr("Apuntado con giroscopio (DualSense, DualShock 4, Switch Pro)"), &o.gyro_aim);
    int gyro = int(o.gyro_speed);
    if (ImGui::SliderInt(Tr("Velocidad del giroscopio"), &gyro, 50, 800, "%d%%")) o.gyro_speed = uint32_t(gyro);
    ImGui::TextWrapped("%s", Tr("Gira la vista junto al stick derecho. Deja el mando quieto un segundo para quitar la deriva."));
}

// the frame rate in the top left corner, over everything
void DrawFps(double fps) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f FPS", fps);
    ImGuiIO& io = ImGui::GetIO();
    float size = std::clamp(io.DisplaySize.y / 900.0f, 0.75f, 2.5f) * 24;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 p{size * 0.6f, size * 0.4f};
    dl->AddText(nullptr, size, {p.x + 1, p.y + 1}, IM_COL32(0, 0, 0, 200), text);
    dl->AddText(nullptr, size, p, IM_COL32(255, 220, 90, 255), text);
}

// The performance overlay: the backend's numbers (gpu::Stats, every half second) as a column of milliseconds a frame
// and their share of the frame, lit yellow past half of it and red past nine tenths, under the frame rate.
struct StatRow {
    std::string label, value;
    ImU32 colour;
};
void DrawStats(bool below_fps) {
    gpu::FrameStats st = gpu::Stats();
    std::vector<StatRow> rows;
    const ImU32 kPlain = IM_COL32(150, 235, 150, 255), kDim = IM_COL32(150, 170, 150, 255),
                kHead = IM_COL32(255, 255, 255, 255), kWarn = IM_COL32(255, 210, 80, 255),
                kBad = IM_COL32(255, 90, 80, 255);
    double frame = st.frame > 0 ? st.frame : 1;
    auto add = [&](ImU32 c, const std::string& label, const char* fmt, auto... args) {
        char b[96];
        std::snprintf(b, sizeof b, fmt, args...);
        rows.push_back({label, b, c});
    };
    auto level = [&](double ms) { return ms > frame * 0.9 ? kBad : ms > frame * 0.5 ? kWarn : kPlain; };
    auto ms_row = [&](int depth, const char* name, double ms, bool share) {
        std::string label = std::string(size_t(depth) * 2, ' ') + name;
        ImU32 c = depth ? kDim : (share ? level(ms) : kPlain);
        if (share) add(c, label, "%7.2f ms %4.0f%%", ms, ms / frame * 100);
        else add(c, label, "%7.2f ms", ms);
    };
    if (!st.valid) {
        add(kDim, "midiendo...", "%s", "");
    } else {
        add(st.worst > frame * 1.5 ? kWarn : kHead, "FPS", "%5.1f  %6.2f ms  peor %.1f", st.fps, st.frame, st.worst);
        if (st.shown_fps > st.fps * 1.2) add(kDim, "en pantalla", "%5.1f FPS (interpolados)", st.shown_fps);
        add(kDim, "dibujos", "%5.0f  tris %6.1fk  subida %4.1f MB", st.draws, st.tris / 1000, st.upload_mb);
        add(kHead, "CPU por fotograma", "%s", "");
        ms_row(1, "hilos del juego", st.game, true);
        ms_row(2, "espera al dibujo", st.game_wait, false);
        ms_row(2, "limite de fps", st.pace, false);
        ms_row(1, "hilo de dibujo", st.render, true);
        ms_row(2, "dibujos", st.draw, false);
        ms_row(2, "copias, borrados", st.copy, false);
        if (st.compile >= 0.005) ms_row(2, "shaders", st.compile, false);
        ms_row(1, "entrega", st.present, true);
        ms_row(2, "intercambio", st.swap, false);
        ms_row(2, "vsync, limite", st.vsync, false);
        add(kDim, "  mezclador de audio", "%5.1f%% de un nucleo", st.audio);
        add(kHead, "GPU por fotograma", "%s", "");
        ms_row(1, "en total", st.gpu, true);
        if (st.shaders || st.pipelines) add(kWarn, "nuevos", "%.0f shaders %.0f pipelines", st.shaders, st.pipelines);
    }
    ImGuiIO& io = ImGui::GetIO();
    float fps_size = std::clamp(io.DisplaySize.y / 900.0f, 0.75f, 2.5f) * 24;
    float size = std::clamp(io.DisplaySize.y / 1080.0f, 1.0f, 2.4f) * 13, line = size * 1.12f;
    ImFont* f = ImGui::GetFont();
    ImVec2 p{fps_size * 0.6f, below_fps ? fps_size * 1.6f : fps_size * 0.4f};
    float label_w = 0, value_w = 0;
    for (const StatRow& r : rows) {
        if (r.value.empty()) continue;
        label_w = std::max(label_w, f->CalcTextSizeA(size, FLT_MAX, 0, r.label.c_str()).x);
        value_w = std::max(value_w, f->CalcTextSizeA(size, FLT_MAX, 0, r.value.c_str()).x);
    }
    float w = label_w + size + value_w;
    for (const StatRow& r : rows)
        if (r.value.empty()) w = std::max(w, f->CalcTextSizeA(size, FLT_MAX, 0, r.label.c_str()).x);
    float pad = size * 0.5f;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled({p.x - pad, p.y - pad}, {p.x + w + pad, p.y + line * float(rows.size()) + pad},
                      IM_COL32(0, 0, 0, 190), size * 0.3f);
    for (const StatRow& r : rows) {
        dl->AddText(f, size, p, r.colour, r.label.c_str());
        if (!r.value.empty()) dl->AddText(f, size, {p.x + label_w + size, p.y}, r.colour, r.value.c_str());
        p.y += line;
    }
}

}  // namespace

void Ui(double fps) {
    if (game::Opt().show_fps) DrawFps(fps);
    // the overlay measures while it is shown, without the log lines of F9's measurement
    diag::StatsFlag().store(game::Opt().show_stats, std::memory_order_relaxed);
    diag::StatsLogFlag().store(game::Opt().log_stats, std::memory_order_relaxed);
    if (game::Opt().show_stats) DrawStats(game::Opt().show_fps);
    weapon_bar::Draw();
    if (!g_open) return;
    ImGuiIO& io = ImGui::GetIO();
    float scale = std::max(1.0f, io.DisplaySize.y / 1080.0f);
    io.FontGlobalScale = scale;
    // a column down the right side, 560 px wide at 1080p and the full height, as in Future Perfect's port: the game
    // stays visible beside it
    float margin = 10 * scale;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - margin, margin), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(
        ImVec2(std::min(560 * scale, io.DisplaySize.x - 2 * margin), io.DisplaySize.y - 2 * margin),
        ImGuiCond_Always);
    ImGui::Begin(Tr("TimeSplitters 2 - ajustes (F1)"), nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    LanguageBar();
    auto tab = [&](const char* name, int index, void (*fn)()) {
        if (ImGui::BeginTabItem(name, nullptr, g_tab.load() == index ? ImGuiTabItemFlags_SetSelected : 0)) {
            if (ImGui::BeginChild("page", ImVec2(0, 0), false)) fn();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
    };
    if (ImGui::BeginTabBar("tabs")) {
        tab(Tr("Graficos"), 0, [] { Screen(); GraphicsExtra(); });
        tab(Tr("Audio"), 1, Audio);
        tab(Tr("Controles"), 2, [] { Controls(); ImGui::Separator(); Mouse(); });
        tab(Tr("Jugabilidad"), 3, Gameplay);
        tab(Tr("Trucos"), 4, Cheats);
        ImGui::EndTabBar();
        g_tab = -1;
    }
    ImGui::End();
}

bool Message(unsigned msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wp == kToggleKey) {
        if (!(lp & (1 << 30))) Toggle();  // not the repeats of a held key
        *result = 0;
        return true;
    }
    return false;
}

void Toggle() { SetOpen(!g_open); }
void OpenTab(int tab) {
    g_tab = tab;
    SetOpen(true);
}
bool Open() { return g_open; }

bool Holds(int port, const input::Pad& pad) {
    if (port < 0 || port > 3) return false;
    if (g_open) return true;
    if (!g_hold[port]) return false;
    if (GetTickCount64() - g_closed_ms > 1500) {  // a hold never outlasts that, whatever still reads as held
        g_hold[port] = false;
        return false;
    }
    bool held = pad.buttons != 0 || std::any_of(std::begin(pad.analog), std::end(pad.analog), [](uint8_t a) {
                    return a > 0x20;
                });
    for (int vk : {VK_ESCAPE, VK_RETURN, VK_SPACE, VK_LBUTTON, VK_RBUTTON, kToggleKey})
        if (port == 0 && GetAsyncKeyState(vk) < 0) held = true;
    if (!held) g_hold[port] = false;
    return held;
}

}  // namespace panel
