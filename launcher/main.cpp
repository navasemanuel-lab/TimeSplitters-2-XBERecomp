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
#include <wincodec.h>
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
#include <unordered_map>
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

float g_scale = 1.f;  // the window's scale (DPI)
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

// ---- languages. The texts are written in Spanish (the source language) and T() gives each one in the language chosen
// (the foot bar's selector): 0 Spanish, 1 English (built in), then every lang/<name>.txt beside the exe (lines
// "Spanish text = translation", the same files as the game's F1 panel; launcher_data/launcher_template.txt lists the
// launcher's texts for a translator to copy). A text with no translation shows as written. The first start picks the
// language of Windows when there is one for it, English when not.
struct TrPair {
    const char* es;
    const char* en;
};
const TrPair kEnglish[] = {
    {"%d grados", "%d degrees"},
    {"Versión instalada: %s", "Installed version: %s"},
    {"Port nativo de la versión USA de Xbox  -  versión %s", "Native port of the USA Xbox version  -  version %s"},
    {"la imagen no tiene default.xbe", "the image has no default.xbe"},
    {"Archivos extraídos: ", "Extracted files: "},
    {"default.xbe de otra versión (", "default.xbe of another version ("},
    {"Imagen de disco: ", "Disc image: "},
    {" es de otra versión del juego (", " is from another version of the game ("},
    {"Este port se hizo con la versión USA (default.xbe ", "This port was made with the USA version (default.xbe "},
    {"No hay juego en ", "No game in "},
    {"Pon la ISO USA de TimeSplitters 2 con el botón de arriba.", "Add your TimeSplitters 2 USA disc copy (ISO) with the button above."},
    {"Comprobando la imagen…", "Checking the image…"},
    {"Esa imagen no sirve: ", "That image does not work: "},
    {"Esa ISO no es la versión USA que usa este port (default.xbe ", "That ISO is not the USA version this port uses (default.xbe "},
    {", se esperaba ", ", expected "},
    {"Copiando la ISO a ", "Copying the ISO to "},
    {"No se pudo copiar la ISO (error ", "The ISO could not be copied (error "},
    {"), ¿hay espacio en el disco?", "), is there space on the disk?"},
    {"ISO USA instalada. Ya puedes jugar.", "USA disc copy installed. You can play now."},
    {"Opciones guardadas en settings.toml.", "Options saved in settings.toml."},
    {"Guardar", "Save"},
    {"Descartar cambios", "Discard changes"},
    {"Hay cambios sin guardar", "There are unsaved changes"},
    {"El juego está abierto: cierra el juego antes de guardar, o pisará estos cambios al salir.", "The game is open: close it before saving, or it will overwrite these changes when it exits."},
    {"Pantalla", "Display"},
    {"Gráficos", "Graphics"},
    {"Audio", "Audio"},
    {"Controles", "Controls"},
    {"Juego", "Game"},
    {"Packs de recursos", "Resource packs"},
    {"Modo de pantalla", "Display mode"},
    {"Pantalla completa", "Fullscreen"},
    {"Sin bordes", "Borderless"},
    {"Ventana", "Windowed"},
    {"Automática (la del escritorio)", "Automatic (the desktop's)"},
    {"Resolución", "Resolution"},
    {"Relación de aspecto", "Aspect ratio"},
    {"La de la pantalla", "The screen's own"},
    {"Resolución interna (máx. 5K)", "Internal resolution (5K at most)"},
    {"Nativa (la de la pantalla)", "Native (the screen's own)"},
    {"Original (480p)", "Original (480p)"},
    {"Límite de fotogramas (0 = sin límite)", "Frame rate cap (0 = no cap)"},
    {"Sincronización vertical", "V-Sync"},
    {"HDR10 (si Windows tiene el HDR activado)", "HDR10 (if Windows has HDR on)"},
    {"Menús, carga y vídeos a pantalla ancha", "Menus, loading screens and movies in wide screen"},
    {"Cinemáticas sin barras negras", "Cutscenes without black bars"},
    {"Arcos de vida y escudo altos (como TS2 Redux)", "Tall health and shield arcs (as in TS2 Redux)"},
    {"Desactivado", "Off"},
    {"Suavizado de bordes", "Edge smoothing"},
    {"Filtrado anisotrópico", "Anisotropic filtering"},
    {"Como pide el juego", "As the game asks"},
    {"Campo de visión (original: 60)", "Field of view (original: 60)"},
    {"Brillo", "Brightness"},
    {"Saturación", "Saturation"},
    {"Contraste", "Contrast"},
    {"Niebla", "Fog"},
    {"Distorsión de calor del fuego", "Heat shimmer around fire"},
    {"Luz de fuego y explosiones con bordes suaves", "Fire and explosion light with soft edges"},
    {"Volumen general (%)", "Master volume (%)"},
    {"Salida de audio (al reiniciar)", "Audio output (on restart)"},
    {"Automática", "Automatic"},
    {"Mono", "Mono"},
    {"Estéreo 2.0", "Stereo 2.0"},
    {"Pausar el juego al perder el foco", "Pause the game when it loses focus"},
    {"Sensibilidad del ratón (100 = 1x)", "Mouse sensitivity (100 = 1x)"},
    {"Velocidad vertical del ratón (%)", "Mouse vertical speed (%)"},
    {"Apuntado con giroscopio (DualSense, DualShock 4, Switch Pro)", "Gyro aiming (DualSense, DualShock 4, Switch Pro)"},
    {"Iconos de botones según el mando o el teclado", "Button icons that follow the pad or keyboard"},
    {"Las teclas se reasignan dentro del juego, con F1 > Controles.", "Keys are rebound in the game, with F1 > Controls."},
    {"Idioma del juego (al reiniciar)", "Game language (on restart)"},
    {"Cañón de cohetes en todos los niveles de Historia", "Rocket Launcher in every Story level"},
    {"Default Gun en las listas de armas de Arcade", "Default Gun in the Arcade weapon lists"},
    {"Tileset gótico del MapMaker", "MapMaker Gothic tileset"},
    {"MapMaker: mapas de Historia con todos los mandos (co-op)", "MapMaker: Story maps with every pad (co-op)"},
    {"MapMaker: sin límites de memoria", "MapMaker: no memory limits"},
    {"Cámara de entrada en Robot Factory", "Intro camera in Robot Factory"},
    {"Trucos", "Cheats"},
    {"Vida infinita", "Infinite health"},
    {"Un solo golpe en Historia", "One-hit kills in Story"},
    {"TS2 Beta Weapons (resourcepacks/beta_weapons)", "TS2 Beta Weapons (resourcepacks/beta_weapons)"},
    {"(sin nombre)", "(no name)"},
    {"Creando el zip…", "Creating the zip…"},
    {"Copia guardada en ", "Copy saved in "},
    {"No se pudo crear el zip (tar.exe devolvió ", "The zip could not be created (tar.exe returned "},
    {"Leyendo el zip…", "Reading the zip…"},
    {"No se pudo leer el zip (tar.exe devolvió ", "The zip could not be read (tar.exe returned "},
    {"El zip no contiene ningún perfil ni mapa del juego (no hay SaveMeta.xbx).", "The zip holds no profile or map of the game (there is no SaveMeta.xbx)."},
    {"Importados ", "Imported "},
    {" elemento(s).", " item(s)."},
    {"No se pudo mover a la papelera: ", "Could not move to the trash: "},
    {"\" movido a launcher_data/trash (se puede recuperar a mano).", "\" moved to launcher_data/trash (it can be recovered by hand)."},
    {"Actualizar", "Refresh"},
    {"Importar…", "Import…"},
    {"Copia de todos…", "Back up all…"},
    {"Abrir carpeta", "Open folder"},
    {"El juego está abierto: ciérralo para importar, borrar o copiar con seguridad.", "The game is open: close it to import, delete or copy safely."},
    {"Nombre", "Name"},
    {"Carpeta", "Folder"},
    {"Modificado", "Modified"},
    {"Tamaño", "Size"},
    {"Exportar", "Export"},
    {"¿Seguro?", "Sure?"},
    {"No", "No"},
    {"Eliminar", "Delete"},
    {"Nada por aquí todavía.", "Nothing here yet."},
    {"dirección no válida", "invalid address"},
    {"sin acceso a la red", "no network access"},
    {"no se pudo conectar", "could not connect"},
    {"sin respuesta del servidor (", "no response from the server ("},
    {"no existe, o el repositorio es privado y falta el token (404)", "does not exist, or the repository is private and the token is missing (404)"},
    {"token no válido (401)", "invalid token (401)"},
    {"el servidor respondió ", "the server answered "},
    {"demasiadas redirecciones", "too many redirects"},
    {"Buscando…", "Searching…"},
    {"No se pudo consultar GitHub: ", "GitHub could not be queried: "},
    {"El repositorio no tiene versiones publicadas todavía.", "The repository has no published releases yet."},
    {"La versión ", "Version "},
    {" no trae ningún .zip.", " has no .zip."},
    {"Estás al día (", "You are up to date ("},
    {"Hay una versión nueva: ", "There is a new version: "},
    {" (tienes ", " (you have "},
    {"Descargando ", "Downloading "},
    {"La descarga falló: ", "The download failed: "},
    {"Descargada. Pulsa Instalar: el launcher se cierra, copia los archivos nuevos y se vuelve a abrir.", "Downloaded. Press Install: the launcher closes, copies the new files and opens again."},
    {"Cierra el juego antes de actualizar.", "Close the game before updating."},
    {"Descomprimiendo…", "Unpacking…"},
    {"No se pudo descomprimir la actualización.", "The update could not be unpacked."},
    {"Las versiones nuevas se publican como \"releases\" de GitHub: el launcher las busca ahí y las instala por ti.", "New versions are published as GitHub \"releases\": the launcher looks for them there and installs them for you."},
    {"Avanzado: repositorio y token de GitHub", "Advanced: GitHub repository and token"},
    {"Repositorio:", "Repository:"},
    {"Token de GitHub (solo para repositorios privados):", "GitHub token (only for private repositories):"},
    {"Usar el token de gh", "Use the gh token"},
    {"gh no devolvió ningún token (¿está instalado y con la sesión iniciada?).", "gh returned no token (is it installed and signed in?)."},
    {"Buscar actualizaciones", "Check for updates"},
    {"Descargar", "Download"},
    {"Instalar y reiniciar el launcher", "Install and restart the launcher"},
    {"El juego ya está abierto.", "The game is already open."},
    {"No se pudo abrir TS2.exe.", "TS2.exe could not be opened."},
    {"Juego listo", "Game ready"},
    {"Versión equivocada", "Wrong version"},
    {"Falta el juego", "Game missing"},
    {"Seleccionar ISO USA...", "Select disc copy (USA ISO)..."},
    {"Volver a comprobar", "Check again"},
    {"JUGAR", "PLAY"},
    {"Abrir la carpeta del juego", "Open the game folder"},
    {"Ver el registro (log)", "View the log"},
    {"En el juego, F1 abre el panel de ajustes.", "In the game, F1 opens the settings panel."},
    {"Versión ", "Version "},
    {"Los perfiles del juego (partidas, niveles y personajes desbloqueados).", "The game's profiles (saves, unlocked levels and characters)."},
    {"Los mapas que has creado o importado en el MapMaker.", "The maps you created or imported in the MapMaker."},
    {"Imagen de Xbox (*.iso)", "Xbox disc image (*.iso)"},
    {"Todos", "All files"},
    {"ISO USA de TimeSplitters 2", "TimeSplitters 2 USA disc copy (ISO)"},
    {"Archivo zip (*.zip)", "Zip file (*.zip)"},
    {"Guardar copia", "Save backup"},
    {"Copia (*.zip)", "Backup (*.zip)"},
    {"Importar perfiles o mapas", "Import profiles or maps"},
    {"Idioma", "Language"},
    {"OPCIONES", "OPTIONS"},
    {"PERFILES", "PROFILES"},
    {"MAPAS MAPMAKER", "MAPMAKER MAPS"},
    {"ACTUALIZACIONES", "UPDATES"},
};

struct Language {
    std::string name;
    std::unordered_map<std::string, std::string> table;
};
std::vector<Language> g_langs;
int g_lang = 1;
std::string g_lang_name;  // the saved choice, "" until the player picks one

const char* T(const char* es) {
    if (g_lang <= 0 || g_lang >= int(g_langs.size())) return es;
    const auto& tb = g_langs[size_t(g_lang)].table;
    auto it = tb.find(es);
    return it == tb.end() ? es : it->second.c_str();
}

void InitLanguages() {
    g_langs.clear();
    g_langs.push_back({"Español", {}});
    Language en{"English", {}};
    for (const TrPair& p : kEnglish) en.table[p.es] = p.en;
    g_langs.push_back(std::move(en));
    std::error_code ec;
    fs::path dir = g_exe / "lang";
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".txt" || e.path().stem() == "template") continue;
        std::ifstream f(e.path(), std::ios::binary);
        Language lg{e.path().stem().string(), {}};
        std::string line;
        while (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            size_t at = line.rfind(" = ");  // the last one: a source text may hold " = " itself, a translation does not
            if (at != std::string::npos) lg.table[line.substr(0, at)] = line.substr(at + 3);
        }
        if (lg.table.empty()) continue;
        lg.name[0] = char(toupper((unsigned char)lg.name[0]));
        if (lg.name == "Francais") lg.name = "Français";
        else if (lg.name == "Portugues") lg.name = "Português";
        g_langs.push_back(std::move(lg));
    }
    // the template for the launcher's texts, in launcher_data (the game's own lang folder stays as it is)
    fs::create_directories(g_data, ec);
    std::ofstream t(g_data / "launcher_template.txt", std::ios::binary);
    if (t) {
        t << "# Copy these lines into lang/<language>.txt (for example french.txt) and translate what is after \" = \".\r\n"
             "# The text before it is the launcher's source text (Spanish) and must stay as written.\r\n";
        for (const TrPair& p : kEnglish) t << p.es << " = " << p.en << "\r\n";
    }
    // the saved choice, else the language of Windows, else English
    auto find = [&](const std::string& name) {
        for (size_t i = 0; i < g_langs.size(); i++) {
            std::string a = g_langs[i].name, b = name;
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            if (a == b) return int(i);
        }
        return -1;
    };
    int pick = g_lang_name.empty() ? -1 : find(g_lang_name);
    if (pick < 0) {
        wchar_t loc[LOCALE_NAME_MAX_LENGTH] = {};
        GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH);
        std::wstring code(loc);
        code = code.substr(0, code.find(L'-'));
        if (code == L"es") pick = 0;
        else if (code == L"fr") pick = std::max({find("french"), find("francais"), find("français")});
        else if (code == L"de") pick = std::max(find("german"), find("deutsch"));
        else if (code == L"it") pick = std::max(find("italian"), find("italiano"));
        else if (code == L"pt") pick = std::max({find("portuguese"), find("portugues"), find("português")});
        if (pick < 0) pick = 1;
    }
    g_lang = pick;
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

// "name\0pattern\0...\0\0" of the file dialogs, the names in the language chosen
std::wstring Filter(const std::vector<std::pair<const char*, const wchar_t*>>& items) {
    std::wstring f;
    for (const auto& it : items) {
        f += W(it.first);
        f.push_back(L'\0');
        f += it.second;
        f.push_back(L'\0');
    }
    f.push_back(L'\0');
    return f;
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
        error = T("la imagen no tiene default.xbe");
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
            g_game = {GameState::Ok, T("Archivos extraídos: ") + P8(assets), "default.xbe " + sha};
            return;
        }
        other = T("default.xbe de otra versión (") + sha + ")";
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
                g_game = {GameState::Ok, T("Imagen de disco: ") + P8(e.path()), "default.xbe " + sha};
                return;
            }
            other = P8(e.path().filename()) + T(" es de otra versión del juego (") + sha + ")";
        }
    if (!other.empty()) g_game = {GameState::Other, other, T("Este port se hizo con la versión USA (default.xbe ") + kExpected + ")."};
    else g_game = {GameState::Missing, T("No hay juego en ") + P8(assets), T("Pon la ISO USA de TimeSplitters 2 con el botón de arriba.")};
}

void ImportIso() {
    std::wstring iso = PickFile(false, Filter({{T("Imagen de Xbox (*.iso)"), L"*.iso"}, {T("Todos"), L"*.*"}}).c_str(), W(T("ISO USA de TimeSplitters 2")).c_str());
    if (iso.empty()) return;
    Job([iso] {
        Say(T("Comprobando la imagen…"));
        std::string sha, error;
        if (!ImageXbe(iso, sha, error)) {
            Say(T("Esa imagen no sirve: ") + error);
            return;
        }
        if (!kExpected.empty() && sha != kExpected) {
            Say(T("Esa ISO no es la versión USA que usa este port (default.xbe ") + sha + T(", se esperaba ") + kExpected + ").");
            return;
        }
        fs::path assets = FindAssets();
        std::error_code ec;
        fs::create_directories(assets, ec);
        fs::path dest = assets / fs::path(iso).filename();
        Say(T("Copiando la ISO a ") + P8(dest) + "…");
        auto cb = [](LARGE_INTEGER total, LARGE_INTEGER done, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, LPVOID) -> DWORD {
            g_progress = total.QuadPart ? float(double(done.QuadPart) / double(total.QuadPart)) : 0.f;
            return PROGRESS_CONTINUE;
        };
        if (!CopyFileExW(iso.c_str(), dest.c_str(), cb, nullptr, nullptr, 0)) {
            Say(T("No se pudo copiar la ISO (error ") + std::to_string(GetLastError()) + T("), ¿hay espacio en el disco?"));
            return;
        }
        CheckGame();
        Say(T("ISO USA instalada. Ya puedes jugar."));
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
    Say(T("Opciones guardadas en settings.toml."));
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
    if (ImGui::Button(T("Guardar"))) SaveCfg();
    ImGui::SameLine();
    if (ImGui::Button(T("Descartar cambios"))) LoadCfg();
    ImGui::SameLine();
    if (g_cfg_dirty) ImGui::TextColored(ImVec4(1, .8f, .3f, 1), T("Hay cambios sin guardar"));
    if (GameRunning()) ImGui::TextColored(ImVec4(1, .6f, .3f, 1), T("El juego está abierto: cierra el juego antes de guardar, o pisará estos cambios al salir."));
    ImGui::Separator();
    // the sections as a row of labels, one open at a time, the open one underlined
    static int sec = 0;
    {
        const char* labels[] = {T("Pantalla"), T("Gráficos"), T("Audio"), T("Controles"), T("Juego"), T("Packs de recursos")};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < 6; i++) {
            if (i) ImGui::SameLine(0, 26.f * g_scale);
            ImVec2 at = ImGui::GetCursorScreenPos(), sz = ImGui::CalcTextSize(labels[i]);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##sec", ImVec2(sz.x, sz.y + 8.f * g_scale))) sec = i;
            bool hot = ImGui::IsItemHovered(), on = sec == i;
            ImGui::PopID();
            ImU32 col = on ? IM_COL32(235, 242, 255, 255) : hot ? IM_COL32(190, 210, 245, 255) : IM_COL32(130, 150, 185, 255);
            dl->AddText(at, col, labels[i]);
            if (on) dl->AddRectFilled(ImVec2(at.x, at.y + sz.y + 5.f * g_scale), ImVec2(at.x + sz.x, at.y + sz.y + 7.f * g_scale), IM_COL32(60, 140, 255, 255));
        }
        ImGui::Separator();
    }
    ImGui::BeginChild("opts", ImVec2(0, 0));
    if (sec == 0) {
        ComboStr(T("Modo de pantalla"), "display_mode", "windowed", {{"fullscreen", T("Pantalla completa")}, {"borderless", T("Sin bordes")}, {"windowed", T("Ventana")}});
        int64_t w = CfgInt("width", 0), h = CfgInt("height", 0);
        std::string cur = w == 0 ? T("Automática (la del escritorio)") : std::to_string(w) + " x " + std::to_string(h);
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo(T("Resolución"), cur.c_str())) {
            if (ImGui::Selectable(T("Automática (la del escritorio)"), w == 0)) SetI("width", 0), SetI("height", 0);
            const int res[][2] = {{1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160}};
            for (auto& r : res) {
                std::string t = std::to_string(r[0]) + " x " + std::to_string(r[1]);
                if (ImGui::Selectable(t.c_str(), w == r[0] && h == r[1])) SetI("width", r[0]), SetI("height", r[1]);
            }
            ImGui::EndCombo();
        }
        ComboStr(T("Relación de aspecto"), "aspect_ratio", "auto", {{"auto", T("La de la pantalla")}, {"4:3", "4:3"}, {"16:9", "16:9"}, {"16:10", "16:10"}, {"21:9", "21:9"}, {"32:9", "32:9"}});
        ComboInt(T("Resolución interna (máx. 5K)"), "render_lines", 0, {{0, T("Nativa (la de la pantalla)")}, {480, T("Original (480p)")}, {720, "720p"}, {1080, "1080p"}, {1440, "1440p"}, {2160, "2160p (4K)"}, {2880, "2880p (5K)"}});
        SliderInt(T("Límite de fotogramas (0 = sin límite)"), "fps_cap", 60, 0, 360);
        CheckBox(T("Sincronización vertical"), "vsync", true);
        CheckBox(T("HDR10 (si Windows tiene el HDR activado)"), "hdr", false);
        CheckBox(T("Menús, carga y vídeos a pantalla ancha"), "menus_wide", true);
        CheckBox(T("Cinemáticas sin barras negras"), "no_letterbox", false);
        CheckBox(T("Arcos de vida y escudo altos (como TS2 Redux)"), "arcs_tall", false);
    }
    if (sec == 1) {
        ComboInt("MSAA", "msaa", 0, {{0, T("Desactivado")}, {2, "2x"}, {4, "4x"}, {8, "8x"}});
        ComboInt(T("Suavizado de bordes"), "edge_smoothing", 0, {{0, T("Desactivado")}, {1, "FXAA"}, {2, "SMAA"}});
        ComboInt(T("Filtrado anisotrópico"), "anisotropic_filtering", 0, {{0, T("Como pide el juego")}, {2, "2x"}, {4, "4x"}, {8, "8x"}, {16, "16x"}});
        SliderInt(T("Campo de visión (original: 60)"), "fov_degrees", 60, 60, 100, T("%d grados"));
        SliderInt(T("Brillo"), "brightness", 100, 50, 150);
        SliderInt(T("Saturación"), "saturation", 100, 50, 200);
        SliderInt(T("Contraste"), "contrast", 100, 50, 150);
        CheckBox(T("Niebla"), "fog", true);
        CheckBox(T("Distorsión de calor del fuego"), "heat_haze", true);
        CheckBox(T("Luz de fuego y explosiones con bordes suaves"), "smooth_lighting", true);
    }
    if (sec == 2) {
        SliderInt(T("Volumen general (%)"), "master_volume", 100, 0, 100);
        ComboInt(T("Salida de audio (al reiniciar)"), "audio_output", 0, {{0, T("Automática")}, {1, T("Mono")}, {2, T("Estéreo 2.0")}, {3, "5.1"}});
        CheckBox(T("Pausar el juego al perder el foco"), "pause_unfocused", true);
    }
    if (sec == 3) {
        SliderInt(T("Sensibilidad del ratón (100 = 1x)"), "mouse_sensitivity", 100, 10, 400);
        SliderInt(T("Velocidad vertical del ratón (%)"), "mouse_vertical", 100, 25, 200);
        CheckBox(T("Apuntado con giroscopio (DualSense, DualShock 4, Switch Pro)"), "gyro_aim", false);
        CheckBox(T("Iconos de botones según el mando o el teclado"), "button_icons", true);
        ImGui::TextDisabled(T("Las teclas se reasignan dentro del juego, con F1 > Controles."));
    }
    if (sec == 4) {
        ComboStr(T("Idioma del juego (al reiniciar)"), "language", "english",
                 {{"english", "English"}, {"german", "Deutsch"}, {"french", "Français"}, {"spanish", "Español"}, {"italian", "Italiano"}});
        CheckBox(T("Cañón de cohetes en todos los niveles de Historia"), "story_rockets", false);
        CheckBox(T("Default Gun en las listas de armas de Arcade"), "default_gun", true);
        CheckBox(T("Tileset gótico del MapMaker"), "gothic_tileset", true);
        CheckBox(T("MapMaker: mapas de Historia con todos los mandos (co-op)"), "mapmaker_coop", true);
        CheckBox(T("MapMaker: sin límites de memoria"), "mapmaker_memory", true);
        CheckBox(T("Cámara de entrada en Robot Factory"), "level_intros", false);
        ImGui::Separator();
        ImGui::TextDisabled(T("Trucos"));
        CheckBox(T("Vida infinita"), "infinite_health", false);
        CheckBox(T("Un solo golpe en Historia"), "one_hit", false);
    }
    if (sec == 5) {
        CheckBox(T("TS2 Beta Weapons (resourcepacks/beta_weapons)"), "beta_reborn", false);
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
        if (s.name.empty()) s.name = T("(sin nombre)");
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
    std::wstring out = PickFile(true, Filter({{T("Archivo zip (*.zip)"), L"*.zip"}}).c_str(), W(T("Guardar copia")).c_str(), def);
    if (out.empty()) return;
    if (fs::path(out).extension() != L".zip") out += L".zip";
    Job([dirs, out] {
        Say(T("Creando el zip…"));
        std::error_code ec;
        fs::remove(out, ec);
        std::wstring cmd = L"tar.exe -a -c -f " + Quote(out) + L" -C " + Quote(dirs[0].parent_path());
        for (auto& d : dirs) cmd += L" " + Quote(d.filename());
        int rc = RunHidden(cmd);
        Say(rc == 0 ? T("Copia guardada en ") + U8(out) : T("No se pudo crear el zip (tar.exe devolvió ") + std::to_string(rc) + ").");
    });
}

bool HexId(const std::string& s) {
    if (s.size() != 12) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return isxdigit((unsigned char)c) != 0; });
}

void ImportSaves() {
    std::wstring zip = PickFile(false, Filter({{T("Copia (*.zip)"), L"*.zip"}, {T("Todos"), L"*.*"}}).c_str(), W(T("Importar perfiles o mapas")).c_str());
    if (zip.empty()) return;
    Job([zip] {
        Say(T("Leyendo el zip…"));
        fs::path tmp = g_data / "import_tmp";
        std::error_code ec;
        fs::remove_all(tmp, ec);
        fs::create_directories(tmp, ec);
        int rc = RunHidden(L"tar.exe -x -f " + Quote(zip) + L" -C " + Quote(tmp));
        if (rc != 0) {
            Say(T("No se pudo leer el zip (tar.exe devolvió ") + std::to_string(rc) + ").");
            return;
        }
        std::vector<fs::path> found;
        if (fs::exists(tmp / "SaveMeta.xbx", ec)) found.push_back(tmp);
        for (auto& e : fs::recursive_directory_iterator(tmp, ec))
            if (e.is_directory(ec) && fs::exists(e.path() / "SaveMeta.xbx", ec)) found.push_back(e.path());
        if (found.empty()) {
            Say(T("El zip no contiene ningún perfil ni mapa del juego (no hay SaveMeta.xbx)."));
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
        Say(T("Importados ") + std::to_string(n) + T(" elemento(s)."));
    });
}

void DeleteSave(const Save& s) {
    std::error_code ec;
    fs::path trash = g_data / "trash";
    fs::create_directories(trash, ec);
    fs::rename(s.dir, trash / (s.id + "_" + Stamp()), ec);
    Say(ec ? T("No se pudo mover a la papelera: ") + ec.message() : "\"" + s.name + T("\" movido a launcher_data/trash (se puede recuperar a mano)."));
}

std::string g_pending_delete;

void SavesTab(const char* kind, const char* title, const char* what) {
    static std::string last_scan;
    if (ImGui::Button(T("Actualizar")) || last_scan.empty()) {
        ScanSaves();
        last_scan = "x";
    }
    ImGui::SameLine();
    bool running = GameRunning();
    if (ImGui::Button(T("Importar…")) && !running) ImportSaves();
    ImGui::SameLine();
    if (ImGui::Button(T("Copia de todos…")) && !running) {
        std::vector<fs::path> d;
        for (auto& s : g_saves)
            if (s.kind == kind) d.push_back(s.dir);
        ExportSaves(d, W(std::string("TS2_") + what + "_" + Stamp()));
    }
    ImGui::SameLine();
    if (ImGui::Button(T("Abrir carpeta"))) ShellExecuteW(nullptr, L"open", UData().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (running) ImGui::TextColored(ImVec4(1, .6f, .3f, 1), T("El juego está abierto: ciérralo para importar, borrar o copiar con seguridad."));
    ImGui::TextDisabled("%s", title);
    if (ImGui::BeginTable("t", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(T("Nombre"), ImGuiTableColumnFlags_WidthStretch, 3);
        ImGui::TableSetupColumn(T("Carpeta"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn(T("Modificado"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn(T("Tamaño"), ImGuiTableColumnFlags_WidthStretch, 1);
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
            if (ImGui::SmallButton(T("Exportar"))) ExportSaves({s.dir}, W(SafeName(s.name)));
            ImGui::SameLine();
            if (g_pending_delete == s.id) {
                if (ImGui::SmallButton(T("¿Seguro?")) && !running) {
                    DeleteSave(s);
                    g_pending_delete.clear();
                    ScanSaves();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(T("No"))) g_pending_delete.clear();
            } else if (ImGui::SmallButton(T("Eliminar"))) {
                g_pending_delete = s.id;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (!shown) ImGui::TextDisabled(T("Nada por aquí todavía."));
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
        g_lang_name = t["language"].value_or(std::string());
    } catch (...) {
    }
    strncpy(g_repo_buf, g_repo.c_str(), sizeof g_repo_buf - 1);
    strncpy(g_token_buf, g_token.c_str(), sizeof g_token_buf - 1);
}
void SaveLauncherCfg() {
    std::error_code ec;
    fs::create_directories(g_data, ec);
    std::ofstream f(g_data / "launcher.toml", std::ios::binary);
    f << toml::table{{"repo", g_repo}, {"token", g_token}, {"language", g_lang_name}};
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
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { error = T("dirección no válida"); return false; }
        std::wstring full = std::wstring(path) + (uc.dwExtraInfoLength ? std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength) : L"");
        HINTERNET ses = WinHttpOpen(L"TS2-XBE-Launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
        if (!ses) { error = T("sin acceso a la red"); return false; }
        HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
        HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", full.c_str(), nullptr, nullptr, nullptr, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
        auto close = [&] { if (req) WinHttpCloseHandle(req); if (con) WinHttpCloseHandle(con); WinHttpCloseHandle(ses); };
        if (!req) { close(); error = T("no se pudo conectar"); return false; }
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy);
        std::wstring hdr = L"Accept: " + W(accept) + L"\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        if (!token.empty() && hop == 0) hdr += L"Authorization: Bearer " + W(token) + L"\r\n";
        if (!WinHttpSendRequest(req, hdr.c_str(), DWORD(-1), nullptr, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr)) {
            error = T("sin respuesta del servidor (") + std::to_string(GetLastError()) + ")";
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
            error = status == 404 ? T("no existe, o el repositorio es privado y falta el token (404)") : status == 401 ? T("token no válido (401)") : T("el servidor respondió ") + std::to_string(status);
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
    error = T("demasiadas redirecciones");
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
        g_update_state = T("Buscando…");
        std::string body, error;
        std::wstring url = L"https://api.github.com/repos/" + W(g_repo) + L"/releases/latest";
        if (!Http(url, g_token, "application/vnd.github+json", &body, {}, error)) {
            g_update_state = T("No se pudo consultar GitHub: ") + error;
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
        if (!r.have) g_update_state = T("El repositorio no tiene versiones publicadas todavía.");
        else if (r.asset_name.empty()) g_update_state = T("La versión ") + r.tag + T(" no trae ningún .zip.");
        else {
            std::string local = LocalVersion(), remote = r.tag;
            if (!remote.empty() && (remote[0] == 'v' || remote[0] == 'V')) remote.erase(0, 1);
            g_update_state = remote == local ? T("Estás al día (") + local + ")." : T("Hay una versión nueva: ") + r.tag + T(" (tienes ") + local + ").";
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
        Say(T("Descargando ") + r.asset_name + " (" + Size(r.asset_size) + ")…");
        std::string error;
        if (!Http(W(r.asset_url), g_token, "application/octet-stream", nullptr, zip, error)) {
            Say(T("La descarga falló: ") + error);
            return;
        }
        g_downloaded = true;
        Say(T("Descargada. Pulsa Instalar: el launcher se cierra, copia los archivos nuevos y se vuelve a abrir."));
    });
}

// extracts the zip, then a batch waits for this exe to end, copies over the install (keeping the settings, saves, the game
// and the logs) and starts the launcher again
void InstallUpdate() {
    if (GameRunning()) {
        Say(T("Cierra el juego antes de actualizar."));
        return;
    }
    fs::path zip = g_data / "updates" / g_release.asset_name;
    fs::path stage = g_data / "updates" / "stage";
    Job([zip, stage] {
        std::error_code ec;
        fs::remove_all(stage, ec);
        fs::create_directories(stage, ec);
        Say(T("Descomprimiendo…"));
        if (RunHidden(L"tar.exe -x -f " + Quote(zip) + L" -C " + Quote(stage)) != 0) {
            Say(T("No se pudo descomprimir la actualización."));
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
    ImGui::Text(T("Versión instalada: %s"), LocalVersion().c_str());
    ImGui::TextDisabled(T("Las versiones nuevas se publican como \"releases\" de GitHub: el launcher las busca ahí y las instala por ti."));
    // the repository is public, so a player never needs a token: this block only shows for whoever tests a private repository
    // (an empty launcher_data/advanced.txt turns it on)
    static const bool advanced = fs::exists(g_data / "advanced.txt");
    if (advanced && ImGui::CollapsingHeader(T("Avanzado: repositorio y token de GitHub"))) {
    ImGui::Text(T("Repositorio:"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(380);
    if (ImGui::InputText("##repo", g_repo_buf, sizeof g_repo_buf)) g_repo = g_repo_buf;
    ImGui::Text(T("Token de GitHub (solo para repositorios privados):"));
    ImGui::SetNextItemWidth(380);
    if (ImGui::InputText("##tok", g_token_buf, sizeof g_token_buf, ImGuiInputTextFlags_Password)) g_token = g_token_buf;
    if (ImGui::Button(T("Guardar"))) SaveLauncherCfg();
    ImGui::SameLine();
    if (ImGui::Button(T("Usar el token de gh"))) {
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
            Say(T("gh no devolvió ningún token (¿está instalado y con la sesión iniciada?)."));
        }
    }
    }
    ImGui::Separator();
    if (ImGui::Button(T("Buscar actualizaciones"))) CheckUpdate();
    if (!g_update_state.empty()) ImGui::TextWrapped("%s", g_update_state.c_str());
    if (g_release.have && !g_release.asset_name.empty()) {
        ImGui::Text("%s  %s  (%s)", g_release.tag.c_str(), g_release.name.c_str(), Size(g_release.asset_size).c_str());
        if (!g_release.notes.empty()) {
            ImGui::BeginChild("notes", ImVec2(0, 150), true);
            ImGui::TextWrapped("%s", g_release.notes.c_str());
            ImGui::EndChild();
        }
        if (!g_downloaded) {
            if (ImGui::Button(T("Descargar"))) DownloadUpdate();
        } else if (ImGui::Button(T("Instalar y reiniciar el launcher"))) {
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
        Say(T("El juego ya está abierto."));
        return;
    }
    if (intptr_t(ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, g_exe.c_str(), SW_SHOWNORMAL)) > 32) g_quit = true;
    else Say(T("No se pudo abrir TS2.exe."));
}

void PlayTab() {
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetWindowFontScale(1.9f);
    ImGui::TextUnformatted("TimeSplitters 2");
    ImGui::SetWindowFontScale(1.f);
    ImGui::TextDisabled(T("Port nativo de la versión USA de Xbox  -  versión %s"), LocalVersion().c_str());
    ImGui::Dummy(ImVec2(0, 6));

    // the state of the game files, as a card with a coloured dot
    const bool ok = g_game.kind == GameState::Ok;
    ImVec4 col = ok ? ImVec4(.35f, .90f, .50f, 1) : g_game.kind == GameState::Other ? ImVec4(1, .62f, .28f, 1) : ImVec4(1, .38f, .38f, 1);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.12f, .14f, .18f, 1));
    ImGui::BeginChild("state", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 4.6f), true);
    {
        ImVec2 at = ImGui::GetCursorScreenPos();
        float r = ImGui::GetTextLineHeight() * 0.32f;
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + r + 1, at.y + ImGui::GetTextLineHeight() * 0.5f), r, ImGui::GetColorU32(col));
        ImGui::Dummy(ImVec2(r * 2 + 8, 0));
        ImGui::SameLine();
        ImGui::TextColored(col, "%s", ok ? T("Juego listo") : g_game.kind == GameState::Other ? T("Versión equivocada") : T("Falta el juego"));
        ImGui::TextWrapped("%s", g_game.where.c_str());
        ImGui::TextDisabled("%s", g_game.detail.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 2));
    if (ImGui::Button(T("Seleccionar ISO USA..."), ImVec2(std::max(210.f, ImGui::CalcTextSize(T("Seleccionar ISO USA...")).x + 40.f), 34))) ImportIso();
    ImGui::SameLine();
    if (ImGui::Button(T("Volver a comprobar"), ImVec2(170, 34))) CheckGame();
    ImGui::Dummy(ImVec2(0, 10));

    ImGui::BeginDisabled(!ok || g_busy);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.14f, .58f, .28f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.19f, .70f, .35f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.11f, .48f, .23f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.f);
    ImGui::SetWindowFontScale(1.35f);
    if (ImGui::Button(T("JUGAR"), ImVec2(-1, 62))) Play();
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 8));
    float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button(T("Abrir la carpeta del juego"), ImVec2(half, 0))) ShellExecuteW(nullptr, L"open", g_exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (ImGui::Button(T("Ver el registro (log)"), ImVec2(half, 0))) ShellExecuteW(nullptr, L"open", (g_exe / "logs").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", T("En el juego, F1 abre el panel de ajustes."));
    ImGui::PopStyleColor();
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

// ---------------------------------------------------------------------------------------------------------------- art
// The banner at the left, as TSFP's launcher has it: art/cover.* first, then the pictures of art/slides in their names' order,
// one fading into the next. Decoded by WIC, shown as ImGui images.
struct Tex {
    ID3D11ShaderResourceView* view = nullptr;
    int w = 0, h = 0;
};
std::vector<Tex> g_art, g_portrait;
Tex g_logo;
ImFont *g_big = nullptr, *g_play = nullptr, *g_tabfont = nullptr;
int g_page = 0;

bool LoadTex(const fs::path& p, Tex& out) {
    IWICImagingFactory* f = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    bool ok = false;
    if (SUCCEEDED(f->CreateDecoderFromFilename(p.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(f->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        std::vector<uint8_t> px(size_t(w) * h * 4);
        if (w && h && SUCCEEDED(conv->CopyPixels(nullptr, w * 4, UINT(px.size()), px.data()))) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd{px.data(), w * 4, 0};
            ID3D11Texture2D* tex = nullptr;
            if (SUCCEEDED(g_dev->CreateTexture2D(&td, &sd, &tex))) {
                ok = SUCCEEDED(g_dev->CreateShaderResourceView(tex, nullptr, &out.view));
                tex->Release();
                out.w = int(w); out.h = int(h);
            }
        }
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    f->Release();
    return ok;
}

void LoadArt() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::error_code ec;
    for (const char* ext : {".png", ".jpg", ".jpeg", ".webp"}) {
        Tex t;
        if (LoadTex(g_exe / "art" / (std::string("cover") + ext), t)) { g_portrait.push_back(t); break; }
    }
    LoadTex(g_exe / "art" / "logo.png", g_logo);
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(g_exe / "art" / "slides", ec)) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const fs::path& p : files) {
        Tex t;
        if (LoadTex(p, t)) (t.w >= t.h ? g_art : g_portrait).push_back(t);
    }
    if (g_art.empty()) g_art = g_portrait;  // no landscape picture: whatever there is, cropped
}

// The background, as the port launchers of the other games have it: a wide picture across the top of the window fading into the
// dark of the rest, a warm glow at the foot and embers rising. The pictures of art/ in their names' order, one fading into the next.
void DrawBackground(ImDrawList* dl, float W, float H) {
    const ImU32 ink = IM_COL32(8, 14, 32, 255), clear = IM_COL32(8, 14, 32, 0);
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), ink);
    float picture_h = 0;
    if (!g_art.empty()) {
        double t = ImGui::GetTime() / 7.0;  // seven seconds a picture, the last 1.5 of them a fade
        size_t n = g_art.size(), i = size_t(t) % n;
        float k = n > 1 ? std::clamp(float((t - std::floor(t)) * 7.0 - 5.5) / 1.5f, 0.f, 1.f) : 0.f;
        k = k * k * (3.f - 2.f * k);  // eased, so the change starts and ends softly
        // every picture fills the same band (cover: scaled to fill, the overflow cropped, never stretched), so a picture of another
        // shape does not move the edge when it fades in; the band's lower edge melts into the dark in several steps, no hard line
        const float band = H * 0.78f;
        picture_h = band;
        auto draw = [&](const Tex& x, float alpha) {
            float s = std::max(W / float(x.w), band / float(x.h));
            float uw = W / (float(x.w) * s), vh = band / (float(x.h) * s);
            float u0 = (1.f - uw) * 0.5f;
            float v0 = (1.f - vh) * 0.28f;  // a tall picture is cropped from the lower part: the faces are above the middle
            dl->AddImage(ImTextureID(x.view), ImVec2(0, 0), ImVec2(W, band), ImVec2(u0, v0), ImVec2(u0 + uw, v0 + vh), IM_COL32(255, 255, 255, int(255 * alpha)));
        };
        draw(g_art[i], 1.f);
        if (k > 0.f) draw(g_art[(i + 1) % n], k);
        auto ink_a = [](int a) { return IM_COL32(8, 14, 32, a); };
        dl->AddRectFilledMultiColor(ImVec2(0, band * 0.35f), ImVec2(W, band * 0.65f), clear, clear, ink_a(70), ink_a(70));
        dl->AddRectFilledMultiColor(ImVec2(0, band * 0.65f), ImVec2(W, band * 0.88f), ink_a(70), ink_a(70), ink_a(190), ink_a(190));
        dl->AddRectFilledMultiColor(ImVec2(0, band * 0.88f), ImVec2(W, band + 1.f), ink_a(190), ink_a(190), ink, ink);
    }
    // the glow at the foot, the menus' blue
    dl->AddRectFilledMultiColor(ImVec2(0, H * 0.55f), ImVec2(W, H), IM_COL32(20, 60, 150, 0), IM_COL32(20, 60, 150, 0), IM_COL32(30, 90, 200, 70),
                                IM_COL32(30, 90, 200, 70));
    // embers rising, in the blue of the menus
    double tm = ImGui::GetTime();
    for (int i = 0; i < 48; i++) {
        float seed = std::fmod(std::sin(float(i) * 12.9898f) * 43758.5453f, 1.f);
        if (seed < 0) seed += 1.f;
        float speed = (10.f + float(i % 7) * 5.f) * g_scale;
        float rise = std::fmod(float(tm) * speed + seed * H, H * 0.95f);
        float x = seed * W + std::sin(float(tm) * 0.7f + float(i)) * 14.f * g_scale;
        float y = H - rise;
        float life = 1.f - rise / (H * 0.95f);
        float a = std::clamp(life * 1.4f, 0.f, 1.f) * (0.45f + 0.35f * std::sin(float(tm) * 2.f + float(i) * 1.7f));
        dl->AddCircleFilled(ImVec2(x, y), (1.0f + float(i % 3) * 0.7f) * g_scale, IM_COL32(90, 175, 255, int(255 * std::clamp(a, 0.f, 1.f))));
    }
}

// text in a font at its own size
void PutText(ImDrawList* dl, ImFont* f, float size, ImVec2 at, ImU32 col, const char* text, bool right = false) {
    if (!f) f = ImGui::GetFont();
    if (right) at.x -= f->CalcTextSizeA(size, FLT_MAX, 0.f, text).x;
    dl->AddText(f, size, at, col, text);
}

// the first page: the title, the play button and what the game's files are, at the foot of the picture to the right
void HomePage(float W, float H, float bar) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float m = 54.f * g_scale, base = H - bar - 40.f * g_scale;
    const bool ok = g_game.kind == GameState::Ok;
    const ImU32 col = ok ? IM_COL32(95, 230, 130, 255) : g_game.kind == GameState::Other ? IM_COL32(255, 160, 70, 255) : IM_COL32(255, 100, 100, 255);
    // the hint, the chips, the button, the title: from the bottom up
    PutText(dl, nullptr, 15.f * g_scale, ImVec2(W - m, base - 4.f * g_scale), IM_COL32(150, 155, 170, 255), T("En el juego, F1 abre el panel de ajustes."), true);
    std::string chips[2] = {std::string(ok ? T("Juego listo") : g_game.kind == GameState::Other ? T("Versión equivocada") : T("Falta el juego")), T("Versión ") + LocalVersion()};
    ImU32 cols[2] = {col, IM_COL32(190, 195, 210, 255)};
    float cx = W - m, cy = base - 46.f * g_scale, ch = 28.f * g_scale;
    for (int i = 0; i < 2; i++) {
        ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(15.f * g_scale, FLT_MAX, 0.f, chips[i].c_str());
        float w = sz.x + 26.f * g_scale;
        dl->AddRect(ImVec2(cx - w, cy), ImVec2(cx, cy + ch), cols[i], ch * 0.5f, 0, 1.4f * g_scale);
        dl->AddText(ImGui::GetFont(), 15.f * g_scale, ImVec2(cx - w + 13.f * g_scale, cy + (ch - sz.y) * 0.5f), cols[i], chips[i].c_str());
        cx -= w + 10.f * g_scale;
    }
    float bw = 236.f * g_scale, bh = 74.f * g_scale, by = cy - 16.f * g_scale - bh;
    ImGui::SetCursorScreenPos(ImVec2(W - m - bw, by));
    ImGui::BeginDisabled(!ok || g_busy);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.10f, .36f, .84f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.18f, .50f, 1.f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.07f, .28f, .66f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f * g_scale);
    if (g_play) ImGui::PushFont(g_play);
    if (ImGui::Button(T("JUGAR"), ImVec2(bw, bh))) Play();
    if (g_play) ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    // the title: the game's own logo, or its name in letters when the picture is missing
    if (g_logo.view) {
        float lw = std::min(430.f * g_scale, W * 0.42f), lh = lw * float(g_logo.h) / float(g_logo.w);
        float lx = W - m - lw, ly2 = by - 14.f * g_scale - lh;
        dl->AddImage(ImTextureID(g_logo.view), ImVec2(lx, ly2), ImVec2(lx + lw, ly2 + lh));
    } else {
        float ts = 60.f * g_scale, ty = by - ts - 6.f * g_scale;
        ImFont* bf = g_big ? g_big : ImGui::GetFont();
        float w2 = bf->CalcTextSizeA(ts, FLT_MAX, 0.f, " 2").x;
        PutText(dl, bf, ts, ImVec2(W - m, ty), IM_COL32(70, 150, 255, 255), " 2", true);
        PutText(dl, bf, ts, ImVec2(W - m - w2, ty), IM_COL32(240, 242, 248, 255), "TIMESPLITTERS", true);
    }
    // the file actions, small and quiet at the left
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, .10f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.74f, .77f, .84f, 1));
    float ly = base - 4.f * g_scale - 4 * (ImGui::GetFrameHeight() + 4.f * g_scale);
    const char* names[4] = {T("Seleccionar ISO USA..."), T("Volver a comprobar"), T("Abrir la carpeta del juego"), T("Ver el registro (log)")};
    for (int i = 0; i < 4; i++) {
        ImGui::SetCursorScreenPos(ImVec2(m * 0.6f, ly + float(i) * (ImGui::GetFrameHeight() + 4.f * g_scale)));
        if (ImGui::Button(names[i])) {
            if (i == 0) ImportIso();
            else if (i == 1) CheckGame();
            else if (i == 2) ShellExecuteW(nullptr, L"open", g_exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else ShellExecuteW(nullptr, L"open", (g_exe / "logs").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
    ImGui::PopStyleColor(3);
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
    s.WindowRounding = 0; s.FrameRounding = 8; s.GrabRounding = 8; s.TabRounding = 8; s.ChildRounding = 10; s.PopupRounding = 8; s.ScrollbarRounding = 8;
    s.FramePadding = ImVec2(12, 7); s.ItemSpacing = ImVec2(10, 9); s.WindowPadding = ImVec2(16, 14); s.ChildBorderSize = 1; s.FrameBorderSize = 0;
    s.TabBarBorderSize = 1; s.ScrollbarSize = 12;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(.07f, .08f, .10f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(.09f, .10f, .13f, 1);
    c[ImGuiCol_Border] = ImVec4(.18f, .21f, .27f, 1);
    c[ImGuiCol_Separator] = ImVec4(.18f, .21f, .27f, 1);
    c[ImGuiCol_Tab] = ImVec4(.11f, .13f, .17f, 1);
    c[ImGuiCol_TabHovered] = ImVec4(.26f, .46f, .78f, 1);
    c[ImGuiCol_TabActive] = ImVec4(.20f, .38f, .68f, 1);
    c[ImGuiCol_Button] = ImVec4(.18f, .34f, .62f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(.25f, .46f, .80f, 1);
    c[ImGuiCol_ButtonActive] = ImVec4(.15f, .29f, .54f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(.13f, .15f, .19f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(.17f, .20f, .26f, 1);
    c[ImGuiCol_Header] = ImVec4(.16f, .20f, .28f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(.22f, .30f, .45f, 1);
    c[ImGuiCol_CheckMark] = ImVec4(.45f, .75f, 1, 1);
    c[ImGuiCol_SliderGrab] = ImVec4(.35f, .60f, .95f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(.55f, .60f, .68f, 1);
    c[ImGuiCol_PlotHistogram] = ImVec4(.25f, .70f, .40f, 1);
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
    InitLanguages();
    CheckGame();
    ScanSaves();

    UINT dpi = GetDpiForSystem();
    float scale = float(dpi) / 96.f;
    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, WndProc, 0, 0, inst, LoadIconW(inst, MAKEINTRESOURCEW(1)), nullptr, nullptr, nullptr, L"TS2Launcher", nullptr};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TimeSplitters 2 - Launcher", WS_OVERLAPPEDWINDOW, 100, 80, int(1100 * scale), int(700 * scale), nullptr, nullptr, inst, nullptr);
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
    g_scale = scale;
    {
        static const ImWchar big_ranges[] = {0x20, 0x17F, 0};
        g_big = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 60.f * scale, nullptr, big_ranges);
        g_play = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 28.f * scale, nullptr, big_ranges);
        g_tabfont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisb.ttf", 16.f * scale, nullptr, big_ranges);
    }
    Style(scale);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    LoadArt();

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
        ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
        const float W = io.DisplaySize.x, H = io.DisplaySize.y, bar = 56.f * scale;
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        DrawBackground(bg, W, H);
        // the page
        if (g_page == 0) {
            HomePage(W, H, bar);
        } else {
            float pm = 26.f * scale;
            ImGui::SetCursorScreenPos(ImVec2(pm, pm));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.03f, .06f, .12f, .90f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.f * scale, 16.f * scale));
            ImGui::BeginChild("page", ImVec2(W - 2 * pm, H - bar - 2 * pm - 34.f * scale), true);
            if (g_page == 1) OptionsTab();
            else if (g_page == 2) SavesTab("perfil", T("Los perfiles del juego (partidas, niveles y personajes desbloqueados)."), "perfiles");
            else if (g_page == 3) SavesTab("mapa", T("Los mapas que has creado o importado en el MapMaker."), "mapas");
            else UpdatesTab();
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        // what the launcher is doing
        if (g_busy) {
            ImGui::SetCursorScreenPos(ImVec2(28.f * scale, H - bar - 52.f * scale));
            ImGui::ProgressBar(g_progress, ImVec2(W * 0.4f, 10.f * scale), "");
        }
        ImGui::SetCursorScreenPos(ImVec2(28.f * scale, H - bar - 28.f * scale));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.80f, .83f, .90f, 1));
        ImGui::TextUnformatted(Said().c_str());
        ImGui::PopStyleColor();
        // the bar of pages at the foot
        const ImU32 red = IM_COL32(60, 140, 255, 255);
        bg->AddRectFilled(ImVec2(0, H - bar), ImVec2(W, H), IM_COL32(6, 10, 22, 248));
        bg->AddRectFilled(ImVec2(0, H - bar), ImVec2(W, H - bar + 2.f * scale), IM_COL32(30, 80, 170, 255));
        static const char* kPages[5] = {"JUGAR", "OPCIONES", "PERFILES", "MAPAS MAPMAKER", "ACTUALIZACIONES"};
        float px = 18.f * scale;
        ImFont* tf = g_tabfont ? g_tabfont : ImGui::GetFont();
        float tsz = 16.f * scale;
        for (int i = 0; i < 5; i++) {
            ImVec2 ts = tf->CalcTextSizeA(tsz, FLT_MAX, 0.f, T(kPages[i]));
            float w = ts.x + 40.f * scale;
            ImGui::SetCursorScreenPos(ImVec2(px, H - bar + 2.f * scale));
            char id[16];
            snprintf(id, sizeof id, "##pg%d", i);
            if (ImGui::InvisibleButton(id, ImVec2(w, bar - 2.f * scale))) g_page = i;
            bool hot = ImGui::IsItemHovered(), on = g_page == i;
            if (on) bg->AddRectFilled(ImVec2(px, H - bar + 2.f * scale), ImVec2(px + w, H), IM_COL32(255, 255, 255, 14));
            if (on) bg->AddRectFilled(ImVec2(px, H - 4.f * scale), ImVec2(px + w, H), red);
            bg->AddText(tf, tsz, ImVec2(px + 20.f * scale, H - bar * 0.5f - ts.y * 0.5f + 1.f * scale),
                        on ? IM_COL32(110, 175, 255, 255) : hot ? IM_COL32(245, 245, 250, 255) : IM_COL32(170, 175, 190, 255), T(kPages[i]));
            px += w;
        }
        {
            std::string v = "v" + LocalVersion() + "  -  Windows";
            ImVec2 vs = ImGui::GetFont()->CalcTextSizeA(15.f * scale, FLT_MAX, 0.f, v.c_str());
            bg->AddText(ImGui::GetFont(), 15.f * scale, ImVec2(W - vs.x - 22.f * scale, H - bar * 0.5f - vs.y * 0.5f), IM_COL32(120, 126, 140, 255), v.c_str());
            float cw = 150.f * scale;
            ImGui::SetCursorScreenPos(ImVec2(W - vs.x - 22.f * scale - cw - 18.f * scale, H - bar + 14.f * scale));
            ImGui::SetNextItemWidth(cw);
            if (ImGui::BeginCombo("##lang", g_langs[size_t(g_lang)].name.c_str())) {
                for (int i = 0; i < int(g_langs.size()); i++)
                    if (ImGui::Selectable(g_langs[size_t(i)].name.c_str(), i == g_lang)) {
                        g_lang = i;
                        g_lang_name = g_langs[size_t(i)].name;
                        SaveLauncherCfg();
                    }
                ImGui::EndCombo();
            }
        }
        ImGui::End();
        ImGui::Render();
        const float clear[4] = {.035f, .035f, .047f, 1};
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
