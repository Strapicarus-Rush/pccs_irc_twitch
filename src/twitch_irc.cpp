#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <vector>
#include <deque>
#include <regex>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <cerrno>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unordered_set>
#include <arpa/inet.h>
#include <stdexcept>
#include <unordered_map>
#include <condition_variable>
#include <poll.h>
#include <unistd.h> // para write
// #include <alerta.hpp>
// #include <sqlite3.h>

// ==========================
// Configuración global
// ==========================
static const std::string SERVER = "irc.chat.twitch.tv";
static const constexpr int PORT = 6667;
static const std::string CHANNEL = "#strapicarus";
static const std::string title = "\033]0;Twitch IRC - " + std::string(CHANNEL) + "\007";
SDL_Event event;

// ==========================
// Filtrado y detección
// ==========================

static const constexpr size_t SPAM_THRESHOLD = 2;
static const constexpr int TIME_WINDOW = 15; // segundos
static const constexpr int FIRST_COOLDOWN = 5 * 60;   // 5 minutos
static const constexpr int SECOND_COOLDOWN = 10 * 60; // 10 minutos

// const char* create_table = "CREATE TABLE IF NOT EXISTS logs ("
//                                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
//                                    "timestamp TEXT,"
//                                    "src_ip TEXT,"
//                                    "dst_ip TEXT,"
//                                    "protocol TEXT,"
//                                    "port INTEGER,"
//                                    "url TEXT,"
//                                    "action TEXT)";
//         sqlite3_exec(db, create_table, nullptr, nullptr, nullptr);

struct UserTTSState {
    std::deque<std::time_t> timestamps; // mensajes recientes
    int warning_level = 0;              // 0 = limpio, 1 = advertido, 2 = advertencia final, 3 = bloqueado
    std::time_t last_warning_time = 0;  // último momento de advertencia
    std::time_t blocked_until = 0;      // bloqueado hasta
    bool banned = false;    
};

std::unordered_map<std::string, UserTTSState> user_tts_states;

//blacklisted
static const std::regex regex_word_blacklist("(^|\\s)(mierda|carajo|puta|joder|cabrón|coño|pendejo|culero|verga|fuck|shit|asshole|bitch|damn|cunt|dick|faggot|bastard|nigga|niggers|nazi|fucker|chingar|pinche|mamón|gilipollas|maricón|huevón|pija|culo|zorra|cagada|pndejo|vga|mda|jdr|cñ|pt|cbn|ass|cock|prick|twat|pussy|bullshit|motherfucker|sonofabitch|whore|slut|douche|jerk|fuk|shite|asshat|bich|dickhead|fgt|wtf|bs|mf|sob)($|\\s)",std::regex::icase);
static const std::regex regex_url("(http[s]?://|\\.[a-z]{2,6}|\\[.?dot.?\\]|\\[\\.\\]|dot|\\swww\\.)|[a-z0-9-]+\\.[a-z]{2,6}",std::regex::icase);

//interactions
static const std::regex regex_greeting("^(alo|hola|holi|hi|hello|hey|que tal|como va|cómo estás|what's up|sup|saludos|buenas|buen día)(\\s|$)",std::regex::icase);
static const std::regex regex_laugh(R"(\b(lol|lmao|rofl|xd|(?:[hj][aeiou]){3,})(?:\b|_))", std::regex::icase);
static const std::regex regex_thanks(R"(\b(gracias|thx)\b)", std::regex::icase);
static const std::regex regex_appreciation(R"(\b(nice|cool|awesome)\b)", std::regex::icase);
static const std::regex regex_epic(R"(\bepic\b)", std::regex::icase);
static const std::regex regex_wtf(R"(\bwtf\b)", std::regex::icase);
static const std::regex regex_f(R"(\b(f|F)\b)", std::regex::icase);
static const std::regex regex_tic_tac(R"(\b(tic|tac)\b)", std::regex::icase);

//tags, quotes, commands
static const std::regex regex_tts("^!s\\s", std::regex::icase);
static const std::regex regex_stream_elements("StreamElements");
static const std::regex regex_msg("PRIVMSG " + CHANNEL + " :(.+)");
static const std::regex regex_new_follow(R"(Gracias por seguirme\s+(.+))");
static const std::regex regex_command("^!c\\s", std::regex::icase);
static const std::regex regex_quoted(R"((\"|')([^\"']*)\1)");
// static const std::regex regex_tag_value(key + "=([^;]*)");
static const std::regex regex_tags(R"(^@([^ ]+))");
static const std::regex regex_emotes_pos(R"((\s+):([\d\-]+))");
static const std::regex regex_emotes(
    R"(:'\(|:'D|:D|c:|;\)|:\)|:\]|:\[|T_T|>:\(|\(y\)|\(n\)|:rocket:|:fire:|<3|\*o\*|:\*|:v|xD)",
    std::regex::optimize
);

//Privileges
static const std::regex regex_tag_moderator("moderator/\\d+");
static const std::regex regex_tag_subscriber("subscriber/\\d+");
static const std::regex regex_tag_broadcaster("broadcaster/\\d+");
static const std::regex regex_tag_vip("vip/\\d+");
static const std::regex regex_tag_admin("admin/\\d+");
static const std::regex regex_tag_staff("staff/\\d+");
static const std::regex regex_tag_partner("partner/\\d+");
static const std::regex regex_tag_global_mod("global_mod/\\d+");
static const std::regex regex_tag_bot("bot/\\d+");
static const std::regex regex_has_privileges("(moderator/\\d+|subscriber/\\d+|broadcaster/\\d+|vip/\\d+|admin/\\d+|staff/\\d+|partner/\\d+|global_mod/\\d+)");


static const std::unordered_map<std::string, std::string> text_to_emoji = {
    {":'D", "\U0001F602"}, {"xD", "\U0001F923"}, {":D", "\U0001F603"},
    {"c:", "\U0001F604"}, {";)", "\U0001F609"}, {":)", "\U0001F60A"},
    {":]", "\U0001F642"}, {":[", "\U0001F641"}, {":'(", "\U0001F622"},
    {"T_T", "\U0001F62D"}, {">:(", "\U0001F621"}, {"(y)", "\U0001F44D"},
    {"(n)", "\U0001F44E"}, {":rocket:", "\U0001F680"}, {":fire:", "\U0001F525"},
    {"<3", "\u2764"}, {"*o*", "\U0001F60D"}, {":*", "\U0001F618"}, {":v", "\U0001F92A"}
};

static const std::unordered_set<std::string> english_words = {
    "the", "and", "to", "of", "a", "in", "is", "you", "that", "it", "next", "all",
    "he", "was", "for", "on", "are", "with", "as", "i", "his", "at", "them", "know",
    "be", "this", "have", "from", "or", "nice", "epic", "cool", "can", "how", "her",
    "lmao", "rofl", "awesome", "haha", "lmfao", "hehe", "kek", "whoa", "who", "among", "other", 
    "nope", "nah", "omg", "sup", "brb", "g2g", "cya", "ttyl", "there", "yes", "things", "thing",
    "yay", "ugh", "meh", "idk", "pls", "sry", "thx", "wtv", "they", "not", "wich",
    "btw", "irl", "smh", "tbh", "fml", "rn", "imo", "nvm", "aren't" "im", "am",
    "off", "try", "close", "near", "again", "she", "our", "their", "come", "last",
    "first", "game", "play", "games", "ear", "reach", "trying", "fly", "sky", "test",
    "comp", "company", "compilation", "music", "sound", "like", "do", "don't", "does",
    "before", "after", "while", "against", "world", "contry", "see", "fellow", "begin",
    "below", "under", "over", "has", "up", "down", "left", "right", "front", "back", "some",
    "channel", "call", "calling", "red", "blue", "people", "kid", "name", "pick", "handle",
    "today", "yesterday", "mornig", "night", "late", "will", "life", "live", "one", "two", "tree",
    "road", "car", "door", "water", "drink", "sleep", "run", "walk", "walking", "talk", "talking"
};
static const std::unordered_set<std::string> spanish_words = {
    "el", "de", "la", "que","y", "en", "los", "un", "es", "se",
    "por", "las", "tal", "con", "lo", "una", "uno", "para", "mi", "si",
    "te", "su", "al", "hola", "holi", "del", "tal", "saludos", "buenas", 
    "bueno", "gracias", "jaja", "jeje", "jiji", "sí", "claro", "vale", 
    "sipo", "nop", "dios", "qué", "uf", "ay", "ola", "chau", "adios", 
    "nos", "vemos", "juego", "genial", "guay", "chévere", "bue", "mal", 
    "pff", "porfa", "gracias", "grax", "pq", "tmb", "dnd", "ahí", "ahora", 
    "ahorita", "vdd", "como", "va", "otra", "otro", "cual", "cuál", "quien",
    "todo", "todos", "dia", "día", "días", "noche", "noches", "hora", "esta",
    "está", "estar", "estoy", "alguien", "alguno", "juegos", "horas", "toda",
    "todas", "porque", "tambien", "también", "aclarar", "contigo", "consigue",
    "llueve"," lluvia", "hoy", "ayer", "mañana", "pasado", "futuro", "dejo"
};
static const std::unordered_set<std::string> blacklist = {
    "mierda", "carajo", "puta", "joder", "cabrón", "coño", "pendejo", "culero", "verga", "fuck", "shit", "asshole", "bitch", "damn", "cunt", "dick", "faggot", "bastard", "nigga", "niggers", "nazi", "fucker",
    "chingar", "pinche", "mamón", "gilipollas", "maricón", "huevón", "pija", "culo", "zorra", "cagada",
    "pndejo", "vga", "mda", "jdr", "cñ", "pt", "cbn",
    "ass", "cock", "prick", "twat", "pussy", "bullshit", "motherfucker", "sonofabitch", "whore", "slut", "douche", "jerk",
    "fuk", "shite", "asshat", "bich", "dickhead", "fgt", "wtf", "bs", "mf", "sob"
};
static const std::unordered_set<std::string> greetings = {
    "hola", "holi", "hi", "hello", "hey", "qué tal", "como va",
    "cómo estás", "what's up", "sup", "saludos", "buenas"
};
static const std::unordered_set<std::string> funny_triggers = {
    "lol", "jaja", "xd", "gracias", "nice", "epic", "wtf", "cool",
    "lmao", "rofl", "awesome", "haha"
};
static const std::vector<std::pair<std::string, int>> iconos = {
    {"👑", 3}, {"🛡️", 3}, {"★", 2}, {"💜", 3},
    {"🔧", 3}, {"🖥️", 3}, {"🏆", 3}, {"🌍", 3}
};

// ==========================
// Manejo y formateo de mensajes
// ==========================
static const int get_terminal_width() {
    winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
}

struct ChatRow {
    std::string text;
    bool first_row;
};

struct ChatMessage {
    std::string badges;
    int badges_length;
    SDL_Color rgb_color;
    std::string ansi_color;
    std::string user;
    std::string msg_language;
    std::vector<ChatRow> rows;
    std::string emotes;
    time_t timestamp;
};

struct tts_type {
    std::string msg;
    std::string lang;
};

struct PrivilegeCheck {
    bool is_privileged;  // User has privileged badges
    bool skip;           // Message should be skipped (blacklisted or URL)
    bool has_command;    // Message contains a command
};

static const std::string DEFAULT_LANGUAGE = "es"; 
std::vector<ChatMessage> chat_messages;
constexpr int MAX_MESSAGES = 25;

// Generar nick
const std::string generarNick() {
    srand(time(nullptr));
    int num = rand() % 99999 + 10000;
    return "justinfan" + std::to_string(num);
}
const std::string NICK = generarNick();

// ==========================
// Depuración
// ==========================
std::atomic<bool> DEBUG{false};
void debug_echo(const std::string &msg) {
    if (DEBUG.load()) {
        std::cerr << "[DEBUG] " << msg << std::endl;
    }
}

// ==========================
// TTS
// ==========================
std::mutex chat_mutex;
std::mutex ttsMutex;
std::deque<tts_type> ttsQueue;
std::atomic<bool> ttsRunning{false};
std::atomic<bool> threads_running{false};
std::atomic<bool> display_needs_update{false};
std::thread console_refresher;


// ==========================
// ALERTAS
// ==========================
std::mutex alert_mutex;
std::atomic<bool> has_to_show_alert{false};
size_t alert_type = 0;
// Constants
constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 600;
constexpr int DISPLAY_DURATION_MS = 9000;
// Paths
const char* IMAGE_PATH = "~/Imágenes/patreon/nuevofollow.jpg";
const char* AUDIO_PATH = "~/Música/LASERS_EP-11879/LASERS_-_01_-_Amsterdam.flac";
const char* FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
std::atomic<bool> alert_running {true};

std::string expand_path(const char* path) {
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        return std::string(home) + (path + 1);
    }
    return std::string(path);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}


// ==========================
// Terminal
// ==========================
struct TerminalConfig {
    termios original;
    bool configured;
    TerminalConfig() : configured(false) {}
};

TerminalConfig termConfig;

void restaurarTerminal(int signo) {
    tcsetattr(STDIN_FILENO, TCSANOW, &termConfig.original);
    // Signal threads to stop
    threads_running = false;
    // Exit safely
    // _exit(0); // Use _exit in signal handler instead of exit to avoid atexit handlers
    std::cout << "\033[?25h" << std::endl;
    _exit(0);
}

void configurarTerminal() {
    tcgetattr(STDIN_FILENO, &termConfig.original);
    termConfig.configured = true;
    termios newConfig = termConfig.original;
    newConfig.c_lflag &= ~(ICANON | ECHO);
    newConfig.c_cc[VMIN] = 0;
    newConfig.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &newConfig);
    std::cout << "\033[?25l";
    signal(SIGINT, restaurarTerminal);
    signal(SIGTERM, restaurarTerminal);
    signal(SIGQUIT, restaurarTerminal);
}

// =========================
// SDL
// =========================
SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
TTF_Font* font = nullptr;
SDL_Color white = {255,255,255, 255};
std::mutex sdl_mutex;
std::mutex display_mutex;
std::condition_variable display_cv;
std::atomic<bool> sdl_available {false};
static const constexpr int font_width = 10;
static const constexpr int font_height = 16;
static const constexpr int window_width = 45 * font_width;
static const constexpr int window_height = 50 * font_height;

bool init_sdl() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
    if (TTF_Init() < 0) return false;

    window = SDL_CreateWindow("Twitch Chat", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width, window_height, SDL_WINDOW_SHOWN);
    if (!window) return false;

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) return false;

    font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 16); // Asegúrate de tener una fuente monoespaciada
    // font = TTF_OpenFont("/usr/share/fonts/truetype/fonts-droid-fallback/DroidSansFallback.ttf", 16); // Asegúrate de tener una fuente monoespaciada
    // font = TTF_OpenFont("/usr/share/fonts/truetype/fonts-font-awesome/fontawesome-webfont.ttf", 16); // Asegúrate de tener una fuente monoespaciada
    if (!font) return false;

    // {
    //     std::lock_guard<std::mutex> lock(sdl_mutex);
        sdl_available = true;
    // }
    return true;
}

void cleanup_sdl() {
    if (font) TTF_CloseFont(font);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}

void render_text(const std::string& text, int x, int y, SDL_Color color) {
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color); 
    // SDL_Surface* surface = TTF_RenderText_Solid(font, text.c_str(), color); TTF_RenderUTF8_Blended
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }

    SDL_Rect dest = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, nullptr, &dest);

    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

// =========================
// Todo lo demás...
// =========================
std::string escape_for_espeak(const std::string &input) {
    std::string escaped;
    escaped.reserve(input.size() + 10);
    for (char c : input) {
        if (c == '"' || c == '\\') {
            escaped += '\\';
        }
        escaped += c;
    }
    return escaped;
}

void process_tts_queue() {
    ttsRunning = true;
    while (threads_running) {
        tts_type current;
        {
            std::lock_guard<std::mutex> lock(ttsMutex);
            if (ttsQueue.empty() || !ttsRunning){
                ttsRunning = false;
                break;
            }
            current = ttsQueue.front();
            ttsQueue.pop_front();
        }
        std::string cmd = "espeak -s 130 -p 40 -a 200 -v "+ current.lang +" \"" + escape_for_espeak(current.msg) + "\" 2>/dev/null";
        int ret = system(cmd.c_str());
        if (ret != 0) {
            debug_echo("espeak failed for message: " + current.msg);
        }
    }
}

void enqueue_tts(const std::string &msg, const std::string& lang) {
    {
        std::lock_guard<std::mutex> lock(ttsMutex);
        if (ttsQueue.size() < 50) { // Arbitrary limit
            tts_type m;
            m.msg = msg;
            m.lang = lang; 
            ttsQueue.push_back(m);
        } else {
            debug_echo("TTS queue full, dropping message: " + msg);
        }
    }
    if (!ttsRunning.load() && threads_running.load()) {
        debug_echo("[enqueue_tts] TTS launching thread msd:" + msg);
        std::thread t(process_tts_queue);
        t.detach();
    }
}

void welcome_new_user(const std::string &user) {
    std::string tts_text = "Bienvenido " + user;
    enqueue_tts(tts_text, DEFAULT_LANGUAGE);
}

const std::string replace_emoticons_regex(const std::string& input) {
    std::ostringstream result;
    std::sregex_iterator currentMatch(input.begin(), input.end(), regex_emotes);
    std::sregex_iterator lastMatch;

    size_t lastPos = 0;
    while (currentMatch != lastMatch) {
        const std::smatch& match = *currentMatch;
        result << input.substr(lastPos, match.position() - lastPos);

        auto it = text_to_emoji.find(match.str());
        if (it != text_to_emoji.end())
            result << it->second;
        else
            result << match.str(); // fallback por si acaso

        lastPos = match.position() + match.length();
        ++currentMatch;
    }

    result << input.substr(lastPos); // resto del string
    return result.str();
}

bool contains_blacklisted(const std::string &text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return std::regex_search(lower, regex_word_blacklist);
}

bool contains_url(const std::string &text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return std::regex_search(lower, regex_url);
}

const std::string to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

const std::string detect_language(const std::string &text) {
    // Check regex patterns on the entire text
    bool is_greeting = std::regex_search(text, regex_greeting);
    bool is_funny_trigger = std::regex_search(text, regex_laugh);
    bool is_thanks = std::regex_search(text, regex_thanks);
    bool is_appreciation = std::regex_search(text, regex_appreciation);
    bool is_epic = std::regex_search(text, regex_epic);
    bool is_wtf = std::regex_search(text, regex_wtf);
    bool is_f = std::regex_search(text, regex_f);
    bool is_tic_tac = std::regex_search(text, regex_tic_tac);


    constexpr int max_words = 25;
    constexpr int max_hits = 6;
    int eng_count = 0, spa_count = 0;
    std::string buffer = text;
    buffer.reserve(text.size());
    std::string word;
    int words_processed = 0;
    size_t pos = 0;

    while (pos < buffer.size() && words_processed < max_words) {
        while (pos < buffer.size() && buffer[pos] == ' ') ++pos;
        size_t start = pos;
        while (pos < buffer.size() && buffer[pos] != ' ') ++pos;
        if (start == pos) break;
        word = buffer.substr(start, pos - start);
        std::transform(word.begin(), word.end(), word.begin(), ::tolower);
        if (spanish_words.count(word)) {
            if (++spa_count >= max_hits) return "es";
        }
        if (english_words.count(word)) {
            if (++eng_count >= max_hits) return "en";
        }
        ++words_processed;
    }
    debug_echo("detect_language: (spa:" + std::to_string(spa_count) + ")-(en:" + std::to_string(eng_count) + ")");
    if (eng_count == 0 && spa_count == 0){ 
        if(is_greeting || is_funny_trigger || is_thanks || is_appreciation || is_epic || is_wtf || is_f || is_tic_tac){
            return "es";
        }
        return "nil";
    }
    return (spa_count >= eng_count) ? "es" : "en";
}

void process_tts(const std::string& user, const std::string& tts_msgs,
                bool has_privileges, bool first_msg, bool returning_chatter,
                const std::string& user_type, bool is_highlighted, const std::string& lang) {
    debug_echo("process_tts: lang=" + lang);
    if(user == "thebot")
    if (lang == "nil") {
        debug_echo("lang nil returning no tts...");
        return;
    }
    std::string normalized_msgs = to_lower(tts_msgs);
    normalized_msgs.reserve(tts_msgs.size());
    std::string tts_text;

    if (lang == "en") {
        tts_text = user + " says: " + tts_msgs;
    } else {
        tts_text = user + " dice: " + tts_msgs;
    }
    enqueue_tts(tts_text, lang);

    if (is_highlighted) {
        tts_text = (lang == "en")
            ? "Wow, " + user + ", your message shines bright with those channel points!"
            : "¡Guau, " + user + ", tu mensaje brilla con esos puntos del canal!";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        enqueue_tts(tts_text, lang);
        return;
    }else if (first_msg) {
        tts_text = (lang == "en")
            ? "Welcome " + user + "! Use !s to talk with me!"
            : "¡Bienvenido " + user + "! ¡Usa !s para hablar conmigo!";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        enqueue_tts(tts_text, lang);
        return;
    }
    if (returning_chatter) {
        tts_text = (lang == "en")
            ? "Hey " + user + ", long time no see! Where’ve you been hiding?"
            : "¡Ey " + user + ", cuánto tiempo! ¿Dónde te habías metido?";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        enqueue_tts(tts_text, lang);
        return;
    }
    // if (!user_type.empty()) {
    //     tts_text = (lang == "en")
    //         ? "Wow, " + user + ", a " + user_type + "! Thanks for dropping by!"
    //         : "¡Guau, " + user + ", un " + user_type + "! ¡Gracias por pasar!";
    //     std::this_thread::sleep_for(std::chrono::milliseconds(1));
    //     enqueue_tts(tts_text, lang);
    //     return;
    // }
    if (std::regex_search(normalized_msgs, regex_greeting)) {
        tts_text = (lang == "en") ? "Hey " + user + ", nice to see you!" : "¡Hola " + user + ", qué bueno verte!";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        enqueue_tts(tts_text, lang);
        return;
    }
    if (std::regex_search(normalized_msgs, regex_laugh)) {
        tts_text = (lang == "en")
            ? "That’s a good laugh, " + user + "!"
            : "¡" + user + ", eso merece una carcajada épica!, JAJAJAJAJAJAJAJAJA";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        enqueue_tts(tts_text, lang);
    } else if (std::regex_search(normalized_msgs, regex_thanks)) {
        tts_text = (lang == "en")
            ? "You’re welcome, " + user + ", anytime... while is not my time xD!"
            : "¡De nada, " + user + ", siempre un placer ayudar... siempre con esperanza de recibir unos bits!";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        enqueue_tts(tts_text, lang);
    } else if (std::regex_search(normalized_msgs, regex_appreciation)) {
        tts_text = (lang == "en")
            ? "Nice one, " + user + ", i always trust in you, and your wallet!"
            : "¡Bien hecho, " + user + ", eso está genial, ahora saca la billetera!";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        enqueue_tts(tts_text, lang);
    } else if (std::regex_search(normalized_msgs, regex_epic)) {
        tts_text = (lang == "en")
            ? "That’s epic, " + user + ", and you rock!"
            : "¡" + user + ", eso es épico, como tú!";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        enqueue_tts(tts_text, lang);
    } else if (std::regex_search(normalized_msgs, regex_wtf)) {
        tts_text = (lang == "en")
            ? "What the heck, " + user + ", do you see that!"
            : "¡" + user + ", qué locura es esa, ni tu te lo crees!";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        enqueue_tts(tts_text, lang);
    } else if (std::regex_search(normalized_msgs, regex_tic_tac)) {
        tts_text = (lang == "en")
            ? "Tic Tac Tic tac, " + user + ", the time is relative!"
            : "¡" + user + ", Tic tac Tic tac, el tiempo es relativo!";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        enqueue_tts(tts_text, lang);
    } else if (std::regex_search(normalized_msgs, regex_f)) {
        tts_text = (lang == "en")
            ? "No way, " + user + ", this never happen before!!!"
            : "¡" + user + ", Es to nunca antes pasó, lo juro!";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        enqueue_tts(tts_text, lang);
    }
}

const std::string hex_to_ansi(const std::string &hex) {
    if (hex[0] == '#') {
        int r = stoi(hex.substr(1,2), nullptr, 16);
        int g = stoi(hex.substr(3,2), nullptr, 16);
        int b = stoi(hex.substr(5,2), nullptr, 16);
        int r_step = r / 51;
        int g_step = g / 51;
        int b_step = b / 51;
        int ansi_index = 16 + (36 * r_step) + (6 * g_step) + b_step;
        return "\033[1;38;5;" + std::to_string(ansi_index) + "m";
    }
    return "";
}

SDL_Color hex_to_rgb(const std::string& hex) {
    SDL_Color color = white;
    
    if (hex[0] == '#') {
        // Remove the '#' character
        std::string hex_value = hex.substr(1);

        // Convert each pair of hex digits to an integer
        unsigned int r, g, b;
        std::stringstream ss;
        ss << std::hex << hex_value.substr(0, 2);
        ss >> r;
        ss.clear();
        ss << std::hex << hex_value.substr(2, 2);
        ss >> g;
        ss.clear();
        ss << std::hex << hex_value.substr(4, 2);
        ss >> b;

        color.r = r;
        color.g = g;
        color.b = b;
    }

    return color;
}

// const std::string strip_ansi(const std::string& s) {
//     std::regex ansi_regex(R"(\x1B[@-_][0-?]*[ -/]*[@-~])");
//     return std::regex_replace(s, ansi_regex, "");
// }

const std::string extract_tag_value(const std::string& tags, const std::string& key) {
    std::regex regex_tag_value(key + "=([^;]*)");
    std::smatch match;
    if (std::regex_search(tags, match, regex_tag_value)) {
        return match[1];
    }
    return "";
}

const std::vector<ChatRow> split_to_rows(const std::string &message, int term_cols) {
    std::vector<ChatRow> rows;
    std::string current_row;
    current_row.reserve(256); // Preallocate
    int row_width = 0;
    bool is_first_row = true;
    size_t current_col = 0;
    // size_t pos_colon = message.find(":");

    while (current_col < message.size()) {
        size_t next_space = message.find(' ', current_col);
        if (next_space == std::string::npos) next_space = message.size();
        std::string word = message.substr(current_col, next_space - current_col);
        int word_length = word.size();// * font_width;

        if (!current_row.empty() && row_width + word_length + 1 > term_cols) {
            rows.push_back({std::move(current_row), is_first_row });
            current_row.clear();
            current_row.reserve(256);
            current_row = word;
            row_width = word_length;
            is_first_row = false;
        } else {
            if (!current_row.empty()) {
                current_row += " ";
                row_width++;
            }
            current_row += word;
            row_width += word_length;
        }
        current_col = next_space + 1;
    }

    if (!current_row.empty()) {
        rows.push_back({std::move(current_row), is_first_row});
    }
    return rows;
}
void welcome_new_follow(const std::string &user) {
    std::string tts_text = "¡Hey!, ¡Gracias " + user + " por ese follow!";
    enqueue_tts(tts_text, DEFAULT_LANGUAGE);
}

bool check_spam(const std::string& user) {
    auto& state = user_tts_states[user];
    std::time_t now = std::time(nullptr);

    if (state.banned) return false;
    if (state.blocked_until > now) return false;

    // limpiar mensajes viejos (más de 10 segundos)
    while (!state.timestamps.empty() && now - state.timestamps.front() > TIME_WINDOW){
        state.timestamps.pop_front();
    }

    state.timestamps.push_back(now);

    if (state.warning_level >= 3) {
        return false; // usuario bloqueado permanentemente
    }

    if (state.timestamps.size() > SPAM_THRESHOLD) {
        state.warning_level += 1;
        if (state.warning_level == 1) {
            state.blocked_until = now + 300;
            enqueue_tts(user + ", estás enviando demasiados mensajes. Espera 5 minutos.", "es");
            state.last_warning_time = now;
        } else if (state.warning_level == 2 && now - state.last_warning_time > FIRST_COOLDOWN) {
            enqueue_tts(user + ", segunda advertencia. Espera 10 minutos.", "es");
            state.blocked_until = now + 600;
            state.last_warning_time = now;
        } else if (state.warning_level == 3 && now - state.last_warning_time > SECOND_COOLDOWN) {
            enqueue_tts(user + ", has sido bloqueado del TTS por spam.", "es");
            state.banned = true;
            state.last_warning_time = now;
        }
        return false;
    }
    return true;
}

void process_privmsg(const std::string &line) {
    
    std::smatch match;
    SDL_Color rgb_color;
    if (!std::regex_search(line, match, regex_tags)) return;
    std::string tags = match[1];

    std::string user = extract_tag_value(tags, "display-name");

    if (!check_spam(user)) return;

    std::string color = extract_tag_value(tags, "color");
    if (color.empty()) {
        color = "#FFFFFF";
    }
    rgb_color = hex_to_rgb(color);
    std::string ansi_color = hex_to_ansi(color);
    
    if (!std::regex_search(line, match, regex_msg)) return;
    std::string raw_msg = match[1];
    
    if (std::regex_match(user, regex_stream_elements))
    {  
        if (std::regex_search(raw_msg, match, regex_new_follow)) {
            std::string nombre_usuario = match[1];
            return welcome_new_follow(nombre_usuario);
        }
        return;
    }
    if (contains_blacklisted(raw_msg) || contains_url(raw_msg)) {
        debug_echo("blacklist found...");
        return;
    }

    std::string badges_raw = extract_tag_value(tags, "badges");
    debug_echo("badges_raw: " + badges_raw);
    std::string first_msg = extract_tag_value(tags, "first-msg");
    std::string returning_chatter = extract_tag_value(tags, "returning-chatter");
    std::string user_type = extract_tag_value(tags, "user-type");
    std::string flags = extract_tag_value(tags, "flags");
    std::string emotes = extract_tag_value(tags, "emotes");

    bool is_highlighted = (flags.find("highlighted") != std::string::npos);
    bool is_first_msg = (first_msg == "1");
    bool is_returning = (returning_chatter == "1");

    std::string mod_status, sub_status, broadcaster_status, vip_status;
    std::string admin_status, staff_status, partner_status, global_mod_status;

    if (std::regex_search(badges_raw, regex_tag_moderator)) mod_status = "🛡️";
    if (std::regex_search(badges_raw, regex_tag_subscriber)) sub_status = "★";
    if (std::regex_search(badges_raw, regex_tag_broadcaster)) broadcaster_status = "👑";
    if (std::regex_search(badges_raw, regex_tag_vip)) vip_status = "💜";
    if (std::regex_search(badges_raw, regex_tag_admin)) admin_status = "🔧";
    if (std::regex_search(badges_raw, regex_tag_staff)) staff_status = "🖥️";
    if (std::regex_search(badges_raw, regex_tag_partner)) partner_status = "🏆";
    if (std::regex_search(badges_raw, regex_tag_global_mod)) global_mod_status = "🌍";

    std::string badges = broadcaster_status + mod_status + sub_status + partner_status +
                        vip_status + admin_status + staff_status + global_mod_status;

    int badges_length = 0;
    for (const auto &par : iconos) {
        size_t pos = badges.find(par.first);
        while (pos != std::string::npos) {
            badges_length += par.second;
            pos = badges.find(par.first, pos + par.first.size());
        }
    }

    std::string text_badge = "(" + std::string(badges_length, 'A') + ")";
    std::string mensaje_total = text_badge + user + ": " + raw_msg;
    int term_cols = (sdl_available) ? window_width / font_width : get_terminal_width();
    std::vector<ChatRow> row = split_to_rows(mensaje_total, term_cols);
    std::string msg_language = detect_language(raw_msg);
    debug_echo("msg_language:"+msg_language);
    ChatMessage chat_msg;
    chat_msg.badges = badges;
    chat_msg.badges_length = badges_length;
    chat_msg.rgb_color = rgb_color;
    chat_msg.ansi_color = ansi_color;
    chat_msg.user = user;
    chat_msg.msg_language = msg_language;
    chat_msg.emotes = emotes;
    chat_msg.rows = std::move(row);
    chat_msg.timestamp = std::time(nullptr);

    {
        std::lock_guard<std::mutex> lock(chat_mutex);
        chat_messages.push_back(chat_msg);
        debug_echo("chat_messages.push_back: "+ raw_msg);
        if (chat_messages.size() > MAX_MESSAGES)
            chat_messages.erase(chat_messages.begin());
    }
    debug_echo("Checking badges: " + badges);
    bool has_privileges = std::regex_search(badges_raw,regex_has_privileges);
    if (has_privileges || std::regex_search(raw_msg, regex_tts) ||
        is_first_msg || is_returning || is_highlighted) {
        debug_echo("User: " + user + "has_privileges: yes");
        std::string tts_msg = raw_msg;
        if (tts_msg.rfind("!s ", 0) == 0)
            {
                tts_msg = raw_msg.substr(3);
            }
        process_tts(chat_msg.user, tts_msg, has_privileges, is_first_msg, is_returning, user_type, is_highlighted, msg_language);
    }else{
        debug_echo("user: " + user + "No tts...");
    }

    {
        std::lock_guard<std::mutex> lock(display_mutex);
        display_needs_update = true;
    }
    display_cv.notify_one();
}
void update_display_console() {
    static char buffer[4096];
    write(STDOUT_FILENO, "\033[?25l", 6);
    if (!DEBUG) {
        write(STDOUT_FILENO, "\033[2J", 4);
    }else{
        return;
    }

    winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int term_rows = w.ws_row;

    time_t now = time(nullptr);
    int row = term_rows;

    std::vector<std::pair<int, std::string>> output_buffer;
    output_buffer.reserve(term_rows * 2);

    {
        std::lock_guard<std::mutex> lock(chat_mutex);
        for (auto msg_it = chat_messages.rbegin(); msg_it != chat_messages.rend() && row > 0; ++msg_it) {
            if (now - msg_it->timestamp > 60) continue;

            std::string badges = msg_it->badges;
            std::string color = msg_it->ansi_color;
            std::string emotes = msg_it->emotes;

            for (auto line_it = msg_it->rows.rbegin(); line_it != msg_it->rows.rend() && row > 0; ++line_it) {
                std::string text = line_it->text;

                if (line_it->first_row) {
                    if (!badges.empty()) {
                        size_t pos_badge = text.find('(');
                        if (pos_badge != std::string::npos && text.find(')', pos_badge) != std::string::npos) {
                            text.replace(pos_badge, text.find(')', pos_badge) - pos_badge + 1, badges);
                        }
                    }
                    if (!emotes.empty()) {
                        text = replace_emoticons_regex(text);
                    }
                    size_t pos_colon = text.find(": ");
                    if (pos_colon != std::string::npos) {
                        text.replace(pos_colon, 2, "\033[0m: ");
                    }
                    text = color + text;
                }

                std::smatch match;
                if (std::regex_search(text, match, regex_quoted)) {
                    std::string quoted = match[2];
                    size_t first_quote = text.find(match[1].str());
                    size_t last_quote = text.find(match[1].str(), first_quote + 1);
                    if (first_quote != std::string::npos && last_quote != std::string::npos) {
                        text = text.substr(0, first_quote) + "\033[3m" + quoted + "\033[0m" + text.substr(last_quote + 1);
                    }
                } else {
                    for (char& c : text) {
                        if (c == '<' || c == '>') c = '\\';
                    }
                }

                // Use size_t for len to match sizeof(buffer)
                size_t len = snprintf(buffer, sizeof(buffer), "\033[%d;1H%s", row, text.c_str());
                if (len < sizeof(buffer)) { // No need to check len > 0 as snprintf returns >= 0
                    output_buffer.emplace_back(row, std::string(buffer, len));
                }
                --row;
            }
        }
    }

    for (const auto& [row, text] : output_buffer) {
        write(STDOUT_FILENO, text.c_str(), text.size());
    }
    {
        std::lock_guard<std::mutex> lock(display_mutex);
        display_needs_update = false;
    }
    display_cv.notify_one();
}

void update_display_sdl() {
    std::lock_guard<std::mutex> lock(sdl_mutex);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    time_t now = time(nullptr);
    int y = window_height - (font_height * 2);
    // const int line_height = 16;

    std::lock_guard<std::mutex> chat_lock(chat_mutex);
    for (auto msg_it = chat_messages.rbegin(); msg_it != chat_messages.rend() && y < window_height; ++msg_it) {
        if (now - msg_it->timestamp > 60) continue;

        std::string badges = "AAA";//msg_it->badges;
        SDL_Color color = msg_it->rgb_color;
        std::string emotes = msg_it->emotes;

        for (auto line_it = msg_it->rows.rbegin(); line_it != msg_it->rows.rend() && y < window_height; ++line_it) {
            std::string text = line_it->text;//.substr(0, 36); // Limitar a 36 columnas

            if (line_it->first_row) {
                if (!badges.empty()) {
                    size_t pos_badge = text.find('(');
                    if (pos_badge != std::string::npos && text.find(')', pos_badge) != std::string::npos) {
                        text.replace(pos_badge, text.find(')', pos_badge) - pos_badge + 1, badges);
                    }
                }
                // if (!emotes.empty()) text = replace_emoticons_regex(text);
                // size_t pos_colon = text.find(": ");
                // if (pos_colon != std::string::npos) text.replace(pos_colon, 2, ": ");
                // render_text(text, 0, y, color);
                // const int char_width = 10;
                size_t pos_colon = text.find(": ");
                if (pos_colon != std::string::npos) {
                    // Renderizar la parte antes de ": " con el color original
                    std::string before_colon = text.substr(0, pos_colon);
                    render_text(before_colon, 0, y, color);

                    // Renderizar la parte desde ": " (incluyendo el colon) con color blanco
                    std::string after_colon = text.substr(pos_colon);
                    render_text(after_colon, before_colon.length() * font_width, y, white); // Ajustar la posición x
                } else {
                    // Si no hay ": ", renderizar todo con el color original
                    render_text(text, 0, y, white);
                }
            } else {
                std::smatch match;
                if (std::regex_search(text, match, regex_quoted)) {
                    std::string quoted = match[2];
                    size_t first_quote = text.find(match[1].str());
                    size_t last_quote = text.find(match[1].str(), first_quote + 1);
                    if (first_quote != std::string::npos && last_quote != std::string::npos) {
                        render_text(text.substr(0, first_quote), 0, y, {255, 255, 255, 255});
                        render_text(quoted, first_quote * 8, y, {200, 200, 200, 255}); // Itálico simulado
                        render_text(text.substr(last_quote + 1), (last_quote + 1) * 8, y, {255, 255, 255, 255});
                    }
                } else {
                    for (char& c : text) if (c == '<' || c == '>') c = '\\';
                    render_text(text, 0, y, {255, 255, 255, 255});
                }
            }
            y -= font_height;
        }
    }

    SDL_RenderPresent(renderer);
    {
        std::lock_guard<std::mutex> lock(display_mutex);
        display_needs_update = false;
    }
    display_cv.notify_one();
}

void render_alert(SDL_Renderer* renderer, SDL_Texture* imgTexture, SDL_Texture* textTexture, SDL_Rect textRect) {
    Uint32 start = SDL_GetTicks();

    while (alert_running) {
        Uint32 now = SDL_GetTicks();
        Uint32 elapsed = now - start;
        if (elapsed > DISPLAY_DURATION_MS) break;

        float t = elapsed / static_cast<float>(DISPLAY_DURATION_MS);
        Uint8 alpha = 255;
        if (t < 0.2f) {
            alpha = static_cast<Uint8>(lerp(0, 255, t / 0.2f));
        } else if (t > 0.8f) {
            alpha = static_cast<Uint8>(lerp(255, 0, (t - 0.8f) / 0.2f));
        }

        SDL_SetTextureAlphaMod(imgTexture, alpha);
        SDL_SetTextureAlphaMod(textTexture, alpha);

        float wobble = std::sinf(now * 0.01f) * 4.0f;
        float scale = 0.5f + 0.05f * std::sinf(now * 0.001f);

        int iw = static_cast<int>(SCREEN_WIDTH * scale);
        int ih = static_cast<int>(SCREEN_HEIGHT * scale);
        int ix = static_cast<int>((SCREEN_WIDTH - iw) / 2 + wobble);
        int iy = static_cast<int>((SCREEN_HEIGHT - ih) / 2 + wobble);
        SDL_Rect imgRect = { ix, iy, iw, ih };

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, imgTexture, nullptr, &imgRect);
        SDL_RenderCopy(renderer, textTexture, nullptr, &textRect);
        SDL_RenderPresent(renderer);

        // Actual OS-level sleep (~60fps, ultra low CPU)
        // std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    /*if (std::this_thread.joinable())
    {
        std::this_thread.join();
    }*/
}


void show_alert(){
    {
        std::unique_lock<std::mutex> lock(alert_mutex);
        alert_running = true;
        debug_echo("[DEBUG] show_alert");
        if (alert_type == 1)
        {
            SDL_Window* alert_wi = SDL_CreateWindow("Follow Alert", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                                  SCREEN_WIDTH, SCREEN_HEIGHT, 0);
            SDL_Renderer* alert_renderer = SDL_CreateRenderer(alert_wi, -1, SDL_RENDERER_ACCELERATED);
            // Load image
            std::string imgPath = expand_path(IMAGE_PATH);
            SDL_Surface* imgSurf = IMG_Load(imgPath.c_str());
            SDL_Texture* imgTexture = SDL_CreateTextureFromSurface(alert_renderer, imgSurf);
            SDL_FreeSurface(imgSurf);
            //textura text
            TTF_Font* al_font = TTF_OpenFont(FONT_PATH, 48);
            SDL_Color white = {255, 255, 255, 255};
            SDL_Surface* textSurf = TTF_RenderText_Blended(font, "Thank you follower", white);
            SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurf);
            SDL_Rect textRect = {
                SCREEN_WIDTH / 2 - textSurf->w / 2,
                SCREEN_HEIGHT - 120,
                textSurf->w,
                textSurf->h
            };
            SDL_FreeSurface(textSurf);

            render_alert(alert_renderer, imgTexture, textTexture, textRect);
            alert_running = false;
            SDL_DestroyTexture(imgTexture);
            SDL_DestroyTexture(textTexture);
            // Mix_FreeMusic(music);
            TTF_CloseFont(al_font);
            SDL_DestroyRenderer(alert_renderer);
            SDL_DestroyWindow(alert_wi);
            debug_echo("[DEBUG] destroy alert");
        }
    } 
}


void welcome_new_sub(const std::string &user, const std::string &sub_plan) {
    std::string plan_text = (sub_plan == "1000") ? "Tier 1" : (sub_plan == "2000") ? "Tier 2" : "Tier 3";
    std::string tts_text = "¡Gracias " + user + " por suscribirte con " + plan_text + "!";
    enqueue_tts(tts_text, DEFAULT_LANGUAGE);
}

void welcome_resub(const std::string &user, const std::string &months, const std::string &sub_plan) {
    std::string plan_text = (sub_plan == "1000") ? "Tier 1" : (sub_plan == "2000") ? "Tier 2" : "Tier 3";
    std::string tts_text = "¡" + user + " ha resubscrito por " + months + " meses con " + plan_text + "! ¡Gracias!";
    enqueue_tts(tts_text, DEFAULT_LANGUAGE);
}

void welcome_subgift(const std::string &user, const std::string &recipient) {
    std::string tts_text = "¡" + user + " ha regalado una suscripción a " + recipient + "! ¡Qué generoso!";
    enqueue_tts(tts_text, DEFAULT_LANGUAGE);
}

void welcome_raid(const std::string &user, const std::string &viewer_count) {
    std::string tts_text = "¡" + user + " nos ha raideado con " + viewer_count + " viewers! ¡Bienvenidos todos!";
    enqueue_tts(tts_text, DEFAULT_LANGUAGE);
}

void notify_roomstate(const std::string &tags) {
    std::string emote_only = extract_tag_value(tags, "emote-only");
    std::string slow = extract_tag_value(tags, "slow");
    std::string followers_only = extract_tag_value(tags, "followers-only");
    std::string tts_text;
    try {
        if (emote_only == "1") {
            debug_echo("emote_only:"+emote_only);
            {
                std::unique_lock<std::mutex> lock(alert_mutex);
                alert_type = 1;
                has_to_show_alert = true;
            }
            tts_text = "El chat ahora es solo emotes.";
        } else if (slow != "0" && slow.size() > 0) {
            debug_echo("modo lento:"+slow);
            tts_text = "El chat está en modo lento, " + slow + " segundos.";
        } else if (followers_only.size() > 0 && followers_only !="0" ) {
            debug_echo("followers_only:"+followers_only);
            tts_text = "El chat es solo para seguidores, con más de " + followers_only + " minutos siguiendo el canal.";
        } else {
            tts_text = "El chat ha vuelto a la normalidad.";
        }
        enqueue_tts(tts_text, DEFAULT_LANGUAGE);
    } catch (const std::invalid_argument& e) {
        debug_echo("[ERR] Invalid argument: " + std::string(e.what()));
    } catch (const std::out_of_range& e) {
        debug_echo("[ERR] Out of range: " + std::string(e.what()));
    } catch (const std::exception& e) {
        debug_echo("[ERR] Unexpected error: " + std::string(e.what()));
    }
    
}

void handle_irc_message(const std::string& line, int& sockfd) {
    if (line.find("PRIVMSG " + CHANNEL) != std::string::npos) {
        process_privmsg(line);
        return;
    }

    if (line.find("PING") == 0) {
        if (DEBUG.load())
        {
           std::cout <<  "PONG :tmi.twitch.tv\r\n" << std::endl;
           return;
        }
        send(sockfd, "PONG :tmi.twitch.tv\r\n", 21, 0);
        return;
    }

    std::regex join_regex(R"(:([^!]+)!.* JOIN )" + CHANNEL);
    std::smatch match;
    if (std::regex_search(line, match, join_regex)) {
        std::string user = match[1];
        if (user != CHANNEL.substr(1) && user != "streamelements" && user != NICK && user.find("justinfan") != 0) {
            welcome_new_user(user);
        }
        return;
    }

    if (line.find("USERNOTICE " + CHANNEL) != std::string::npos) {
        if (std::regex_search(line, match, regex_tags)) {
            std::string tags = match[1];
            std::string msg_id = extract_tag_value(tags, "msg-id");
            std::string user = extract_tag_value(tags, "display-name");
            std::string months = extract_tag_value(tags, "msg-param-cumulative-months");
            std::string sub_plan = extract_tag_value(tags, "msg-param-sub-plan");
            std::string recipient = extract_tag_value(tags, "msg-param-recipient-display-name");
            std::string viewer_count = extract_tag_value(tags, "msg-param-viewerCount");

            if (msg_id == "sub") {
                welcome_new_sub(user, sub_plan);
            } else if (msg_id == "resub") {
                welcome_resub(user, months, sub_plan);
            } else if (msg_id == "subgift") {
                welcome_subgift(user, recipient);
            } else if (msg_id == "raid") {
                welcome_raid(user, viewer_count);
            }
        }
        return;
    }

    if (line.find("ROOMSTATE " + CHANNEL) != std::string::npos) {
        if (std::regex_search(line, match, regex_tags)) {
            std::string tags = match[1];
            notify_roomstate(tags);
        }
    }
}

// ==========================
// Conexión IRC
// ==========================
int conectar_socket(const std::string &server, int port) {
    int sockfd;
    struct hostent *he;
    struct sockaddr_in their_addr;

    if ((he = gethostbyname(server.c_str())) == nullptr) {
        std::cerr << "Error: no se pudo obtener host." << std::endl;
        exit(1);
    }
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }
    their_addr.sin_family = AF_INET;
    their_addr.sin_port = htons(port);
    their_addr.sin_addr = *((struct in_addr *)he->h_addr);
    memset(&their_addr.sin_zero, '\0', 8);

    if (connect(sockfd, (struct sockaddr *)&their_addr, sizeof(struct sockaddr)) == -1) {
        perror("connect");
        exit(1);
    }
    return sockfd;
}

void connect_to_twitch() {
    int sockfd = conectar_socket(SERVER, PORT);

    auto send_line = [sockfd](const std::string& line) {
        std::string s = line + "\r\n";
        send(sockfd, s.c_str(), s.size(), 0);
    };

    send_line("NICK " + NICK);
    send_line("JOIN " + CHANNEL);
    send_line("CAP REQ :twitch.tv/tags");
    send_line("CAP REQ :twitch.tv/membership");
    send_line("CAP REQ :twitch.tv/commands");

    auto last_ping = std::chrono::steady_clock::now();
    const auto ping_interval = std::chrono::seconds(300);

    char buffer[4096];
    std::string pending;
    pending.reserve(8192);

    struct pollfd pfd;
    pfd.fd = sockfd;
    pfd.events = POLLIN;

    try {
        while (threads_running) {
            int ret = poll(&pfd, 1, 500); // 500ms timeout

            if (ret > 0 && (pfd.revents & POLLIN)) {
                int numbytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
                if (numbytes <= 0) break;

                buffer[numbytes] = '\0';
                pending += buffer;

                size_t pos;
                while ((pos = pending.find("\r\n")) != std::string::npos) {
                    std::string line = pending.substr(0, pos);
                    pending.erase(0, pos + 2);
                    debug_echo("Received: " + line);
                    handle_irc_message(line, sockfd);
                }

                auto now = std::chrono::steady_clock::now();
                if (now - last_ping >= ping_interval) {
                    send_line("PING :tmi.twitch.tv");
                    last_ping = now;
                }
            } else if (ret < 0) {
                std::cerr << "poll() error.\n";
                break;
            }
            // else: timeout, simplemente continuar
        }
    } catch (...) {
        threads_running = false;
        close(sockfd);
        throw;
    }

    threads_running = false;
    close(sockfd);
}

std::thread start_console_refresher_thread() {
    return std::thread([] {
        std::unique_lock<std::mutex> lock(display_mutex);
        while (threads_running) {
            display_cv.wait(lock, [] {
                return display_needs_update || !threads_running;
            });

            if (!threads_running) break;

            lock.unlock();
            update_display_console();
            lock.lock();

            display_needs_update = false;
        }
    });
}

std::thread start_irc_thread() {
    return std::thread([] {
        std::unique_lock<std::mutex> lock(display_mutex);
        while (threads_running) {
            display_cv.wait(lock, [] {
                return display_needs_update || !threads_running;
            });

            if (!threads_running) break;

            lock.unlock();
            update_display_console();
            lock.lock();

            display_needs_update = false;
        }
    });
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "-debug") DEBUG = true;

    if (system("command -v espeak >/dev/null 2>&1") != 0) {
        std::cerr << "Error: espeak no está instalado. Instálalo con 'sudo apt install espeak'." << std::endl;
        return 1;
    }

    if (!init_sdl()) {
        std::cerr << "SDL2 failed. Using console as fallback." << std::endl;
        sdl_available = false;
    }

    threads_running = true;
    std::thread twitch_thread(connect_to_twitch);

    if (!sdl_available) {
        console_refresher = start_console_refresher_thread();
        std::cout << "\e[8;36;50t?25l" << std::endl;
        std::cout << "\e[2J";
        std::cout.flush();
        configurarTerminal();
        write(STDOUT_FILENO, "\033[?25l", 6);
        write(STDOUT_FILENO, title.c_str(), title.size());
        twitch_thread.join();
        threads_running = false;
        display_cv.notify_all();
        if (console_refresher.joinable()) {
            console_refresher.join();
        }
    } else {
        while (threads_running) {
            if (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                    if (ttsRunning) {
                        ttsRunning = false;
                        continue;
                    }
                    threads_running = false;
                }
            }

            if (display_needs_update) {
                update_display_sdl();
                display_needs_update = false;
            }

            if (has_to_show_alert)
            {
                show_alert();
                has_to_show_alert = false;
            }

            SDL_Delay(50); // 20 FPS aprox.
        }

        twitch_thread.join();
        cleanup_sdl();
    }

    return 0;
}