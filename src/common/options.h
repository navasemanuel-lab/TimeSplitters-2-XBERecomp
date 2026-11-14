#pragma once
#include <algorithm>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <iterator>
#include <string>

// the port's own settings, in the settings file through app::kGame.options
namespace game {

struct Options {
    // the mouse turns the view itself: 100 is 0.1 degrees a count
    uint32_t mouse_sensitivity = 100;
    uint32_t mouse_vertical = 100;  // up and down at this percent of the sideways speed
    bool invert_mouse = false;
    // the game's Auto Lookahead: the view levels itself to the horizon as the player moves; off unless asked for (game/aim.cpp)
    bool auto_lookahead = false;
    // the crosshair's sway as the view turns (the game's free aim), in percent of the game's own (0: none, as a modern game; game/aim.cpp)
    uint32_t turn_sway = 0;
    // percent of the game's own speeds: the pad's stick, and the mouse's on top of its sensitivity, normally and aiming
    uint32_t pad_h = 100, pad_v = 100, pad_aim_h = 100, pad_aim_v = 100;
    uint32_t mouse_aim_h = 100, mouse_aim_v = 100;
    // player 1's controller's gyro turns the view as the mouse does, beside the right stick (controls.cpp GyroAim)
    bool gyro_aim = false;
    uint32_t gyro_speed = 200;
    uint32_t ui_lang = 0;       // the F1 panel's language: 0 Spanish, 1 English, then the lang/*.txt files (common/lang.cpp)
    bool show_fps = false;     // the frames a second in the top left corner
    bool show_stats = false;    // the performance overlay: where each frame's time goes (panel.cpp DrawStats)
    bool log_stats = false;     // the same numbers in the log every ten seconds, each hitch, a summary every five minutes
    uint32_t fov = 60;          // the view's vertical angle in degrees: the game's 60 up to 100
    bool arcs_tall = false;     // the health and shield arcs raised with the screen shape as TS2 Redux has them (false: the original size)
    // debug: the whole level drawn from anywhere, at once: every room the portals hide, the objects in no room or culled by
    // the view's test, no far plane at 1000 and no fog (game/free_cam.cpp)
    bool render_all = false;
    bool no_letterbox = false;  // the cutscenes' black bars not drawn (game/ammo_hud.cpp)
    bool reticle_always = false; // the crosshair shown without aiming (game/ammo_hud.cpp)
    bool ts1_bars = false;     // a TimeSplitters 1 style health bar instead of the arcs (game/ammo_hud.cpp)
    bool hud_unstretch = false; // the HUD's pictures kept at the shape they were made for, at the screen's edges (game/ammo_hud.cpp)
    // the prompts' buttons as the input in use shows them (common/button_icons.cpp); the style 0 follows the last input used,
    // else one of button_icons::Style
    bool button_icons = true;
    uint32_t button_icons_style = 0;
    bool menus_wide = true;    // the menus, loading and movies stretched to the output's shape, not 4:3 with bars
    bool story_rockets = false; // the Rocket Launcher in every Story level (game/story_rockets.cpp)
    bool story_pistol = false;  // the Silenced Pistol in every Story level (game/story_rockets.cpp)
    bool beta_reborn = false;   // gun models of the Beta Reborn PS2 mod from resourcepacks/beta_reborn/ (game/pak_replace.cpp)
    bool xmas_textures = false;  // the Christmas Edition textures from resourcepacks/xmas/ (game/pak_replace.cpp), read at start
    bool default_gun = true;     // the cut Default Gun (a Desert Eagle) in the Arcade weapon lists (game/unused_content.cpp)
    bool gothic_tileset = true;  // the MapMaker's cut Gothic tileset (game/unused_content.cpp)
    bool mapmaker_coop = true;   // MapMaker Story maps played by every controller connected, up to four (game/unused_content.cpp)
    bool mapmaker_memory = true; // the MapMaker's memory caps lifted (game/unused_content.cpp)
    bool homefront_sfx = false;  // the Homefront sounds of resourcepacks/homefront/sounds (game/sound_pack.cpp)
    bool level_intros = false;   // Robot Factory opens with its cut camera sequence (game/level_intros.cpp)
    bool one_hit = false;        // any damage kills in Story, enemies too (game/infinite_health.cpp)
    bool infinite_ammo = false;   // the ammunition kept full (game/infinite_health.cpp)
    bool infinite_health = false; // the players' health kept full (game/infinite_health.cpp)
    // the keys of controls::kBindings: 0xFFFFFFFF the default, 0 none, else a virtual key
    static constexpr int kKeys = 32;
    uint32_t keys[kKeys];   // the primary key of each
    uint32_t keys2[kKeys];  // the alternate key of each
    Options() {
        std::fill(std::begin(keys), std::end(keys), 0xFFFFFFFFu);
        std::fill(std::begin(keys2), std::end(keys2), 0xFFFFFFFFu);
    }
};

Options& Opt();

}  // namespace game
