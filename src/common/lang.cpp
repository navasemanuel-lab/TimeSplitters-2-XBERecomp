#include "common/lang.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/options.h"

namespace lang {
namespace {

struct Pair {
    const char* es;
    const char* en;
};

const Pair kEnglish[] = {
    {"Cada accion tiene una tecla principal y otra alterna. Pulsa una casilla y despues la tecla, el boton del raton o la rueda que quieras. Clic derecho, o Suprimir mientras esperas, la deja sin asignar; Esc cancela. Si la tecla ya era de otra accion, esa se queda sin ella. Sin nada asignado a la rueda, cambia de arma y hace zoom con el visor puesto.",
     "Each action has a main key and an alternate one. Click a box, then press the key, mouse button or wheel you want. Right click, or Delete while waiting, leaves it unassigned; Esc cancels. If the key was another action's, that one loses it. With nothing assigned to the wheel, it switches weapons and zooms with the scope on."},
    {"Filtrar acciones o teclas...", "Filter actions or keys..."},
    {"Accion", "Action"},
    {"Principal", "Main"},
    {"Alterna", "Alternate"},
    {"Ninguna accion coincide.", "No action matches."},
    {"Restablecer teclas", "Reset keys"},
    {"Pulsa una tecla...", "Press a key..."},
    {" ya era de ", " was bound to "},
    {": se quedo sin ella.", ": it was left unassigned."},
    {"MANDO", "CONTROLLER"},
    {"Velocidad de giro horizontal", "Horizontal turn speed"},
    {"Velocidad de giro vertical", "Vertical turn speed"},
    {"Velocidad horizontal al apuntar", "Horizontal speed when aiming"},
    {"Velocidad vertical al apuntar", "Vertical speed when aiming"},
    {"RATON Y TECLADO", "MOUSE AND KEYBOARD"},
    {"Velocidad de giro horizontal##m", "Horizontal turn speed##m"},
    {"Velocidad vertical (respecto a la horizontal)", "Vertical speed (relative to the horizontal)"},
    {"Velocidad horizontal al apuntar##m", "Horizontal speed when aiming##m"},
    {"Velocidad vertical al apuntar##m", "Vertical speed when aiming##m"},
    {"Invertir eje vertical", "Invert vertical axis"},
    {"Sway de la mira al girar (estilo GoldenEye)", "Crosshair sway when turning (GoldenEye style)"},
    {"Auto Lookahead (nivelar la vista al horizonte)", "Auto Lookahead (level the view to the horizon)"},
    {"Restablecer a 1x", "Reset to 1x"},
    {"1x del raton son 0,1 grados por cuenta; con el visor puesto gira menos para cubrir lo mismo.",
     "Mouse 1x is 0.1 degrees a count; with the scope on it turns less to cover the same."},
    {"Modo de pantalla", "Display mode"},
    {"Pantalla completa", "Fullscreen"},
    {"Ventana", "Windowed"},
    {"Escritorio", "Desktop"},
    {"Resolucion", "Resolution"},
    {"Relacion de aspecto", "Aspect ratio"},
    {"Automatica (la de la pantalla)", "Automatic (the screen's)"},
    {"Escala de render", "Render scale"},
    {"Resolucion interna", "Internal resolution"},
    {"Nativa", "Native"},
    {"Original", "Original"},
    {"Nativa iguala los pixeles de la ventana; Original dibuja las 480 lineas de la consola. Mas alta afina la imagen y gasta mas GPU.",
     "Native matches the window's pixels; Original draws the console's 480 lines. Higher sharpens the image and uses more GPU."},
    {"HUD sin estirar en pantallas anchas (en los bordes de la pantalla)", "HUD not stretched on wide screens (at the screen's edges)"},
    {"Ganancia del audio (0 = nivel original de la consola)", "Audio gain (0 = the console's own level)"},
    {"Sube el volumen general con un limitador suave para no distorsionar. 6 dB iguala el nivel limpio de Boston.", "Raises the overall volume with a soft limiter so it does not distort. 6 dB matches Boston's clean level."},
    {"Iconos de botones del juego segun el mando o el teclado", "Game button icons by controller or keyboard"},
    {"Los avisos de los menus y la pausa (A Aceptar, B Atras...) muestran los botones del mando que uses (Xbox, PlayStation 4 y 5, Switch) o las teclas que tengas asignadas, y cambian al momento si cambias una tecla.",
     "The prompts of the menus and the pause screen (A Select, B Back...) show the buttons of the controller you use (Xbox, PlayStation 4 and 5, Switch) or the keys you have bound, and change at once when you rebind a key."},
    {"Automatico (segun el ultimo mando o tecla usado)", "Automatic (by the last controller or key used)"},
    {"Teclado y raton", "Keyboard and mouse"},
    {"Iconos", "Icons"},
    {"Ahora:", "Now:"},
    {"Eliminar el perfil", "Delete the profile"},
    {"X / Supr otra vez: eliminar (se cancela solo en 6 s)", "X / Delete again: delete (cancels itself in 6 s)"},
    {"Ver todo el mapa sin limite de distancia ni niebla", "See the whole map: no draw distance limit, no fog"},
    {"Cinematicas sin barras negras", "Cutscenes without letterbox bars"},
    {"Recorta las barras negras de arriba y abajo de los videos de las cinematicas en pantallas anchas.", "Crops the black bars at the top and bottom of the cutscene videos on wide screens."},
    {"Mostrar siempre la reticula (sin apuntar)", "Always show reticle (without aiming)"},
    {"La mira se ve siempre, no solo al apuntar o con un objetivo cerca. Necesita la mira activada en las opciones del juego.", "The crosshair is always shown, not only when aiming or with a target near. Needs the crosshair turned on in the game's options."},
    {"Barras de vida y escudo estilo TimeSplitters 1 (en lugar de los arcos)", "TimeSplitters 1 style health and shield bars (instead of the arcs)"},
    {"Dos barras arriba a la izquierda en vez de los arcos de vida y escudo. Solo con un jugador.", "Two bars at the top left instead of the health and shield arcs. One player only."},
    {"Toda la municion que llevas se mantiene llena.", "All the ammo you carry is kept full."},
    {"Trucos", "Cheats"},
    {"Invulnerabilidad", "Invulnerability"},
    {"No recibes dano. Se aplica a todos los jugadores locales.", "Take no damage. Applies to all local players."},
    {"Desbloquear todo (niveles, personajes y retos del perfil) esta en el menu del juego: Options > Player Options > Unlock Everything.",
     "Unlock everything (levels, characters and challenges of the profile) is in the game's menu: Options > Player Options > Unlock Everything."},
    {"MSAA", "MSAA"},
    {"Desactivado", "Off"},
    {"Suavizado de bordes", "Edge smoothing"},
    {"Filtrado anisotropico", "Anisotropic filtering"},
    {"Como pide el juego", "As the game asks"},
    {"Sincronizacion vertical", "Vertical sync"},
    {"Brillo", "Brightness"},
    {"Idioma del juego (al reiniciar)", "Game language (next start)"},
    {"El texto de los menus y del juego. Las voces siguen en ingles.", "The menus' and game's text. The voices stay English."},
    {"Saturacion", "Saturation"},
    {"Contraste", "Contrast"},
    {"Distorsion de calor del fuego", "Fire heat shimmer"},
    {"Neblina de atmosfera (el filtro suave de la Xbox sobre la imagen)", "Atmosphere haze (the Xbox's soft filter over the picture)"},
    {"Estelas de movimiento (la imagen anterior mezclada)", "Motion trails (the previous frame blended in)"},
    {"Estelas de clima (las que anaden la lluvia y la nieve)", "Weather trails (the ones rain and snow add)"},
    {"Luz de fuego y explosiones con bordes suaves en el nivel", "Fire and explosion light with smooth edges on the level"},
    {"Campo de vision (original: 60)", "Field of view (original: 60)"},
    {"%d grados", "%d degrees"},
    {"Menus, carga y videos a pantalla ancha (sin bordes negros)", "Menus, loading and videos widescreen (no black bars)"},
    {"Arcos de vida y escudo altos, como en TS2 Redux (apagado: tamano original)", "Tall health and shield arcs, as in TS2 Redux (off: original size)"},
    {"Todo se aplica al momento. La vista del juego se ensancha con la relacion de aspecto, con el mismo angulo vertical; reinicia si quieres que el juego use sus pantallas de 16:9 desde el principio.",
     "Everything applies at once. The game's view widens with the aspect ratio, at the same vertical angle; restart if you want the game to use its 16:9 screens from the start."},
    {"HDR10 (pantalla con HDR activado en Windows)", "HDR10 (display with HDR on in Windows)"},
    {"activo", "active"},
    {"HDR desactivado en Windows", "HDR is off in Windows"},
    {"Blanco HDR", "HDR white"},
    {"Brillos HDR", "HDR highlights"},
    {"El de Windows", "Windows'"},
    {"El de la pantalla", "The display's"},
    {"Mostrar fotogramas por segundo", "Show frames per second"},
    {"Mostrar estadisticas de rendimiento", "Show performance statistics"},
    {"En la esquina superior izquierda: adonde va el tiempo de cada fotograma (hilos del juego, hilo de dibujo, entrega a la pantalla, trabajo de la GPU por tipo, dibujos, mezclador de audio y los hilos mas ocupados). Medir cuesta un poco.",
     "In the top left corner: where each frame's time goes (game threads, draw thread, handover to the display, GPU work by kind, draws, audio mixer and the busiest threads). Measuring costs a little."},
    {"Guardar rendimiento en el log", "Save performance in the log"},
    {"Los mismos numeros en logs/game.log: un promedio cada diez segundos (con el 1% de fotogramas mas lentos), una linea por cada tiron (shaders creados, texturas cargadas, el juego esperando) y un resumen cada cinco minutos.",
     "The same numbers in logs/game.log: an average every ten seconds (with the slowest 1% of frames), a line for each hitch (shaders made, textures loaded, the game waiting) and a summary every five minutes."},
    {"Salida de audio (al reiniciar)", "Audio output (on restart)"},
    {"Automatico (segun tus altavoces)", "Automatic (by your speakers)"},
    {"Estereo 2.0", "Stereo 2.0"},
    {"Sonidos de Homefront (resourcepacks/homefront/sounds)", "Homefront sounds (resourcepacks/homefront/sounds)"},
    {"Cambia los efectos por los de Homefront, ajustados a la frecuencia y el formato de los originales. Se aplica al cargar el siguiente nivel.",
     "Replaces the effects with Homefront's, fitted to the originals' rate and format. Applies when the next level loads."},
    {"El volumen de la musica y de los efectos se ajusta en Options > Audio Options del juego.",
     "Music and effects volume is set in the game's Options > Audio Options."},
    {"Camara de entrada en Robot Factory (como la de Siberia)", "Opening camera in Robot Factory (like Siberia's)"},
    {"Usa los datos de camara que el juego conserva sin usar. Se aplica al empezar el nivel.",
     "Uses the camera data the game keeps unused. Applies when the level starts."},
    {"Modo debug: renderizar el mapa entero", "Debug mode: render the whole map"},
    {"Dibuja todo el nivel desde cualquier sitio: sin recorte por portales ni por distancia, sin plano lejano ni niebla. Se aplica al momento.",
     "Draws the whole level from anywhere: no portal or distance culling, no far plane and no fog. Applies at once."},
    {"Lanzacohetes en todos los niveles de Historia", "Rocket Launcher in every Story level"},
    {"Se da al empezar o reiniciar un nivel de Historia, con la municion llena.",
     "Given when a Story level starts or restarts, with full ammo."},
    {"Armas de Beta Reborn (resourcepacks/beta_reborn)", "Beta Reborn weapons (resourcepacks/beta_reborn)"},
    {"Cambia los modelos de las armas por los del mod Beta Reborn. Se aplica al cargar el siguiente nivel.",
     "Replaces the weapon models with the Beta Reborn mod's. Applies when the next level loads."},
    {"Texturas de la Christmas Edition (resourcepacks/xmas)", "Christmas Edition textures (resourcepacks/xmas)"},
    {"Cambia las texturas por las de la edicion navidena. Se aplica al instante.",
     "Replaces the textures with the Christmas Edition ones. Applies at once."},
    {"Cambia la fuente del juego por la de la demo de PS2 de septiembre de 2001. Se aplica al momento.",
     "Replaces the game's font with the September 2001 PS2 demo's. Applies at once."},
    {"Vida infinita", "Infinite health"},
    {"Un solo golpe en Historia", "One hit in Story"},
    {"Cualquier dano mata, al jugador y a los enemigos, en los niveles de Historia.",
     "Any damage kills, the player and the enemies, in Story levels."},
    {"Apuntado con giroscopio (DualSense, DualShock 4, Switch Pro)", "Gyro aiming (DualSense, DualShock 4, Switch Pro)"},
    {"Velocidad del giroscopio", "Gyro speed"},
    {"Gira la vista junto al stick derecho. Deja el mando quieto un segundo para quitar la deriva.",
     "Turns the view along with the right stick. Leave the controller still for a second to remove drift."},
    {"TimeSplitters 2 - ajustes (F1)", "TimeSplitters 2 - settings (F1)"},
    {"Graficos", "Graphics"},
    {"Audio", "Audio"},
    {"Controles", "Controls"},
    {"Jugabilidad", "Gameplay"},
    // the control labels (controls.cpp)
    {"Avanzar", "Move forward"},
    {"Retroceder", "Move back"},
    {"Paso a la izquierda", "Strafe left"},
    {"Paso a la derecha", "Strafe right"},
    {"Disparar (gatillo derecho)", "Fire (right trigger)"},
    {"Apuntar (gatillo izquierdo)", "Aim (left trigger)"},
    {"Activar (boton A)", "Use (A button)"},
    {"Boton B (atras en menus; sin uso en juego)", "B button (back in menus; unused in game)"},
    {"Agacharse (boton X)", "Crouch (X button)"},
    {"Recargar (boton Y)", "Reload (Y button)"},
    {"Boton negro (sin uso por defecto)", "Black button (unused by default)"},
    {"Boton blanco (sin uso por defecto)", "White button (unused by default)"},
    {"Zoom + con visor (cruceta arriba)", "Zoom in with scope (D-pad up)"},
    {"Zoom - con visor (cruceta abajo)", "Zoom out with scope (D-pad down)"},
    {"Arma anterior (cruceta izquierda)", "Previous weapon (D-pad left)"},
    {"Arma siguiente (cruceta derecha)", "Next weapon (D-pad right)"},
    {"Pulsar stick izquierdo (sin uso por defecto)", "Left stick click (unused by default)"},
    {"Disparo alternativo (stick derecho pulsado)", "Alternate fire (right stick click)"},
    {"Arma {} (de las que llevas, en orden)", "Weapon {} (of those you carry, in order)"},
};

using Table = std::unordered_map<std::string, std::string>;

struct Language {
    std::string name;
    Table table;
};

std::vector<Language>& Languages() {
    static std::vector<Language> list = [] {
        std::vector<Language> l;
        l.push_back({"Espanol", {}});
        Language en{"English", {}};
        for (const Pair& p : kEnglish) en.table[p.es] = p.en;
        l.push_back(std::move(en));
        // lang/*.txt beside the exe
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::error_code ec;
        std::filesystem::path dir = std::filesystem::path(std::wstring(buf, n)).parent_path() / "lang";
        std::filesystem::path tmpl = dir / "template.txt";
        if (!std::filesystem::exists(tmpl, ec)) {
            std::filesystem::create_directories(dir, ec);
            std::ofstream t(tmpl, std::ios::binary);
            if (t) {
                t << "# Copy this file to <language>.txt (for example french.txt) and translate what is after \" = \".\r\n"
                     "# The text before it is the panel's source text (Spanish) and must stay as written.\r\n"
                     "# {} in a text stands for a number.\r\n";
                for (const Pair& p : kEnglish) t << p.es << " = " << p.en << "\r\n";
            }
        }
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (e.path().extension() != ".txt" || e.path().stem() == "template") continue;
            std::ifstream f(e.path(), std::ios::binary);
            Language lg{e.path().stem().string(), {}};
            std::string line;
            while (std::getline(f, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
                if (line.empty() || line[0] == '#') continue;
                size_t at = line.find(" = ");
                if (at == std::string::npos) continue;
                lg.table[line.substr(0, at)] = line.substr(at + 3);
            }
            if (!lg.table.empty()) {
                if (!lg.name.empty()) lg.name[0] = char(std::toupper((unsigned char)lg.name[0]));
                l.push_back(std::move(lg));
            }
        }
        return l;
    }();
    return list;
}

// "Arma 7 (...)" is looked up as "Arma {} (...)"
bool Normalise(const std::string& s, std::string& key, std::string& number) {
    if (s.rfind("Arma ", 0) != 0 || s.size() < 6 || !std::isdigit((unsigned char)s[5])) return false;
    size_t e = 5;
    while (e < s.size() && std::isdigit((unsigned char)s[e])) e++;
    number = s.substr(5, e - 5);
    key = "Arma {}" + s.substr(e);
    return true;
}

}  // namespace

int Count() { return int(Languages().size()); }

const char* Name(int index) {
    auto& l = Languages();
    return l[size_t(std::clamp(index, 0, int(l.size()) - 1))].name.c_str();
}

const char* Tr(const char* source) {
    if (!source) return "";
    auto& langs = Languages();
    size_t idx = std::min<size_t>(game::Opt().ui_lang, langs.size() - 1);
    if (idx == 0) return source;
    static std::mutex lock;
    static std::vector<Table> done;
    std::lock_guard<std::mutex> guard(lock);
    if (done.size() < langs.size()) done.resize(langs.size());
    auto hit = done[idx].find(source);
    if (hit != done[idx].end()) return hit->second.c_str();
    std::string s = source, out;
    // a "##id" suffix stays, the text before it is translated
    std::string head = s, tail;
    if (size_t h = s.find("##"); h != std::string::npos) head = s.substr(0, h), tail = s.substr(h);
    const Table& t = langs[idx].table;
    std::string key, number;
    if (auto it = t.find(head); it != t.end()) {
        out = it->second;
    } else if (Normalise(head, key, number) && t.count(key)) {
        out = t.at(key);
        if (size_t b = out.find("{}"); b != std::string::npos) out.replace(b, 2, number);
    } else {
        out = head;
    }
    return done[idx].emplace(source, out + tail).first->second.c_str();
}

}  // namespace lang
