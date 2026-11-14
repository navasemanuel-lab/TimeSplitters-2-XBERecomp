// TimeSplitters 2 launcher: the game's status (disc image or files, the right release), play, the options of settings.toml,
// the profiles and MapMaker maps of the save folder (export, import, delete to a trash folder) and updates from the GitHub
// releases of the project. One window of Dear ImGui on Direct3D 11. TS2.exe starts it when it finds no game (app::kGame.launcher).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <commdlg.h>
#include <d3d11.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <toml++/toml.hpp>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "kernel/disc.h"

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#ifndef TS2_XBE_SHA1
#define TS2_XBE_SHA1 ""
#endif
#ifndef TS2_REPO
#define TS2_REPO "navasemanuel-lab/TimeSplitters-2-XBE"
#endif

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------------------------------------------- strings
std::string U8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string P8(const fs::path& p) { return U8(p.wstring()); }

// ---------------------------------------------------------------------------------------------------------------- state
fs::path g_exe;       // the folder of this exe
fs::path g_settings;  // settings.toml beside TS2.exe
fs::path g_data;      // launcher_data: the launcher's own settings, trash, downloads
std::mutex g_msg_lock;
std::string g_msg;    // the last result, shown at the bottom
void Say(const std::string& s) {
    std::lock_guard<std::mutex> l(g_msg_lock);
    g_msg = s;
}
std::string Said() {
    std::lock_guard<std::mutex> l(g_msg_lock);
    return g_msg;
}

std::atomic<bool> g_busy{false};     // a background job (copy, download) is running
std::atomic<float> g_progress{0};    // 0..1 of that job
std::atomic<bool> g_quit{false};

void Job(const std::function<void()>& fn) {
    if (g_busy.exchange(true)) return;
    g_progress = 0;
    std::thread([fn] {
        fn();
        g_busy = false;
    }).detach();
}

bool GameRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W e{sizeof(e)};
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
        if (_wcsicmp(e.szExeFile, L"TS2.exe") == 0) found = true;
    CloseHandle(snap);
    return found;
}

// Runs a program hidden and waits; the exit code, -1 when it did not start.
int RunHidden(const std::wstring& cmd, const fs::path& cwd = {}) {
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring c = cmd;
    if (!CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return int(code);
}

std::wstring Quote(const fs::path& p) { return L"\"" + p.wstring() + L"\""; }

std::string Sha1(const void* data, size_t n) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) return {};
    unsigned char out[20];
    NTSTATUS st = BCryptHash(alg, nullptr, 0, PUCHAR(data), ULONG(n), out, 20);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (st != 0) return {};
    std::string hex;
    for (unsigned char b : out) {
        hex += "0123456789abcdef"[b >> 4];
        hex += "0123456789abcdef"[b & 15];
    }
    return hex;
}

std::wstring PickFile(bool save, const wchar_t* filter, const wchar_t* title, const std::wstring& name = L"") {
    wchar_t buf[MAX_PATH * 2] = {};
    wcsncpy(buf, name.c_str(), MAX_PATH - 1);
    OPENFILENAMEW o{sizeof(o)};
    o.lpstrFilter = filter;
    o.lpstrFile = buf;
    o.nMaxFile = MAX_PATH * 2;
    o.lpstrTitle = title;
    o.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
    BOOL ok = save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o);
    return ok ? std::wstring(buf) : std::wstring();
}

std::string Size(uintmax_t b) {
    char t[32];
    if (b >= (1u << 30)) snprintf(t, sizeof t, "%.2f GB", double(b) / (1u << 30));
    else if (b >= (1u << 20)) snprintf(t, sizeof t, "%.1f MB", double(b) / (1u << 20));
    else snprintf(t, sizeof t, "%.0f KB", double(b) / 1024);
    return t;
}

// ---------------------------------------------------------------------------------------------------------------- game files
struct GameState {
    enum Kind { Missing, Ok, Other } kind = Missing;
    std::string where, detail;
};
GameState g_game;
const std::string kExpected = TS2_XBE_SHA1;

fs::path FindAssets() {
    fs::path dir = g_exe;
    std::error_code ec;
    for (int i = 0; i < 6; i++) {
        if (fs::is_directory(dir / "assets", ec)) return dir / "assets";
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return g_exe / "assets";
}

bool ReadFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

// the xbe of an image or folder is the release's when its sha1 is
bool ImageXbe(const fs::path& iso, std::string& sha, std::string& error) {
    kernel::Disc disc;
    if (!disc.Open(iso, error)) return false;
    std::vector<uint8_t> xbe;
    if (!disc.Load("default.xbe", xbe)) {
        error = "la imagen no tiene default.xbe";
        return false;
    }
    sha = Sha1(xbe.data(), xbe.size());
    return true;
}

void CheckGame() {
    g_game = {};
    fs::path assets = FindAssets();
    std::error_code ec;
    std::string other;
    std::vector<uint8_t> xbe;
    if (ReadFile(assets / "default.xbe", xbe)) {
        std::string sha = Sha1(xbe.data(), xbe.size());
        if (kExpected.empty() || sha == kExpected) {
            g_game = {GameState::Ok, "Archivos extraídos: " + P8(assets), "default.xbe " + sha};
            return;
        }
        other = "default.xbe de otra versión (" + sha + ")";
    }
    if (fs::is_directory(assets, ec))
        for (auto& e : fs::directory_iterator(assets, ec)) {
            std::wstring ext = e.path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
            if (ext != L".iso") continue;
            std::string sha, error;
            if (!ImageXbe(e.path(), sha, error)) {
                other = P8(e.path().filename()) + ": " + error;
                continue;
            }
            if (kExpected.empty() || sha == kExpected) {
                g_game = {GameState::Ok, "Imagen de disco: " + P8(e.path()), "default.xbe " + sha};
                return;
            }
            other = P8(e.path().filename()) + " es de otra versión del juego (" + sha + ")";
        }
    if (!other.empty()) g_game = {GameState::Other, other, "Este port se hizo con la versión USA (default.xbe " + kExpected + ")."};
    else g_game = {GameState::Missing, "No hay juego en " + P8(assets), "Pon la ISO USA de TimeSplitters 2 con el botón de arriba."};
}

void ImportIso() {
    std::wstring iso = PickFile(false, L"Imagen de Xbox (*.iso)\0*.iso\0Todos\0*.*\0", L"ISO USA de TimeSplitters 2");
    if (iso.empty()) return;
    Job([iso] {
        Say("Comprobando la imagen…");
        std::string sha, error;
        if (!ImageXbe(iso, sha, error)) {
            Say("Esa imagen no sirve: " + error);
            return;
        }
        if (!kExpected.empty() && sha != kExpected) {
            Say("Esa ISO no es la versión USA que usa este port (default.xbe " + sha + ", se esperaba " + kExpected + ").");
            return;
        }
        fs::path assets = FindAssets();
        std::error_code ec;
        fs::create_directories(assets, ec);
        fs::path dest = assets / fs::path(iso).filename();
        Say("Copiando la ISO a " + P8(dest) + "…");
        auto cb = [](LARGE_INTEGER total, LARGE_INTEGER done, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, LPVOID) -> DWORD {
            g_progress = total.QuadPart ? float(double(done.QuadPart) / double(total.QuadPart)) : 0.f;
            return PROGRESS_CONTINUE;
        };
        if (!CopyFileExW(iso.c_str(), dest.c_str(), cb, nullptr, nullptr, 0)) {
            Say("No se pudo copiar la ISO (error " + std::to_string(GetLastError()) + "), ¿hay espacio en el disco?");
            return;
        }
        CheckGame();
        Say("ISO USA instalada. Ya puedes jugar: la primera vez el juego prepara su código y tarda un rato.");
    });
}

// ---------------------------------------------------------------------------------------------------------------- settings
toml::table g_cfg;
bool g_cfg_dirty = false;

void LoadCfg() {
    try {
        g_cfg = toml::parse_file(g_settings.string());
    } catch (...) {
        g_cfg = toml::table{};
    }
    g_cfg_dirty = false;
}
void SaveCfg() {
    std::ofstream f(g_settings, std::ios::binary);
    f << g_cfg;
    g_cfg_dirty = false;
    Say("Opciones guardadas en settings.toml.");
}
bool CfgBool(const char* k, bool d) { return g_cfg[k].value_or(d); }
int64_t CfgInt(const char* k, int64_t d) { return g_cfg[k].value_or(d); }
std::string CfgStr(const char* k, const std::string& d) { return g_cfg[k].value_or(d); }
void SetB(const char* k, bool v) { g_cfg.insert_or_assign(k, v); g_cfg_dirty = true; }
void SetI(const char* k, int64_t v) { g_cfg.insert_or_assign(k, v); g_cfg_dirty = true; }
void SetS(const char* k, const std::string& v) { g_cfg.insert_or_assign(k, v); g_cfg_dirty = true; }

void CheckBox(const char* label, const char* key, bool def, const char* tip = nullptr) {
    bool v = CfgBool(key, def);
    if (ImGui::Checkbox(label, &v)) SetB(key, v);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
}
void SliderInt(const char* label, const char* key, int64_t def, int lo, int hi, const char* fmt = "%d") {
    int v = int(CfgInt(key, def));
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderInt(label, &v, lo, hi, fmt)) SetI(key, v);
}
void ComboStr(const char* label, const char* key, const std::string& def, std::vector<std::pair<const char*, const char*>> items) {
    std::string cur = CfgStr(key, def);
    const char* shown = cur.c_str();
    for (auto& it : items)
        if (cur == it.first) shown = it.second;
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo(label, shown)) {
        for (auto& it : items)
            if (ImGui::Selectable(it.second, cur == it.first)) SetS(key, it.first);
        ImGui::EndCombo();
    }
}
void ComboInt(const char* label, const char* key, int64_t def, std::vector<std::pair<int, const char*>> items) {
    int64_t cur = CfgInt(key, def);
    const char* shown = "?";
    for (auto& it : items)
        if (cur == it.first) shown = it.second;
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo(label, shown)) {
        for (auto& it : items)
            if (ImGui::Selectable(it.second, cur == it.first)) SetI(key, it.first);
        ImGui::EndCombo();
    }
}

void OptionsTab() {
    if (ImGui::Button("Guardar")) SaveCfg();
    ImGui::SameLine();
    if (ImGui::Button("Descartar cambios")) LoadCfg();
    ImGui::SameLine();
    if (g_cfg_dirty) ImGui::TextColored(ImVec4(1, .8f, .3f, 1), "Hay cambios sin guardar");
    if (GameRunning()) ImGui::TextColored(ImVec4(1, .6f, .3f, 1), "El juego está abierto: cierra el juego antes de guardar, o pisará estos cambios al salir.");
    ImGui::Separator();
    ImGui::BeginChild("opts", ImVec2(0, 0));
    if (ImGui::CollapsingHeader("Pantalla", ImGuiTreeNodeFlags_DefaultOpen)) {
        ComboStr("Modo de pantalla", "display_mode", "windowed", {{"fullscreen", "Pantalla completa"}, {"borderless", "Sin bordes"}, {"windowed", "Ventana"}});
        int64_t w = CfgInt("width", 0), h = CfgInt("height", 0);
        std::string cur = w == 0 ? "Automática (la del escritorio)" : std::to_string(w) + " x " + std::to_string(h);
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("Resolución", cur.c_str())) {
            if (ImGui::Selectable("Automática (la del escritorio)", w == 0)) SetI("width", 0), SetI("height", 0);
            const int res[][2] = {{1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160}};
            for (auto& r : res) {
                std::string t = std::to_string(r[0]) + " x " + std::to_string(r[1]);
                if (ImGui::Selectable(t.c_str(), w == r[0] && h == r[1])) SetI("width", r[0]), SetI("height", r[1]);
            }
            ImGui::EndCombo();
        }
        ComboStr("Relación de aspecto", "aspect_ratio", "auto", {{"auto", "La de la pantalla"}, {"4:3", "4:3"}, {"16:9", "16:9"}, {"16:10", "16:10"}, {"21:9", "21:9"}, {"32:9", "32:9"}});
        SliderInt("Escala de render (%)", "render_percent", 100, 25, 200);
        SliderInt("Límite de fotogramas (0 = sin límite)", "fps_cap", 60, 0, 360);
        CheckBox("Sincronización vertical", "vsync", true);
        ComboInt("Interpolación de fotogramas", "frame_interp", 1, {{1, "Desactivada"}, {2, "x2"}, {3, "x3"}, {4, "x4"}});
        CheckBox("HDR10 (si Windows tiene el HDR activado)", "hdr", false);
        CheckBox("Menús, carga y vídeos a pantalla ancha", "menus_wide", true);
        CheckBox("Cinemáticas sin barras negras", "no_letterbox", false);
        CheckBox("HUD sin estirar en pantallas anchas", "hud_unstretch", false);
        CheckBox("Arcos de vida y escudo altos (como TS2 Redux)", "arcs_tall", false);
    }
    if (ImGui::CollapsingHeader("Gráficos")) {
        ComboInt("MSAA", "msaa", 0, {{0, "Desactivado"}, {2, "2x"}, {4, "4x"}, {8, "8x"}});
        ComboInt("Suavizado de bordes", "edge_smoothing", 0, {{0, "Desactivado"}, {1, "FXAA"}, {2, "SMAA"}});
        ComboInt("Filtrado anisotrópico", "anisotropic_filtering", 0, {{0, "Como pide el juego"}, {2, "2x"}, {4, "4x"}, {8, "8x"}, {16, "16x"}});
        ComboInt("Oclusión ambiental", "ambient_occlusion", 0, {{0, "Desactivada"}, {1, "SSAO"}, {2, "GTAO"}});
        SliderInt("Campo de visión (original: 60)", "fov_degrees", 60, 60, 100, "%d grados");
        SliderInt("Brillo", "brightness", 100, 50, 150);
        SliderInt("Saturación", "saturation", 100, 50, 200);
        SliderInt("Contraste", "contrast", 100, 50, 150);
        CheckBox("Niebla", "fog", true);
        CheckBox("Distorsión de calor del fuego", "heat_haze", true);
        CheckBox("Luz de fuego y explosiones con bordes suaves", "smooth_lighting", true);
    }
    if (ImGui::CollapsingHeader("Audio")) {
        SliderInt("Ganancia del audio (dB, 0 = nivel de la consola)", "audio_boost_db", 6, 0, 12);
        ComboInt("Salida de audio (al reiniciar)", "audio_output", 0, {{0, "Automática"}, {1, "Mono"}, {2, "Estéreo 2.0"}, {3, "5.1"}});
        CheckBox("Pausar el juego al perder el foco", "pause_unfocused", true);
    }
    if (ImGui::CollapsingHeader("Controles")) {
        SliderInt("Sensibilidad del ratón (100 = 1x)", "mouse_sensitivity", 100, 10, 400);
        SliderInt("Velocidad vertical del ratón (%)", "mouse_vertical", 100, 25, 200);
        CheckBox("Invertir el eje vertical del ratón", "invert_mouse", false);
        CheckBox("Auto Lookahead (nivela la vista al horizonte)", "auto_lookahead", false);
        CheckBox("Apuntado con giroscopio (DualSense, DualShock 4, Switch Pro)", "gyro_aim", false);
        CheckBox("Iconos de botones según el mando o el teclado", "button_icons", true);
        ImGui::TextDisabled("Las teclas se reasignan dentro del juego, con F1 > Controles.");
    }
    if (ImGui::CollapsingHeader("Juego")) {
        ComboStr("Idioma del juego (al reiniciar)", "language", "english",
                 {{"english", "English"}, {"german", "Deutsch"}, {"french", "Français"}, {"spanish", "Español"}, {"italian", "Italiano"}});
        CheckBox("Cañón de cohetes en todos los niveles de Historia", "story_rockets", false);
        CheckBox("Pistola silenciada en todos los niveles de Historia", "story_pistol", false);
        CheckBox("Default Gun en las listas de armas de Arcade", "default_gun", true);
        CheckBox("Tileset gótico del MapMaker", "gothic_tileset", true);
        CheckBox("MapMaker: mapas de Historia con todos los mandos (co-op)", "mapmaker_coop", true);
        CheckBox("MapMaker: sin límites de memoria", "mapmaker_memory", true);
        CheckBox("Cámara de entrada en Robot Factory", "level_intros", false);
        ImGui::Separator();
        ImGui::TextDisabled("Trucos");
        CheckBox("Vida infinita", "infinite_health", false);
        CheckBox("Munición infinita", "infinite_ammo", false);
        CheckBox("Un solo golpe en Historia", "one_hit", false);
    }
    if (ImGui::CollapsingHeader("Packs de recursos")) {
        CheckBox("Armas de Beta Reborn (resourcepacks/beta_reborn)", "beta_reborn", false);
        CheckBox("Texturas de la Christmas Edition (resourcepacks/xmas)", "xmas_textures", false);
        CheckBox("Sonidos de Homefront (resourcepacks/homefront/sounds)", "homefront_sfx", false);
        ImGui::TextDisabled("Chemical Plant está en la lista de Arcade si resourcepacks/chemical_plant existe.");
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------------------------------------------- saves
// <exe>/hdd/E/UDATA/4553000a: one folder a save. A profile has signon.ts2, a MapMaker map a .mkr, the rest are games.
struct Save {
    fs::path dir;
    std::string id, name, kind;
    uintmax_t size = 0;
    std::string when;
};
std::vector<Save> g_saves;
fs::path UData() { return g_exe / "hdd" / "E" / "UDATA" / "4553000a"; }

std::string MetaName(const fs::path& dir) {
    std::vector<uint8_t> d;
    if (!ReadFile(dir / "SaveMeta.xbx", d) || d.size() < 2) return {};
    std::string s;
    if (d[0] == 0xFF && d[1] == 0xFE) {
        std::wstring w((const wchar_t*)(d.data() + 2), (d.size() - 2) / 2);
        s = U8(w);
    } else {
        s.assign(d.begin(), d.end());
    }
    size_t at = s.find("Name=");
    if (at == std::string::npos) return {};
    size_t e = s.find_first_of("\r\n", at);
    return s.substr(at + 5, e == std::string::npos ? std::string::npos : e - at - 5);
}

void ScanSaves() {
    g_saves.clear();
    std::error_code ec;
    for (auto& e : fs::directory_iterator(UData(), ec)) {
        if (!e.is_directory(ec)) continue;
        Save s;
        s.dir = e.path();
        s.id = P8(e.path().filename());
        s.name = MetaName(e.path());
        bool profile = fs::exists(e.path() / "signon.ts2", ec), map = false;
        for (auto& f : fs::directory_iterator(e.path(), ec)) {
            s.size += f.is_regular_file(ec) ? f.file_size(ec) : 0;
            if (f.path().extension() == ".mkr") map = true;
        }
        s.kind = profile ? "perfil" : map ? "mapa" : "partida";
        auto t = fs::last_write_time(e.path(), ec);
        auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        std::time_t tt = std::chrono::system_clock::to_time_t(sys);
        char buf[32] = {};
        std::tm tmv{};
        localtime_s(&tmv, &tt);
        strftime(buf, sizeof buf, "%d/%m/%Y %H:%M", &tmv);
        s.when = buf;
        if (s.name.empty()) s.name = "(sin nombre)";
        g_saves.push_back(std::move(s));
    }
    std::sort(g_saves.begin(), g_saves.end(), [](const Save& a, const Save& b) { return a.kind == b.kind ? a.name < b.name : a.kind < b.kind; });
}

std::string Stamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &t);
    char b[32];
    strftime(b, sizeof b, "%Y%m%d_%H%M%S", &tmv);
    return b;
}

std::string SafeName(std::string s) {
    for (char& c : s)
        if (strchr("\\/:*?\"<>|", c)) c = '_';
    return s;
}

void ExportSaves(const std::vector<fs::path>& dirs, const std::wstring& title) {
    if (dirs.empty()) return;
    std::wstring def = title + L".zip";
    std::wstring out = PickFile(true, L"Archivo zip (*.zip)\0*.zip\0", L"Guardar copia", def);
    if (out.empty()) return;
    if (fs::path(out).extension() != L".zip") out += L".zip";
    Job([dirs, out] {
        Say("Creando el zip…");
        std::error_code ec;
        fs::remove(out, ec);
        std::wstring cmd = L"tar.exe -a -c -f " + Quote(out) + L" -C " + Quote(dirs[0].parent_path());
        for (auto& d : dirs) cmd += L" " + Quote(d.filename());
        int rc = RunHidden(cmd);
        Say(rc == 0 ? "Copia guardada en " + U8(out) : "No se pudo crear el zip (tar.exe devolvió " + std::to_string(rc) + ").");
    });
}

bool HexId(const std::string& s) {
    if (s.size() != 12) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return isxdigit((unsigned char)c) != 0; });
}

void ImportSaves() {
    std::wstring zip = PickFile(false, L"Copia (*.zip)\0*.zip\0Todos\0*.*\0", L"Importar perfiles o mapas");
    if (zip.empty()) return;
    Job([zip] {
        Say("Leyendo el zip…");
        fs::path tmp = g_data / "import_tmp";
        std::error_code ec;
        fs::remove_all(tmp, ec);
        fs::create_directories(tmp, ec);
        int rc = RunHidden(L"tar.exe -x -f " + Quote(zip) + L" -C " + Quote(tmp));
        if (rc != 0) {
            Say("No se pudo leer el zip (tar.exe devolvió " + std::to_string(rc) + ").");
            return;
        }
        std::vector<fs::path> found;
        if (fs::exists(tmp / "SaveMeta.xbx", ec)) found.push_back(tmp);
        for (auto& e : fs::recursive_directory_iterator(tmp, ec))
            if (e.is_directory(ec) && fs::exists(e.path() / "SaveMeta.xbx", ec)) found.push_back(e.path());
        if (found.empty()) {
            Say("El zip no contiene ningún perfil ni mapa del juego (no hay SaveMeta.xbx).");
            fs::remove_all(tmp, ec);
            return;
        }
        fs::create_directories(UData(), ec);
        std::mt19937_64 rng{std::random_device{}()};
        int n = 0;
        for (auto& src : found) {
            std::string id = P8(src.filename());
            if (!HexId(id) || fs::exists(UData() / id, ec)) {
                do {
                    char b[16];
                    snprintf(b, sizeof b, "%012llX", (unsigned long long)(rng() & 0xFFFFFFFFFFFFull));
                    id = b;
                } while (fs::exists(UData() / id, ec));
            }
            fs::copy(src, UData() / id, fs::copy_options::recursive, ec);
            if (!ec) n++;
        }
        fs::remove_all(tmp, ec);
        Say("Importados " + std::to_string(n) + " elemento(s).");
    });
}

void DeleteSave(const Save& s) {
    std::error_code ec;
    fs::path trash = g_data / "trash";
    fs::create_directories(trash, ec);
    fs::rename(s.dir, trash / (s.id + "_" + Stamp()), ec);
    Say(ec ? "No se pudo mover a la papelera: " + ec.message() : "\"" + s.name + "\" movido a launcher_data/trash (se puede recuperar a mano).");
}

std::string g_pending_delete;

void SavesTab(const char* kind, const char* title, const char* what) {
    static std::string last_scan;
    if (ImGui::Button("Actualizar") || last_scan.empty()) {
        ScanSaves();
        last_scan = "x";
    }
    ImGui::SameLine();
    bool running = GameRunning();
    if (ImGui::Button("Importar…") && !running) ImportSaves();
    ImGui::SameLine();
    if (ImGui::Button("Copia de todos…") && !running) {
        std::vector<fs::path> d;
        for (auto& s : g_saves)
            if (s.kind == kind) d.push_back(s.dir);
        ExportSaves(d, W(std::string("TS2_") + what + "_" + Stamp()));
    }
    ImGui::SameLine();
    if (ImGui::Button("Abrir carpeta")) ShellExecuteW(nullptr, L"open", UData().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (running) ImGui::TextColored(ImVec4(1, .6f, .3f, 1), "El juego está abierto: ciérralo para importar, borrar o copiar con seguridad.");
    ImGui::TextDisabled("%s", title);
    if (ImGui::BeginTable("t", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Nombre", ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn("Carpeta", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Modificado", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Tamaño", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 2.4f);
        ImGui::TableHeadersRow();
        int shown = 0;
        for (auto& s : g_saves) {
            if (s.kind != kind) continue;
            shown++;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.id.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.when.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Size(s.size).c_str());
            ImGui::TableNextColumn();
            ImGui::PushID(s.id.c_str());
            if (ImGui::SmallButton("Exportar")) ExportSaves({s.dir}, W(SafeName(s.name)));
            ImGui::SameLine();
            if (g_pending_delete == s.id) {
                if (ImGui::SmallButton("¿Seguro?") && !running) {
                    DeleteSave(s);
                    g_pending_delete.clear();
                    ScanSaves();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("No")) g_pending_delete.clear();
            } else if (ImGui::SmallButton("Eliminar")) {
                g_pending_delete = s.id;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (!shown) ImGui::TextDisabled("Nada por aquí todavía.");
    }
}

// ---------------------------------------------------------------------------------------------------------------- GitHub updates
struct Json {
    enum T { Null, Str, Num, Bool, Arr, Obj } t = Null;
    std::string s;
    double n = 0;
    std::vector<Json> a;
    std::map<std::string, Json> o;
    const Json& operator[](const char* k) const {
        static Json none;
        auto it = o.find(k);
        return it == o.end() ? none : it->second;
    }
};
struct JsonParser {
    const std::string& s;
    size_t i = 0;
    void ws() { while (i < s.size() && isspace((unsigned char)s[i])) i++; }
    std::string str() {
        std::string r;
        i++;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char c = s[++i];
                if (c == 'n') r += '\n';
                else if (c == 't') r += '\t';
                else if (c == 'r') r += '\r';
                else if (c == 'u' && i + 4 < s.size()) {
                    unsigned v = unsigned(strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16));
                    std::wstring w(1, wchar_t(v));
                    r += U8(w);
                    i += 4;
                } else r += c;
            } else r += s[i];
            i++;
        }
        i++;
        return r;
    }
    Json val() {
        Json j;
        ws();
        if (i >= s.size()) return j;
        char c = s[i];
        if (c == '{') {
            j.t = Json::Obj;
            i++;
            ws();
            while (i < s.size() && s[i] != '}') {
                ws();
                std::string k = str();
                ws();
                i++;  // :
                j.o[k] = val();
                ws();
                if (s[i] == ',') i++;
                ws();
            }
            i++;
        } else if (c == '[') {
            j.t = Json::Arr;
            i++;
            ws();
            while (i < s.size() && s[i] != ']') {
                j.a.push_back(val());
                ws();
                if (s[i] == ',') i++;
                ws();
            }
            i++;
        } else if (c == '"') {
            j.t = Json::Str;
            j.s = str();
        } else if (s.compare(i, 4, "true") == 0) {
            j.t = Json::Bool; j.n = 1; i += 4;
        } else if (s.compare(i, 5, "false") == 0) {
            j.t = Json::Bool; i += 5;
        } else if (s.compare(i, 4, "null") == 0) {
            i += 4;
        } else {
            j.t = Json::Num;
            size_t e = i;
            while (e < s.size() && strchr("+-0123456789.eE", s[e])) e++;
            j.n = atof(s.substr(i, e - i).c_str());
            i = e;
        }
        return j;
    }
};

// the launcher's own settings: which repository, a token for a private one
std::string g_repo = TS2_REPO, g_token;
char g_repo_buf[160], g_token_buf[200];

void LoadLauncherCfg() {
    try {
        auto t = toml::parse_file((g_data / "launcher.toml").string());
        g_repo = t["repo"].value_or(std::string(TS2_REPO));
        g_token = t["token"].value_or(std::string());
    } catch (...) {
    }
    strncpy(g_repo_buf, g_repo.c_str(), sizeof g_repo_buf - 1);
    strncpy(g_token_buf, g_token.c_str(), sizeof g_token_buf - 1);
}
void SaveLauncherCfg() {
    std::error_code ec;
    fs::create_directories(g_data, ec);
    std::ofstream f(g_data / "launcher.toml", std::ios::binary);
    f << toml::table{{"repo", g_repo}, {"token", g_token}};
}

std::string LocalVersion() {
    std::ifstream f(g_exe / "version.txt");
    std::string v;
    std::getline(f, v);
    while (!v.empty() && isspace((unsigned char)v.back())) v.pop_back();
    return v.empty() ? "0.0.0" : v;
}

// GET; the body to `out` as text or to `file` as bytes. A redirect is followed by hand without the token (it goes to
// another host, whose signed address refuses a second kind of authorization).
bool Http(const std::wstring& url0, const std::string& token, const char* accept, std::string* out, const fs::path& file, std::string& error) {
    std::wstring url = url0;
    for (int hop = 0; hop < 5; hop++) {
        URL_COMPONENTS uc{sizeof(uc)};
        wchar_t host[256] = {}, path[2048] = {};
        uc.lpszHostName = host; uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
        uc.dwSchemeLength = 1; uc.dwExtraInfoLength = 1;
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { error = "dirección no válida"; return false; }
        std::wstring full = std::wstring(path) + (uc.dwExtraInfoLength ? std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength) : L"");
        HINTERNET ses = WinHttpOpen(L"TS2-XBE-Launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
        if (!ses) { error = "sin acceso a la red"; return false; }
        HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
        HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", full.c_str(), nullptr, nullptr, nullptr, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
        auto close = [&] { if (req) WinHttpCloseHandle(req); if (con) WinHttpCloseHandle(con); WinHttpCloseHandle(ses); };
        if (!req) { close(); error = "no se pudo conectar"; return false; }
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy);
        std::wstring hdr = L"Accept: " + W(accept) + L"\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        if (!token.empty() && hop == 0) hdr += L"Authorization: Bearer " + W(token) + L"\r\n";
        if (!WinHttpSendRequest(req, hdr.c_str(), DWORD(-1), nullptr, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr)) {
            error = "sin respuesta del servidor (" + std::to_string(GetLastError()) + ")";
            close();
            return false;
        }
        DWORD status = 0, sz = sizeof status;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sz, nullptr);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            wchar_t loc[2048] = {};
            DWORD ls = sizeof loc;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, nullptr, loc, &ls, nullptr);
            url = loc;
            close();
            continue;
        }
        if (status != 200) {
            error = status == 404 ? "no existe, o el repositorio es privado y falta el token (404)" : status == 401 ? "token no válido (401)" : "el servidor respondió " + std::to_string(status);
            close();
            return false;
        }
        DWORD ln = 0, lsz = sizeof ln;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &ln, &lsz, nullptr);
        std::ofstream f;
        if (!file.empty()) f.open(file, std::ios::binary);
        uint64_t got = 0;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
            std::string buf(avail, 0);
            DWORD read = 0;
            if (!WinHttpReadData(req, buf.data(), avail, &read) || read == 0) break;
            got += read;
            if (out) out->append(buf.data(), read);
            else f.write(buf.data(), read);
            if (ln) g_progress = float(double(got) / double(ln));
        }
        close();
        return true;
    }
    error = "demasiadas redirecciones";
    return false;
}

struct Release {
    bool have = false;
    std::string tag, name, notes, asset_name, asset_url;
    uint64_t asset_size = 0;
};
Release g_release;
std::string g_update_state;  // what the tab says
bool g_downloaded = false;

void CheckUpdate() {
    Job([] {
        g_update_state = "Buscando…";
        std::string body, error;
        std::wstring url = L"https://api.github.com/repos/" + W(g_repo) + L"/releases/latest";
        if (!Http(url, g_token, "application/vnd.github+json", &body, {}, error)) {
            g_update_state = "No se pudo consultar GitHub: " + error;
            return;
        }
        JsonParser p{body};
        Json j = p.val();
        Release r;
        r.tag = j["tag_name"].s;
        r.name = j["name"].s;
        r.notes = j["body"].s;
        for (auto& a : j["assets"].a) {
            std::string n = a["name"].s;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".zip") == 0) {
                r.asset_name = n;
                r.asset_url = a["url"].s;  // the API's address, which a private repository serves with the token
                r.asset_size = uint64_t(a["size"].n);
                break;
            }
        }
        r.have = !r.tag.empty();
        g_release = r;
        g_downloaded = false;
        if (!r.have) g_update_state = "El repositorio no tiene versiones publicadas todavía.";
        else if (r.asset_name.empty()) g_update_state = "La versión " + r.tag + " no trae ningún .zip.";
        else {
            std::string local = LocalVersion(), remote = r.tag;
            if (!remote.empty() && (remote[0] == 'v' || remote[0] == 'V')) remote.erase(0, 1);
            g_update_state = remote == local ? "Estás al día (" + local + ")." : "Hay una versión nueva: " + r.tag + " (tienes " + local + ").";
        }
    });
}

void DownloadUpdate() {
    Job([] {
        Release r = g_release;
        std::error_code ec;
        fs::path dir = g_data / "updates";
        fs::create_directories(dir, ec);
        fs::path zip = dir / r.asset_name;
        Say("Descargando " + r.asset_name + " (" + Size(r.asset_size) + ")…");
        std::string error;
        if (!Http(W(r.asset_url), g_token, "application/octet-stream", nullptr, zip, error)) {
            Say("La descarga falló: " + error);
            return;
        }
        g_downloaded = true;
        Say("Descargada. Pulsa Instalar: el launcher se cierra, copia los archivos nuevos y se vuelve a abrir.");
    });
}

// extracts the zip, then a batch waits for this exe to end, copies over the install (keeping the settings, saves, the game
// and the logs) and starts the launcher again
void InstallUpdate() {
    if (GameRunning()) {
        Say("Cierra el juego antes de actualizar.");
        return;
    }
    fs::path zip = g_data / "updates" / g_release.asset_name;
    fs::path stage = g_data / "updates" / "stage";
    Job([zip, stage] {
        std::error_code ec;
        fs::remove_all(stage, ec);
        fs::create_directories(stage, ec);
        Say("Descomprimiendo…");
        if (RunHidden(L"tar.exe -x -f " + Quote(zip) + L" -C " + Quote(stage)) != 0) {
            Say("No se pudo descomprimir la actualización.");
            return;
        }
        // a zip with one top folder is that folder's contents
        fs::path root = stage;
        std::vector<fs::path> top;
        for (auto& e : fs::directory_iterator(stage, ec)) top.push_back(e.path());
        if (top.size() == 1 && fs::is_directory(top[0], ec)) root = top[0];
        std::string tag = g_release.tag;
        if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
        fs::path bat = g_data / "apply_update.bat";
        std::ofstream b(bat, std::ios::binary);
        b << "@echo off\r\n"
             "timeout /t 2 /nobreak >nul\r\n"
             ":wait\r\n"
             "tasklist /fi \"imagename eq TS2Launcher.exe\" 2>nul | find /i \"TS2Launcher.exe\" >nul && (timeout /t 1 /nobreak >nul & goto wait)\r\n"
             "robocopy \"" << P8(root) << "\" \"" << P8(g_exe) << "\" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /XD assets hdd logs CrashDumps launcher_data /XF settings.toml >nul\r\n"
             "echo " << tag << "> \"" << P8(g_exe / "version.txt") << "\"\r\n"
             "rd /s /q \"" << P8(stage) << "\" 2>nul\r\n"
             "start \"\" \"" << P8(g_exe / "TS2Launcher.exe") << "\"\r\n";
        b.close();
        ShellExecuteW(nullptr, L"open", L"cmd.exe", (L"/c \"" + bat.wstring() + L"\"").c_str(), g_exe.c_str(), SW_HIDE);
        g_quit = true;
    });
}

void UpdatesTab() {
    ImGui::Text("Versión instalada: %s", LocalVersion().c_str());
    ImGui::Text("Repositorio:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(380);
    if (ImGui::InputText("##repo", g_repo_buf, sizeof g_repo_buf)) g_repo = g_repo_buf;
    ImGui::Text("Token de GitHub (solo para repositorios privados):");
    ImGui::SetNextItemWidth(380);
    if (ImGui::InputText("##tok", g_token_buf, sizeof g_token_buf, ImGuiInputTextFlags_Password)) g_token = g_token_buf;
    if (ImGui::Button("Guardar")) SaveLauncherCfg();
    ImGui::SameLine();
    if (ImGui::Button("Usar el token de gh")) {
        std::wstring tmp = (g_data / "gh_token.txt").wstring();
        std::error_code ec;
        fs::create_directories(g_data, ec);
        RunHidden(L"cmd.exe /c gh auth token > " + Quote(tmp));
        std::ifstream f(tmp);
        std::string t;
        std::getline(f, t);
        f.close();
        fs::remove(tmp, ec);
        while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
        if (!t.empty()) {
            g_token = t;
            strncpy(g_token_buf, t.c_str(), sizeof g_token_buf - 1);
            SaveLauncherCfg();
            Say("Token de gh guardado en launcher_data/launcher.toml.");
        } else {
            Say("gh no devolvió ningún token (¿está instalado y con la sesión iniciada?).");
        }
    }
    ImGui::Separator();
    if (ImGui::Button("Buscar actualizaciones")) CheckUpdate();
    if (!g_update_state.empty()) ImGui::TextWrapped("%s", g_update_state.c_str());
    if (g_release.have && !g_release.asset_name.empty()) {
        ImGui::Text("%s  %s  (%s)", g_release.tag.c_str(), g_release.name.c_str(), Size(g_release.asset_size).c_str());
        if (!g_release.notes.empty()) {
            ImGui::BeginChild("notes", ImVec2(0, 150), true);
            ImGui::TextWrapped("%s", g_release.notes.c_str());
            ImGui::EndChild();
        }
        if (!g_downloaded) {
            if (ImGui::Button("Descargar")) DownloadUpdate();
        } else if (ImGui::Button("Instalar y reiniciar el launcher")) {
            InstallUpdate();
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------- play tab
void Play() {
    fs::path exe = g_exe / "TS2.exe";
    if (!fs::exists(exe)) {
        Say("No encuentro TS2.exe junto al launcher.");
        return;
    }
    if (GameRunning()) {
        Say("El juego ya está abierto.");
        return;
    }
    if (intptr_t(ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, g_exe.c_str(), SW_SHOWNORMAL)) > 32) g_quit = true;
    else Say("No se pudo abrir TS2.exe.");
}

void PlayTab() {
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted("TimeSplitters 2");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();
    ImGui::TextDisabled("Port nativo de la versión USA de Xbox  -  versión %s", LocalVersion().c_str());
    ImGui::Dummy(ImVec2(0, 8));
    ImVec4 col = g_game.kind == GameState::Ok ? ImVec4(.4f, 1, .5f, 1) : g_game.kind == GameState::Other ? ImVec4(1, .6f, .3f, 1) : ImVec4(1, .4f, .4f, 1);
    ImGui::TextColored(col, g_game.kind == GameState::Ok ? "Juego listo" : g_game.kind == GameState::Other ? "Versión equivocada" : "Falta el juego");
    ImGui::TextWrapped("%s", g_game.where.c_str());
    ImGui::TextDisabled("%s", g_game.detail.c_str());
    ImGui::Dummy(ImVec2(0, 6));
    if (ImGui::Button("Seleccionar ISO USA…", ImVec2(210, 34))) ImportIso();
    ImGui::SameLine();
    if (ImGui::Button("Volver a comprobar", ImVec2(170, 34))) CheckGame();
    ImGui::Dummy(ImVec2(0, 14));
    ImGui::BeginDisabled(g_game.kind != GameState::Ok || g_busy);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.15f, .55f, .25f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.2f, .65f, .3f, 1));
    if (ImGui::Button("JUGAR", ImVec2(210, 52))) Play();
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 14));
    if (ImGui::Button("Abrir la carpeta del juego")) ShellExecuteW(nullptr, L"open", g_exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (ImGui::Button("Ver el registro (log)")) ShellExecuteW(nullptr, L"open", (g_exe / "logs").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextWrapped("La primera vez que juegas, el juego prepara su código para tu PC con tu propia copia del juego: tarda unos minutos y solo pasa una vez. "
                       "En el juego, F1 abre el panel de ajustes.");
}

// ---------------------------------------------------------------------------------------------------------------- Direct3D 11
ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

void MakeTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}
bool InitD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx))) return false;
    MakeTarget();
    return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_dev && wp != SIZE_MINIMIZED) {
            if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
            g_swap->ResizeBuffers(0, UINT(LOWORD(lp)), UINT(HIWORD(lp)), DXGI_FORMAT_UNKNOWN, 0);
            MakeTarget();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Style(float scale) {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.FrameRounding = 5; s.GrabRounding = 5; s.TabRounding = 5; s.ChildRounding = 5;
    s.FramePadding = ImVec2(10, 6); s.ItemSpacing = ImVec2(10, 8);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(.09f, .10f, .12f, 1);
    c[ImGuiCol_Tab] = ImVec4(.14f, .16f, .20f, 1);
    c[ImGuiCol_TabHovered] = ImVec4(.25f, .45f, .75f, 1);
    c[ImGuiCol_TabActive] = ImVec4(.20f, .38f, .66f, 1);
    c[ImGuiCol_Button] = ImVec4(.20f, .38f, .66f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(.26f, .48f, .82f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(.15f, .17f, .21f, 1);
    c[ImGuiCol_Header] = ImVec4(.17f, .20f, .27f, 1);
    c[ImGuiCol_CheckMark] = ImVec4(.45f, .75f, 1, 1);
    c[ImGuiCol_SliderGrab] = ImVec4(.35f, .60f, .95f, 1);
    s.ScaleAllSizes(scale);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetProcessDPIAware();
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    g_exe = fs::path(buf).parent_path();
    g_settings = g_exe / "settings.toml";
    g_data = g_exe / "launcher_data";
    LoadCfg();
    LoadLauncherCfg();
    CheckGame();
    ScanSaves();

    UINT dpi = GetDpiForSystem();
    float scale = float(dpi) / 96.f;
    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, WndProc, 0, 0, inst, LoadIconW(inst, MAKEINTRESOURCEW(1)), nullptr, nullptr, nullptr, L"TS2Launcher", nullptr};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TimeSplitters 2 - Launcher", WS_OVERLAPPEDWINDOW, 100, 80, int(980 * scale), int(680 * scale), nullptr, nullptr, inst, nullptr);
    if (!InitD3D(hwnd)) {
        MessageBoxW(nullptr, L"No se pudo iniciar Direct3D 11.", L"TS2 Launcher", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    // Segoe UI covers the accents of every language; the default font only the basic ones
    static const ImWchar ranges[] = {0x20, 0x17F, 0x2010, 0x2026, 0};
    if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.f * scale, nullptr, ranges)) io.Fonts->AddFontDefault();
    Style(scale);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    MSG msg{};
    while (!g_quit) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) g_quit = true;
        }
        if (g_quit) break;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        float footer = ImGui::GetFrameHeightWithSpacing() * (g_busy ? 3.2f : 2.2f);
        ImGui::BeginChild("body", ImVec2(0, -footer));
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Jugar")) { PlayTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Opciones")) { OptionsTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Perfiles")) { SavesTab("perfil", "Los perfiles del juego (partidas, niveles y personajes desbloqueados).", "perfiles"); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Mapas MapMaker")) { SavesTab("mapa", "Los mapas que has creado o importado en el MapMaker.", "mapas"); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Actualizaciones")) { UpdatesTab(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        ImGui::Separator();
        if (g_busy) ImGui::ProgressBar(g_progress, ImVec2(-1, 0));
        ImGui::TextWrapped("%s", Said().c_str());
        ImGui::End();
        ImGui::Render();
        const float clear[4] = {.09f, .10f, .12f, 1};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g_rtv) g_rtv->Release();
    if (g_swap) g_swap->Release();
    if (g_ctx) g_ctx->Release();
    if (g_dev) g_dev->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, inst);
    return 0;
}
