#include "app/app.h"
#include "common/button_icons.h"
#include "common/controls.h"
#include "common/mouse.h"
#include "common/options.h"
#include "common/lang.h"
#include "common/panel.h"
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "core/input.h"
#include "gpu/backend.h"

void InitSounds();  // game/sounds.cpp
void InitPakReplace();  // game/pak_replace.cpp
void Ts1BarsDraw();     // game/ammo_hud.cpp
void RequestDeleteProfile();  // game/menu.cpp
std::string DeleteProfilePrompt();  // game/menu.cpp: the profile the question asks to delete, empty when none

namespace game {
Options& Opt() {
    static Options o;
    return o;
}
namespace {
void Window(void* hwnd) { mouse::Start(hwnd); }
bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    if (panel::Message(msg, wp, lp, result)) return true;
    // the Delete key asks the profile screen to delete the chosen profile (not on key repeat)
    if (msg == 0x0100 && wp == 0x2E && !(lp & 0x40000000)) RequestDeleteProfile();
    return mouse::Message(msg, wp, lp, result);
}
// each frame inside the host's ImGui frame. The host hands window messages to ImGui first, and ImGui answers
// WM_SETCURSOR with its arrow: the cursor is the mouse module's (hidden in play, an arrow in the menus)
void Ui(double fps) {
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    // menus, loading and movies fill a wide output (a 4:3 output has no bars to remove): no pillarboxing
    app::g_pillarbox_menus = false;
    panel::Ui(fps);
    if (std::string del = DeleteProfilePrompt(); !del.empty()) {
        // the question over the profile list (game/menu.cpp holds the list until it is answered): the game's own wording,
        // its button glyphs (0x0B, 1 to 5) written out as the pad's letters
        std::string text;
        for (size_t i = 0; i < del.size(); i++) {
            if (del[i] == 0x0B && i + 1 < del.size()) {
                static const char* k[] = {"?", "B", "A", "Y", "X", "Back"};
                int g = del[++i] - '0';
                text += std::string("(") + ((g >= 0 && g <= 5) ? k[g] : "?") + ")";
            } else
                text += del[i];
        }
        std::vector<std::string> lines;
        for (size_t at = 0;;) {
            size_t e = text.find(0x0A, at);
            lines.push_back(text.substr(at, e == std::string::npos ? std::string::npos : e - at));
            if (e == std::string::npos) break;
            at = e + 1;
        }
        ImVec2 size = ImGui::GetIO().DisplaySize;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();
        float s = size.y / 720.0f * 1.6f, fs = ImGui::GetFontSize() * s, lh = fs * 1.25f;
        float tw = 0;
        for (auto& l : lines) tw = std::max(tw, ImGui::CalcTextSize(l.c_str()).x * s);
        float w = tw + 70 * s, h = lh * lines.size() + 44 * s;
        ImVec2 p0(size.x / 2 - w / 2, size.y / 2 - h / 2), p1(size.x / 2 + w / 2, size.y / 2 + h / 2);
        dl->AddRectFilled(p0, p1, IM_COL32(8, 8, 24, 235), 10 * s);
        dl->AddRect(p0, p1, IM_COL32(70, 130, 255, 255), 10 * s, 0, 2.0f);
        float y = p0.y + 22 * s;
        for (auto& l : lines) {
            float lw = ImGui::CalcTextSize(l.c_str()).x * s;
            dl->AddText(font, fs, ImVec2(size.x / 2 - lw / 2, y), IM_COL32(255, 255, 255, 255), l.c_str());
            y += lh;
        }
    }
    Ts1BarsDraw();
    button_icons::Tick();
    gpu::g_no_fog.store(Opt().render_all, std::memory_order_relaxed);
    gpu::g_crop_letterbox.store(Opt().no_letterbox, std::memory_order_relaxed);
}
// the settings file's keys: the mouse's, and each control's key_<id>
std::vector<app::Option> MakeOptions() {
    std::vector<app::Option> o{{"invert_mouse", &Opt().invert_mouse, nullptr},
                               {"auto_lookahead", &Opt().auto_lookahead, nullptr},
                               {"turn_sway", nullptr, &Opt().turn_sway},
                               {"mouse_sensitivity", nullptr, &Opt().mouse_sensitivity},
                               {"mouse_vertical", nullptr, &Opt().mouse_vertical},
                               {"fov_degrees", nullptr, &Opt().fov},
                               {"gyro_aim", &Opt().gyro_aim, nullptr},
                               {"gyro_speed", nullptr, &Opt().gyro_speed},
                               {"ui_language", nullptr, &Opt().ui_lang},
                               {"show_stats", &Opt().show_stats, nullptr},
                               {"log_stats", &Opt().log_stats, nullptr},
                               {"pad_look_h", nullptr, &Opt().pad_h},
                               {"pad_look_v", nullptr, &Opt().pad_v},
                               {"pad_aim_h", nullptr, &Opt().pad_aim_h},
                               {"pad_aim_v", nullptr, &Opt().pad_aim_v},
                               {"mouse_aim_h", nullptr, &Opt().mouse_aim_h},
                               {"mouse_aim_v", nullptr, &Opt().mouse_aim_v},
                               {"menus_wide", &Opt().menus_wide, nullptr},
                               {"hud_unstretch", &Opt().hud_unstretch, nullptr},
                               {"arcs_tall", &Opt().arcs_tall, nullptr},
                               {"ts1_bars", &Opt().ts1_bars, nullptr},
                               {"button_icons", &Opt().button_icons, nullptr},
                               {"button_icons_style", nullptr, &Opt().button_icons_style},
                               {"reticle_always", &Opt().reticle_always, nullptr},
                               {"no_letterbox", &Opt().no_letterbox, nullptr},
                               {"level_intros", &Opt().level_intros, nullptr},
                               {"story_rockets", &Opt().story_rockets, nullptr},
                               {"story_pistol", &Opt().story_pistol, nullptr},
                              {"beta_reborn", &Opt().beta_reborn, nullptr},
                              {"xmas_textures", &Opt().xmas_textures, nullptr},
                              {"gothic_tileset", &Opt().gothic_tileset, nullptr},
                              {"default_gun", &Opt().default_gun, nullptr},
                              {"mapmaker_coop", &Opt().mapmaker_coop, nullptr},
                              {"mapmaker_memory", &Opt().mapmaker_memory, nullptr},
                              {"homefront_sfx", &Opt().homefront_sfx, nullptr},
                              {"infinite_health", &Opt().infinite_health, nullptr},
                              {"infinite_ammo", &Opt().infinite_ammo, nullptr},
                              {"one_hit", &Opt().one_hit, nullptr}};
    static std::string names[Options::kKeys], names2[Options::kKeys];
    for (int i = 0; i < controls::kBindingCount && i < Options::kKeys; i++) {
        names[i] = std::string("key_") + controls::kBindings[i].id;
        o.push_back({names[i].c_str(), nullptr, &Opt().keys[i]});
        names2[i] = std::string("key2_") + controls::kBindings[i].id;
        o.push_back({names2[i].c_str(), nullptr, &Opt().keys2[i]});
    }
    return o;
}
void Init() { InitSounds(); InitPakReplace(); }
}  // namespace
}  // namespace game

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"TimeSplitters 2",               // window and splash
    .short_title = L"TS2",               // message boxes
    .name = L"TS2",                      // window classes, crash dumps, handover event
    .log = "game",                        // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "TS2"}},  // each recompiled xbe and its exe, for title launches
    .options = game::MakeOptions(),       // game settings kept in the settings file
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = game::Init,                   // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = game::Window,                    // the game window is up
    .message = game::Message,                   // window messages first; true takes one
    .ui = game::Ui,                       // ImGui drawing each frame
    .pad = controls::Pad,                       // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
    .launcher = L"TS2Launcher.exe",        // started when the game is not found
};
