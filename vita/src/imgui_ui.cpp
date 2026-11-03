/*
 * WoozyLLM Vita - modern minimalist chat UI (ChatGPT-style) on PS Vita.
 *
 * Modes:
 *   ZERO - startup: pure black, big bold "WoozyLLM" title and a centered
 *          input bar (~66% width).
 *   CHAT - once a message exists or the user is typing: clean top-centered
 *          header, auto-scrolling chat bubbles, docked input bar at bottom.
 *
 * The render loop reads AppState only and raises one-shot intent flags
 * (want_input / want_send) consumed by main.c (OSK + inference).
 */

#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <utility>
#include <dirent.h>
#include <string.h>

#include <vita2d.h>
#include <psp2/touch.h>
#include <psp2/io/stat.h>

#include <imgui_vita2d/imgui_vita.h>
#include <imgui_vita2d/imgui_internal.h>   /* GetCurrentWindow, BringWindowToDisplayFront */

#include "imgui_ui.h"
#include "ui_model.h"
#include "assets_icons.h"

extern "C" {
    extern const unsigned char g_font_inter_ttf[];
    extern const size_t g_font_data_ttf_size;
    extern const unsigned char g_font_impact_ttf[];
    extern const size_t g_font_impact_ttf_size;
}

#define VIEWPORT_W 960.0f
#define VIEWPORT_H 544.0f

#define UI_SCALE 1.05f           /* global UI size multiplier (+5%) */

#define HEADER_H    68.0f * UI_SCALE     /* CHAT-mode title zone             */
#define INPUT_ZONE_H 96.0f * UI_SCALE    /* docked input bar (64) + 32px floor gap */
#define PAD         22.0f
#define BUBBLE_PAD  15.0f * UI_SCALE     /* inner padding of a bubble        */
#define BUBBLE_R    16.0f * UI_SCALE     /* corner radius of a bubble (Material) */

namespace {

AppState g_app;
size_t   g_lastMsgCount = 0;         /* follow-newest trigger            */
static bool g_first_sent = false;    /* animation fires only after Send  */

ImFont *g_fontBody   = NULL;         /* chat body text                   */
ImFont *g_fontHeader = NULL;         /* CHAT-mode title                  */
ImFont *g_fontHero   = NULL;         /* ZERO-mode big title              */
ImFont *g_fontSmall  = NULL;         /* sidebar labels (RECENTS, rows)   */

/* UI icon textures, built at init from the embedded 1bpp glyph masks    */
static vita2d_texture *g_texMenu      = NULL;
static vita2d_texture *g_texBack      = NULL;
static vita2d_texture *g_texSend      = NULL;
static vita2d_texture *g_texSettings  = NULL;
static vita2d_texture *g_texLogo      = NULL;   /* WoozyLLM title logo    */

/* single-finger swipe gesture state (touch panel read directly)          */
struct SwipeState {
    bool down = false, scrolling = false;
    bool flicking = false;         /* momentum still decaying after lift */
    float startY = 0.0f;           /* finger Y at gesture start          */
    float lastY = 0.0f;            /* previous finger Y (delta+velocity) */
    float lastX = 0.0f;            /* last finger X (zone ownership)     */
    float velocity = 0.0f;         /* px/sec flick momentum (scroll-down+)*/
};
struct OverscrollState { float offset = 0.0f; };
static SwipeState g_chat_swipe;
static SwipeState g_sidebar_swipe;
static SwipeState g_settings_swipe;
static OverscrollState g_chat_overscroll;
static OverscrollState g_sidebar_overscroll;
static OverscrollState g_settings_overscroll;
static float g_touchSX = 960.0f / 1919.0f;   /* aa-coord -> screen px     */
static float g_touchSY = 544.0f / 1087.0f;

/* ------------------------------------------------------------------ */
/* Elastic overscroll ("rubber band") for a scrollable child window.    */
/*                                                                      */
/* Inputs:                                                              */
/*   touch_active  - a finger is currently on the panel                 */
/*   gesture_started - a swipe gesture is in progress                   */
/*   target_scroll - the raw finger-following scroll position (can be   */
/*                   negative or past scroll_max)                       */
/*   dt            - frame time                                         */
/*                                                                      */
/* Output: st.offset (visual stretch in px, can be +/-).                */
/*                                                                      */
/* The valid scroll range is [0, scroll_max]. Anything the gesture      */
/* pulls *past* either edge is converted into `offset` with resistance  */
/* (logarithmic feel), clamped, then springs back to 0 on release.      */
/* ------------------------------------------------------------------ */
static float g_overscroll_resistance = 0.35f;   /* diminishing feel      */
static float g_overscroll_max        = 90.0f;   /* max stretch (px)      */
static float g_overscroll_spring     = 12.0f;   /* spring-back speed     */

/* flick / momentum constants (px/sec velocities)                          */
static float g_flick_friction    = 8.0f;        /* exp(-k*dt) decay rate  */
static float g_flick_trigger     = 50.0f;       /* min release speed px/s */
static float g_flick_stop        = 30.0f;       /* min speed to keep going */

static void update_overscroll(OverscrollState &st, float target_scroll,
                              float scroll_max, bool touch_active,
                              bool gesture_started, float dt)
{
    if (touch_active && gesture_started) {
        /* finger is stretching the list: derive the rubber-band shift from
         * how far the RAW target goes past either edge (resistance-scaled).
         * target_scroll may be negative or past scroll_max here.           */
        if (target_scroll < 0.0f)
            st.offset = -target_scroll * g_overscroll_resistance;   /* pull down (+ve) */
        else if (target_scroll > scroll_max)
            st.offset = (scroll_max - target_scroll) * g_overscroll_resistance; /* pull up (-ve) */
        else
            st.offset = 0.0f;
    } else {
        /* finger released: spring the existing stretch back to zero.
         * IMPORTANT: never recompute the offset from the (clamped) scroll
         * position here - GetScrollY() is always within [0, scroll_max],
         * so doing so would zero the stretch in a single frame and the
         * smooth bounce-back would never animate.                        */
        st.offset += (0.0f - st.offset) * g_overscroll_spring * dt;
        if (std::fabs(st.offset) < 0.5f)
            st.offset = 0.0f;
    }

    /* hard clamp */
    st.offset = ImClamp(st.offset, -g_overscroll_max, g_overscroll_max);
}

#define TARGET_SIDEBAR_WIDTH (270.0f * UI_SCALE)  /* fully-open sidebar width       */
#define RECENTS_DIR     "ux0:data/woozyllm/recents/"

static void start_new_session(void);   /* fwd: defined below in this TU */
static ChatSession &cur(void);         /* fwd: defined below in this TU */

/* ------------------------------------------------------------------ */
/* LEFT DRAWER SIDEBAR (chat history).                                  */
/*  - animated: current_sidebar_width lerps to 0 (closed) or to         */
/*    TARGET_SIDEBAR_WIDTH (open), starting CLOSED.                     */
/*  - sliding "✕"/"☰" toggle button lives ON the moving right edge      */
/*    of the drawer, so it follows the panel while it slides.           */
/*  - "+ New Chat" button at the top, then a scrollable list of files   */
/*    found in RECENTS_DIR, each rendered as a selectable item.         */
/* ------------------------------------------------------------------ */
static float current_sidebar_width = 0.0f;
static std::vector<std::string> g_history_files;   /* cached scan result  */
static std::vector<std::string> g_history_titles;  /* per-file 1st line    */

/* ------------------------------------------------------------------ */
/* RECENTS PERSISTENCE                                                 */
/*  Plain, length-prefixed text format that survives embedded newlines */
/*  inside message bodies:                                            */
/*    WOOZYREC1\n title\n count\n U <len>\n <len bytes>\n A <len>\n ... */
/*  Files live in ux0:data/woozyllm/recents/.                          */
/* ------------------------------------------------------------------ */

static void ensure_recents_dir()
{
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir("ux0:data/woozyllm", 0777);
    sceIoMkdir(RECENTS_DIR, 0777);
}

static std::string sanitize_stub(const std::string &s)
{
    std::string out;
    for (char ch : s) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')
            out.push_back(ch);
        else if (ch == ' ')
            out.push_back('_');
    }
    if (out.empty())
        out = "chat";
    if (out.size() > 48)
        out = out.substr(0, 48);
    return out;
}

static std::string make_recent_filename(const ChatSession &s)
{
    time_t now  = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tmv);
    static unsigned seq = 0;
    return std::string("woo_") + ts + "_" + sanitize_stub(s.title)
           + std::to_string(seq++) + ".txt";
}

static bool save_session_file(const ChatSession &s, const std::string &fname)
{
    ensure_recents_dir();
    FILE *fp = fopen((RECENTS_DIR + fname).c_str(), "wb");
    if (!fp)
        return false;
    fprintf(fp, "WOOZYREC1\n%s\n%u\n",
            s.title.c_str(), (unsigned)s.messages.size());
    for (const ChatMessage &m : s.messages) {
        fprintf(fp, "%c %u\n", m.is_user ? 'U' : 'A',
                (unsigned)m.text.size());
        fwrite(m.text.data(), 1, m.text.size(), fp);
        fputc('\n', fp);                 /* loader consumes this via fgetc */
    }
    fclose(fp);
    return true;
}

/* cheap 2-line peek used to render the sidebar list without reading a
 * whole file; returns the saved title (filename as a fallback). */
static std::string read_session_title(const std::string &fname)
{
    FILE *fp = fopen((RECENTS_DIR + fname).c_str(), "r");
    if (!fp)
        return fname;
    char mag[32] = {0};
    char line[256];
    std::string title;
    if (fgets(mag, sizeof mag, fp) &&
        strncmp(mag, "WOOZYREC1", 9) == 0 &&
        fgets(line, sizeof line, fp)) {
        title = line;
        if (!title.empty() && title.back() == '\n')
            title.pop_back();
    }
    fclose(fp);
    return title.empty() ? fname : title;
}

/* On-demand lazy load: opens the EXACT filepath given by the sidebar and
 * parses every message (user & assistant). Uses C stdio ("rb") to match
 * the saver ("wb") byte-for-byte; the active buffer is cleared only after
 * a successful open+parse, and the active file pointer is updated last. */
static bool load_session_messages(const std::string &filepath)
{
    /* guard against double-path: never re-prepend RECENTS_DIR when the
     * caller already handed us an absolute/relative recents path */
    std::string full = filepath;
    if (full.find("ux0:") == std::string::npos &&
        full.find(RECENTS_DIR) == std::string::npos)
        full = RECENTS_DIR + filepath;

    FILE *fp = fopen(full.c_str(), "rb");
    if (!fp)
        return false;

    ChatSession loaded;
    char mag[32] = {0};
    if (!fgets(mag, sizeof mag, fp) || strncmp(mag, "WOOZYREC1", 9) != 0) {
        fclose(fp);
        return false;
    }
    char line[256];
    if (!fgets(line, sizeof line, fp)) { fclose(fp); return false; }
    loaded.title = line;
    if (!loaded.title.empty() && loaded.title.back() == '\n')
        loaded.title.pop_back();
    if (loaded.title.empty())
        loaded.title = full;
    if (!fgets(line, sizeof line, fp)) { fclose(fp); return false; }
    unsigned count = (unsigned)strtoul(line, NULL, 10);
    for (unsigned i = 0; i < count; i++) {
        if (!fgets(mag, sizeof mag, fp))
            break;
        bool is_user = (mag[0] == 'U');
        unsigned len = (unsigned)strtoul(mag + 1, NULL, 10);
        std::string text;
        text.resize(len);
        if (len > 0 && fread(&text[0], 1, len, fp) != len)
            break;
        fgetc(fp);                       /* saver's trailing '\n' (fputc) */
        loaded.messages.push_back(ChatMessage(text, is_user));
    }
    fclose(fp);

    /* file parsed OK -> now replace the ACTIVE session in place.
     * `cur()` is the EXACT vector the chat UI iterates (msgs_cp), so the
     * loaded messages appear immediately in the render loop. */
    if (g_app.sessions.empty())
        g_app.sessions.push_back(ChatSession());
    ChatSession &s = cur();
    s.messages.clear();                  /* clear AFTER open             */
    s.messages   = loaded.messages;
    s.title      = loaded.title;
    s.generating = false;
    s.file       = filepath;             /* keep the caller's bare name  */

    g_app.draft.clear();
    g_lastMsgCount = 0;                  /* reset scroll follow          */
    g_first_sent   = true;               /* flip UI into chat mode       */
    return true;
}

static void autosave_current_session()
{
    if (g_app.sessions.empty())
        return;
    ChatSession &s = cur();
    if (s.messages.empty() && s.title == "New chat")
        return;
    if (s.file.empty())
        s.file = make_recent_filename(s);
    save_session_file(s, s.file);
}

static void RefreshHistoryList()
{
    g_history_files.clear();
    g_history_titles.clear();
    DIR *d = opendir(RECENTS_DIR);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        std::string fname = e->d_name;
        g_history_files.push_back(fname);
        g_history_titles.push_back(read_session_title(fname));
    }
    closedir(d);

    /* newest first: filenames start with woo_YYYYMMDD_HHMMSS_ so a plain
     * descending sort puts the most recent chat at the top of the list. */
    std::vector<std::pair<std::string, std::string>> pairs;
    pairs.reserve(g_history_files.size());
    for (size_t i = 0; i < g_history_files.size(); i++)
        pairs.emplace_back(g_history_files[i], g_history_titles[i]);
    std::sort(pairs.begin(), pairs.end(),
              [](const std::pair<std::string, std::string> &a,
                 const std::pair<std::string, std::string> &b) {
                  return a.first > b.first;
              });
    g_history_files.clear();
    g_history_titles.clear();
    for (const auto &p : pairs) {
        g_history_files.push_back(p.first);
        g_history_titles.push_back(p.second);
    }
}

/* Bulletproof title fitter for the RECENTS list.
 *  - Hard cap by CODEPOINT count first (prevents ultra-wide characters like
 *    'W' or long words from defeating the pixel loop / overflowing the panel).
 *  - Then a strict pixel-width trimming loop against the active font.
 *  Slicing is UTF-8 aware so multibyte glyphs are never split in half. */
static std::string ForceTruncateTitle(const std::string &text,
                                      float max_pixel_width)
{
    if (text.empty())
        return "";
    const std::string ellipsis = "...";

    /* count codepoints: lead bytes are <0x80 (ASCII) or >=0xC0 (UTF-8 lead) */
    size_t lead_count = 0;
    for (unsigned char c : text)
        if (c < 0x80 || c >= 0xC0)
            lead_count++;

    /* 1. hard cap: keep the first 12 codepoints + "..." */
    std::string result = text;
    if (lead_count > 15) {
        std::string head;
        size_t cp = 0;
        for (char ch : text) {
            unsigned char uc = (unsigned char)ch;
            if (uc < 0x80 || uc >= 0xC0) {   /* new codepoint starts here    */
                if (cp == 12)
                    break;
                cp++;
            }
            head += ch;
        }
        result = (head.empty() ? ellipsis : head + ellipsis);
    }

    /* 2. strict pixel-width trimming loop: drop WHOLE trailing UTF-8
     * codepoints (never split a multibyte glyph into an invalid sequence). */
    while (result.length() > 4 &&
           ImGui::CalcTextSize(result.c_str()).x > max_pixel_width) {
        if (result.length() >= 3 &&
            result.compare(result.length() - 3, 3, ellipsis) == 0)
            result.erase(result.length() - 3);
        /* back up to the start of the last codepoint */
        size_t cut = result.length() - 1;
        while (cut > 0 && ((unsigned char)result[cut] & 0xC0) == 0x80)
            cut--;
        result.erase(cut);
        result += ellipsis;
    }
    return result;
}

/* Sliding toggle: positioned just past the live right edge of the
 * drawer (current_sidebar_width), so it visibly travels with it. */
static void DrawSidebarToggle()
{
    const float button_x = current_sidebar_width + 10.0f;
    /* vertical center in header, matching the title center (HEADER_H/2 ≈ 35.7) */
    const float button_y = (HEADER_H - 40.0f) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(button_x, button_y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("SidebarToggle", NULL,
                 ImGuiWindowFlags_NoDecoration |
                 ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 18.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(0, 0, 0, 0));
    if (ImGui::Button("##toggle", ImVec2(40, 40))) {
        g_app.sidebar_open = !g_app.sidebar_open;
        if (g_app.sidebar_open)
            RefreshHistoryList();
    }
    /* draw the hamburger (closed) / back-arrow (open) icon over the pill */
    if (vita2d_texture *tex = g_app.sidebar_open ? g_texBack : g_texMenu) {
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 mx = ImGui::GetItemRectMax();
        ImVec2 c  = ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
        const float s = 20.0f;                       /* icon box half-size  */
        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)tex,
            ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s),
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(230, 230, 230, 255));
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(4);

    ImGui::End();
}

static void DrawSidebar(float dt)
{
    /* animate the drawer width toward the open/closed target.
     * Exponential ease-out makes the slide frame-rate independent; dt is
     * clamped so a slow frame (e.g. the history-file refresh right after
     * opening) can never snap the drawer to its target in a single jump. */
    const float target = g_app.sidebar_open ? TARGET_SIDEBAR_WIDTH : 0.0f;
    const float d = std::min(dt, 1.0f / 30.0f);
    const float k = 1.0f - std::exp(-d * 8.0f);
    current_sidebar_width += (target - current_sidebar_width) * k;

    /* Snap to the exact target when the remaining distance is tiny so the
     * lerp never asymptotically stalls just above 0. While CLOSING, any
     * width below the render gate (2.0f) is collapsed straight to 0 so a
     * sub-2px dark sliver never lingers on the left edge. */
    if (!g_app.sidebar_open && current_sidebar_width < 2.0f)
        current_sidebar_width = 0.0f;
    else if (std::fabs(current_sidebar_width - target) < 0.5f)
        current_sidebar_width = target;

    if (current_sidebar_width >= 2.0f) {
        const float w = current_sidebar_width;

        /* own top-level window, opened LAST in the frame -> sits on top
         * of the chat draw stack so messages never bleed over the panel. */
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(w, VIEWPORT_H));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,  ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    ImVec2(0.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1176f, 0.1176f, 0.1176f, 1.0f)); /* #1E1E1E */
        ImGui::PushStyleColor(ImGuiCol_ChildBg,  IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
        ImGui::Begin("GeminiSidebar", NULL,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollbar);

        /* solid opaque drawer background + 1px right divider (#2D2D2D) */
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, VIEWPORT_H),
                          IM_COL32(30, 30, 30, 255));
        dl->AddLine(ImVec2(w - 1, 0), ImVec2(w - 1, VIEWPORT_H),
                    IM_COL32(45, 45, 45, 255));

        const float content_w = w;                 /* full-bleed plates */

        /* =============================================================
         * 1. TOP HEADER (Sequential, top-to-bottom layout)
         * ============================================================= */

        /* "+ New Chat" — full-width button, sharp corners, taller padding */
        const float newchat_h = 42.0f * UI_SCALE;
        const ImGuiStyle &ngstyle = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ngstyle.FramePadding.x,
                                   ngstyle.FramePadding.y * 2.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(255, 102, 0, 38));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 102, 0, 70));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(255, 102, 0, 95));
        if (ImGui::Button("##newchat", ImVec2(content_w, newchat_h))) {
            start_new_session();
            g_app.sidebar_open = false;
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
        /* "+" icon + "New Chat" label over button */
        {
            ImVec2 mn = ImGui::GetItemRectMin();
            ImVec2 mx = ImGui::GetItemRectMax();
            ImFont *f  = g_fontSmall;
            const float fs = f->FontSize;
            ImVec2 c(mn.x + 20.0f, (mn.y + mx.y) * 0.5f);
            const float s = 6.0f, t = 2.0f;
            dl->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y),
                        IM_COL32(255, 140, 60, 255), t);
            dl->AddLine(ImVec2(c.x, c.y - s), ImVec2(c.x, c.y + s),
                        IM_COL32(255, 140, 60, 255), t);
            dl->AddText(f, fs, ImVec2(mn.x + 40.0f, (mn.y + mx.y) * 0.5f - fs * 0.5f),
                        IM_COL32(255, 140, 60, 255), "New Chat");
        }

        /* "Recents" category header */
        ImGui::SetCursorPos(ImVec2(12.0f, ImGui::GetCursorPosY() + 4.0f));
        ImGui::PushFont(g_fontSmall);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 155, 255));
        ImGui::Text("Recents");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        /* =============================================================
         * 2. CHAT HISTORY LIST (Occupies remaining height minus footer)
         * ============================================================= */
        const float settings_h = newchat_h;   /* footer reserved for Settings */
        if (ImGui::BeginChild("HistoryScrollArea", ImVec2(0.0f, -settings_h), false,
                              ImGuiWindowFlags_NoNav |
                              ImGuiWindowFlags_NoScrollbar)) {
            ImDrawList *child_dl = ImGui::GetWindowDrawList();

            /* ---- elastic overscroll on the sidebar list (touch / drag) ---- */
            {
                ImGuiIO &sio      = ImGui::GetIO();
                const float s_max = ImGui::GetScrollMaxY();
                const bool  s_inside = sio.MousePos.x < current_sidebar_width;

                SceTouchData std;
                memset(&std, 0, sizeof(std));
                bool stouch = (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &std, 1) >= 0 &&
                               std.reportNum > 0);
                bool spull = false;

                if (stouch && s_inside) {
                    float smy = sio.MousePos.y;
                    g_sidebar_swipe.lastX = sio.MousePos.x;
                    if (!g_sidebar_swipe.down) {
                        g_sidebar_swipe.down        = true;
                        g_sidebar_swipe.scrolling   = false;
                        g_sidebar_swipe.startY      = smy;
                        g_sidebar_swipe.lastY       = smy;
                    } else {
                        if (!g_sidebar_swipe.scrolling &&
                            std::fabs(smy - g_sidebar_swipe.startY) > 8.0f)
                            g_sidebar_swipe.scrolling = true;
                        float sdy = g_sidebar_swipe.lastY - smy;
                        g_sidebar_swipe.lastY = smy;
                        if (g_sidebar_swipe.scrolling) {
                            float target = ImGui::GetScrollY() + sdy;
                            update_overscroll(g_sidebar_overscroll, target, s_max,
                                              true, true, sio.DeltaTime);
                            ImGui::SetScrollY(ImClamp(target, 0.0f, s_max));
                            spull = true;
                        }
                    }
                } else {
                    g_sidebar_swipe.down = false;
                    g_sidebar_swipe.scrolling = false;
                }

                if (!spull)
                    update_overscroll(g_sidebar_overscroll, ImGui::GetScrollY(), s_max,
                                      false, false, sio.DeltaTime);

                /* rubber-band shift: pulls the list content past its edge */
                if (std::fabs(g_sidebar_overscroll.offset) > 0.1f)
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + g_sidebar_overscroll.offset);
            }

            if (g_history_files.empty()) {
                ImGui::PushFont(g_fontSmall);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 120, 125, 255));
                ImGui::SetCursorPos(ImVec2(12.0f, ImGui::GetCursorPosY()));
                ImGui::Text("No saved chats");
                ImGui::PopStyleColor();
                ImGui::PopFont();
            } else {
                std::string active_file;
                if (!g_app.sessions.empty())
                    active_file = cur().file;

                std::vector<size_t> order;
                order.reserve(g_history_files.size());
                size_t active_idx = SIZE_MAX;
                for (size_t i = 0; i < g_history_files.size(); i++) {
                    if (!active_file.empty() && g_history_files[i] == active_file) {
                        active_idx = i;
                        break;
                    }
                }
                if (active_idx != SIZE_MAX)
                    order.push_back(active_idx);
                for (size_t i = 0; i < g_history_files.size(); i++)
                    if (i != active_idx)
                        order.push_back(i);

                ImGui::PushFont(g_fontSmall);
                const float row_h = 44.0f * UI_SCALE;
                for (size_t n = 0; n < order.size(); n++) {
                    const size_t i = order[n];
                    std::string label =
                        i < g_history_titles.size() ? g_history_titles[i]
                                                    : g_history_files[i];
                    label = ForceTruncateTitle(label, content_w - 12.0f - 10.0f);

                    const bool is_active = (i == active_idx);

                    if (is_active) {
                        ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(255, 255, 255, 20));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(255, 255, 255, 30));
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32(255, 255, 255, 40));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(255, 255, 255, 0));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(255, 255, 255, 10));
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32(255, 255, 255, 16));
                    }

                    char idbuf[32];
                    snprintf(idbuf, sizeof(idbuf), "##hist_%u", (unsigned)i);
                    bool clicked = ImGui::Selectable(idbuf, is_active,
                                                     ImGuiSelectableFlags_None,
                                                     ImVec2(content_w, row_h));
                    ImGui::PopStyleColor(3);

                    ImVec2 rmn = ImGui::GetItemRectMin();
                    ImVec2 rmx = ImGui::GetItemRectMax();

                    /* Win10 accent bar: 3px orange strip at the plate's left */
                    if (is_active) {
                        child_dl->AddRectFilled(ImVec2(rmn.x, rmn.y), ImVec2(rmn.x + 3.0f, rmx.y),
                                                IM_COL32(255, 120, 30, 255));
                    }

                    /* left-aligned label with fixed 12px inset */
                    child_dl->AddText(g_fontSmall, g_fontSmall->FontSize,
                                      ImVec2(rmn.x + 12.0f,
                                             rmn.y + (rmx.y - rmn.y) * 0.5f
                                                 - g_fontSmall->FontSize * 0.5f),
                                      is_active ? IM_COL32(255, 255, 255, 255)
                                                : IM_COL32(224, 224, 224, 255),
                                      label.c_str());

                    if (clicked) {
                        /* load actual messages from disk into the active
                         * buffer (no reset, no metadata-only peek) */
                        if (load_session_messages(g_history_files[i]))
                            g_app.sidebar_open = false;
                    }
                }
                ImGui::PopFont();
            }
        }
        ImGui::EndChild();

        /* =============================================================
         * 3. FOOTER (Settings pinned at the bottom)
         * ============================================================= */
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ImGui::GetStyle().FramePadding.x,
                                   ImGui::GetStyle().FramePadding.y * 2.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(255, 255, 255, 12));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 24));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(255, 255, 255, 36));
        if (ImGui::Button("##settings", ImVec2(content_w, settings_h))) {
            g_app.show_settings = true;
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);
        if (g_texSettings) {
            ImVec2 mn = ImGui::GetItemRectMin();
            ImVec2 mx = ImGui::GetItemRectMax();
            ImVec2 c  = ImVec2(mn.x + 16.0f, (mn.y + mx.y) * 0.5f);
            const float s = 11.0f;
            dl->AddImage(
                (ImTextureID)g_texSettings,
                ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s),
                ImVec2(0, 0), ImVec2(1, 1),
                IM_COL32(205, 205, 208, 255));
        }
        {
            ImVec2 mn = ImGui::GetItemRectMin();
            ImVec2 mx = ImGui::GetItemRectMax();
            ImGui::PushFont(g_fontSmall);
            dl->AddText(g_fontSmall, g_fontSmall->FontSize,
                        ImVec2(mn.x + 38.0f, (mn.y + mx.y) * 0.5f - g_fontSmall->FontSize * 0.5f),
                        IM_COL32(205, 205, 208, 255), "Settings");
            ImGui::PopFont();
        }

        ImGui::End();
        ImGui::PopStyleColor(3);  /* WindowBg, ChildBg, ScrollbarBg      */
        ImGui::PopStyleVar(3);    /* WindowPadding, WindowMinSize, ItemSpacing */
    }

    DrawSidebarToggle();
}

/* ------------------------------------------------------------------ */
/* Palette: pure black + dark greys + warm orange                       */
/* ------------------------------------------------------------------ */
ImVec4 colText()      { return ImVec4(0.878f, 0.878f, 0.878f, 1.0f); }  // #E0E0E0
ImVec4 colDim()       { return ImVec4(0.533f, 0.533f, 0.533f, 1.0f); }  // #888888
ImVec4 colAccent()    { return ImVec4(0.800f, 0.322f, 0.000f, 1.0f); }  // #CC5200
ImVec4 colAccentMid() { return ImVec4(0.580f, 0.230f, 0.000f, 1.0f); }  // #943B00
ImVec4 colUserBg()    { return ImVec4(0.800f, 0.322f, 0.000f, 1.0f); }  // #CC5200
ImVec4 colAiBg()      { return ImVec4(0.102f, 0.102f, 0.102f, 1.0f); }  // #1A1A1A
ImVec4 colField()     { return ImVec4(0.071f, 0.071f, 0.071f, 1.0f); }  // #121212
ImVec4 colBg()        { return ImVec4(0.000f, 0.000f, 0.000f, 1.0f); }  // #000000

ImU32 to32(const ImVec4 &c) { return ImGui::ColorConvertFloat4ToU32(c); }

void apply_theme()
{
    ImGuiStyle &s = ImGui::GetStyle();

    s.WindowRounding     = 10.0f;
    s.ChildRounding      = 10.0f;
    s.FrameRounding      = 9.0f;
    s.PopupRounding      = 10.0f;
    s.ScrollbarRounding  = 8.0f;
    s.GrabRounding       = 6.0f;

    s.WindowPadding      = ImVec2(PAD, PAD);
    s.FramePadding       = ImVec2(16.0f, 12.0f);
    s.ItemSpacing        = ImVec2(10.0f, 12.0f);
    s.ItemInnerSpacing   = ImVec2(8.0f, 8.0f);

    s.WindowBorderSize = 0.0f;
    s.FrameBorderSize  = 1.0f;
    s.ChildBorderSize  = 0.0f;

    ImVec4 *c = s.Colors;
    c[ImGuiCol_WindowBg]             = colBg();
    c[ImGuiCol_ChildBg]              = colField();
    c[ImGuiCol_PopupBg]              = ImVec4(0.08f, 0.08f, 0.10f, 1.0f); // Sleek dark slate
    c[ImGuiCol_FrameBg]              = colField();
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.09f, 0.09f, 0.09f, 1.0f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
    c[ImGuiCol_Button]               = colField();
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.09f, 0.09f, 0.09f, 1.0f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.11f, 0.11f, 0.11f, 1.0f);
    c[ImGuiCol_Text]                 = colText();
    c[ImGuiCol_TextDisabled]         = colDim();
    c[ImGuiCol_Separator]            = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
    c[ImGuiCol_Border]               = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]  = colAccent();
    c[ImGuiCol_NavHighlight]         = colAccent();

    /* Beautiful cohesive dark theme headers & widgets (removes default blue) */
    c[ImGuiCol_TitleBg]              = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.05f, 0.05f, 0.05f, 1.0f);
    c[ImGuiCol_Header]               = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
    c[ImGuiCol_HeaderActive]         = colAccent();
    c[ImGuiCol_SliderGrab]           = colAccent();
    c[ImGuiCol_SliderGrabActive]     = colAccentMid();
    c[ImGuiCol_CheckMark]            = colAccent();
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.65f); // Smooth dark backdrop behind modals

    /* global +5%: scale every spacing / padding / rounding / size metric */
    s.ScaleAllSizes(UI_SCALE);
}

static ChatSession &cur() { return g_app.sessions[g_app.current]; }

void start_new_session()
{
    if (!g_app.sessions.empty())
        autosave_current_session();
    ChatSession s;
    s.title = "New chat";
    g_app.sessions.push_back(s);
    g_app.current = g_app.sessions.size() - 1;
    g_app.draft.clear();
    g_lastMsgCount = 0;
}

void ensure_session()
{
    if (g_app.sessions.empty())
        start_new_session();
}

/* Center `s` around `center` in the current window draw list. `bold`
 * fakes a heavier weight by re-drawing the glyphs 1px around. */
static void drawCentered(ImFont *f, ImVec2 center, const char *s, ImU32 col, bool bold)
{
    if (!s || !*s) return;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float fs  = f->FontSize;
    ImVec2 sz = f->CalcTextSizeA(fs, FLT_MAX, -1.0f, s);
    ImVec2 p(center.x - sz.x * 0.5f, center.y - sz.y * 0.5f);
    if (bold) {
        static const float off[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
        for (int k = 0; k < 4; k++)
            dl->AddText(f, fs, ImVec2(p.x + off[k][0], p.y + off[k][1]), col, s);
    }
    dl->AddText(f, fs, p, col, s);
}

/* ------------------------------------------------------------------ */
/* ChatGPT-style input bar: rounded #121212 pill, left-aligned          */
/* placeholder, circular orange send button on the right.               */
/* ------------------------------------------------------------------ */
void draw_input_bar(float bar_x, float bar_y, float bar_w, float bar_h)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImFont *f = g_fontBody;
    float   fs = f->FontSize;

    const float rad = bar_h * 0.5f;

    /* ---- Pulsing orange "cloud" aura, ONLY on the home/welcome screen ----
     * Hidden as soon as the current chat has any history: once inside an
     * active conversation the input field stays clean. Drawn BEFORE the
     * pill, purely behind it, on the BACKGROUND draw list so it can never
     * overlap the sidebar or any window above.                         */
    if (cur().messages.empty()) {
        ImDrawList *gdl = ImGui::GetBackgroundDrawList();

        /* frame-rate independent time accumulator */
        static float anim_time = 0.0f;
        anim_time += ImGui::GetIO().DeltaTime * 1.2f;   /* breathing speed */

        /* cosine + smoothstep easing: no velocity jumps, smooth 8-bit blend */
        float raw_wave     = (1.0f - cosf(anim_time)) * 0.5f;
        float smooth_pulse = raw_wave * raw_wave * (3.0f - 2.0f * raw_wave);

        const int cloud_layers = 7;
        for (int i = cloud_layers; i >= 1; --i) {
            float expand = (float)i * 6.0f;          /* fixed, no jitter */

            /* quadratic falloff maximizes 8-bit alpha resolution */
            float layer_depth = 1.0f - ((float)(i - 1) / (float)cloud_layers);
            float max_alpha   = 0.22f * (layer_depth * layer_depth);
            float animated    = max_alpha * (0.25f + 0.75f * smooth_pulse);

            gdl->AddRectFilled(
                ImVec2(bar_x - expand, bar_y - expand),
                ImVec2(bar_x + bar_w + expand, bar_y + bar_h + expand),
                IM_COL32(255, 115, 20, (int)(animated * 255.0f)), /* warm orange */
                rad + expand);
        }
    }

    /* standard dark pill background (#121212), drawn over the aura */
    dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h),
                      to32(colField()), rad);

    const float btn     = bar_h - 10.0f;
    const float btn_gap = 5.0f;
    const float btn_x   = bar_x + bar_w - btn_gap - btn;
    const float btn_y   = bar_y + (bar_h - btn) * 0.5f;
    const bool  enabled = !g_app.draft.empty();
    const bool  generating = cur().generating;

    /* placeholder / draft text: single line, horizontally auto-scrolled to
     * the tail, clipped to the capsule so it never bleeds vertically past
     * the rounded borders nor extends underneath the Send button. */
    const char *txt = g_app.draft.empty() ? "Message WoozyLLM..." : g_app.draft.c_str();
    ImU32 col       = g_app.draft.empty() ? to32(colDim()) : to32(colText());

    const float pad_l  = 24.0f;                 /* left air inside the pill */
    const float pad_r  = 16.0f;                 /* internal right margin    */
    const float txt_w  = btn_x - bar_x - pad_l - pad_r;   /* usable width   */
    const float text_w = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, txt).x;
    float       tx     = bar_x + pad_l;
    if (text_w > txt_w)
        tx -= (text_w - txt_w);                 /* auto-scroll to the tail */
    dl->PushClipRect(ImVec2(bar_x + pad_l, bar_y), ImVec2(btn_x - pad_r, bar_y + bar_h), true);
    dl->AddText(f, fs, ImVec2(tx, bar_y + (bar_h - fs) * 0.5f), col, txt);
    dl->PopClipRect();

    /* field (opens OSK) - transparent hit region, spans the pill but stops
     * just before the send button, so the two hit regions never overlap /
     * steal each other's tap. No background of its own: the pill and the
     * aura behind are the only visuals. */
    const float field_w = btn_x - bar_x;
    ImGui::SetCursorScreenPos(ImVec2(bar_x, bar_y));
    ImGui::PushStyleColor(ImGuiCol_Button,         IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   rad);
    bool field_tap = ImGui::Button("##field", ImVec2(field_w, bar_h));
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    /* right button: "Stop" (red square) while generating, else "Send".
     * Styled, drawn after the field, on the same bar without overlapping. */
    ImVec4 btnCol      = generating ? ImVec4(0.80f, 0.20f, 0.20f, 1.0f) /* #CC3333 */
                             : (enabled ? colAccent() : ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
    ImVec4 btnColHover = generating ? ImVec4(0.66f, 0.15f, 0.15f, 1.0f)
                             : (enabled ? colAccentMid() : ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
    ImVec4 btnColActive= btnColHover;

    ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
    ImGui::PushStyleColor(ImGuiCol_Button,         to32(btnCol));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  to32(btnColHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   to32(btnColActive));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, btn * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    bool hit = ImGui::Button("##send", ImVec2(btn, btn));
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    if (field_tap && !generating) {
        g_app.want_input = true;
        g_app.want_send  = false;
    }

    if (hit) {
        if (generating) {
            g_app.want_stop   = true;   /* replaces the send action      */
            g_app.want_input  = false;
            g_app.want_send   = false;
        } else if (enabled) {
            g_app.want_send   = true;
            g_app.want_input  = false;
        }
    }

    /* icon: red stop square while generating, else a send arrow */
    ImVec2 mn = ImGui::GetItemRectMin();
    ImVec2 mx = ImGui::GetItemRectMax();
    ImVec2 c  = ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    float  r  = btn * 0.28f;
    ImU32 iconCol = (generating || enabled) ? to32(ImVec4(1, 1, 1, 1))
                                            : to32(ImVec4(0.55f, 0.55f, 0.55f, 1));
    if (generating) {
        float s = btn * 0.22f;   /* centered square = classic stop icon  */
        dl->AddRectFilled(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), iconCol);
    } else if (g_texSend) {
        float s = btn * 0.30f;   /* embedded send glyph, tinted            */
        dl->AddImage((ImTextureID)g_texSend,
                     ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s),
                     ImVec2(0, 0), ImVec2(1, 1), iconCol);
    } else {
        float s = btn * 0.22f;   /* fallback: classic paper-plane triangle */
        (void)s;
        dl->AddTriangleFilled(ImVec2(c.x, c.y - r),
                              ImVec2(c.x - r, c.y + r),
                              ImVec2(c.x + r, c.y + r), iconCol);
    }

    if (hit && enabled) {
        g_app.want_send  = true;
        g_app.want_input = false;
    }
}

/*                                                                      */
/* The vertical budget is split into FIXED sections:                    */
/*   1) reserve INPUT_ZONE_H at the bottom for the input bar,           */
/*   2) the message list gets exactly the remaining height,             */
/*   3) the input field is drawn strictly below the child afterwards.   */
/*                                                                      */
/* Each message is emitted as a real child widget (bubble layout), so   */
/* ImGui derives the scroll extent from the actual layout; a touch      */
/* swipe drives the native scroll state (SetScrollY / SetScrollHereY).  */
/* ------------------------------------------------------------------ */
void drawChat(float top, float left)
{
    /* 1. Reserve fixed height for the docked input bar: the scrollable
     *    region occupies everything above it, in window (screen) coords. */
    const float available_height = ImGui::GetContentRegionAvail().y
                                 - INPUT_ZONE_H;             /* list height */
    const float bottom = std::max(available_height, 64.0f);  /* slab end Y */

    /* transparent, scrollbar-less, non-navigable container */
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));

    /* uniform 12px air around the message slab + compact item spacing so
     * the bubbles never hug the screen edges or the header. */
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  ImVec2(8.0f, 10.0f));

    /* HARD ZONE SEPARATION: the chat never extends under the open sidebar;
     * it is laid out strictly right of `left` (the live drawer width), so
     * the two panes cannot overlap or steal each other's hitboxes. */
    ImGui::SetCursorPos(ImVec2(left, top));
    ImGui::BeginChild("ChatList", ImVec2(VIEWPORT_W - left, bottom - top), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse |
                      ImGuiWindowFlags_NoNav);

    ImGuiIO &io   = ImGui::GetIO();
    ImFont *f     = ImGui::GetFont();

    /* THREAD-SAFE snapshot of the live session history: taken once per
     * frame BEFORE any bubble is laid out. The render path (and the
     * scroll/follow code below) works exclusively on this copy, so the
     * UI thread can keep drawing previous bubbles even while the LLM
     * worker is busy with context prefill or streaming a new token.   */
    const std::vector<ChatMessage> msgs_cp = cur().messages;

    /* is this the placeholder assistant bubble that is being generated? */
    auto is_typing = [&](const ChatMessage &m, size_t i) -> bool {
        return (!m.is_user && cur().generating &&
                i + 1 == msgs_cp.size() && m.text.empty());
    };

    /* ---- render the messages one bubble at a time:
     *      user  -> right-aligned orange bubble
     *      ai    -> left-aligned  gray bubble
     *      bubbles are drawn directly onto the window draw list using
     *      symmetric inner padding (no nested child widgets needed).     */
    for (size_t i = 0; i < msgs_cp.size(); i++) {
        const ChatMessage &m    = msgs_cp[i];
        const bool        user  = m.is_user;
        const bool        typing = is_typing(m, i);

        ImDrawList *dl   = ImGui::GetWindowDrawList();
        const float time = (float)ImGui::GetTime();
        const ImVec2 win_pos = ImGui::GetWindowPos();
        const float ob = g_chat_overscroll.offset;   /* rubber-band shift    */

        /* ---- Android-style generous symmetric padding on all sides ---- */
        const ImVec2 padding     = ImVec2(20.0f, 14.0f);
        const float  max_bubble_w = ImGui::GetWindowWidth() * 0.80f;
        const float  max_text_w   = max_bubble_w - (padding.x * 2.0f);
        const float  text_h       = f->FontSize;   /* native 28px, no scaling */

        /* content string (typing bubble has no text yet) */
        std::string content;
        if (typing)
            content.clear();
        else if (!m.text.empty())
            content = m.text;
        else
            content = "\xE2\x80\xA6";  /* "…" for a placeholder assistant */

        /* text size -> bubble size including symmetric padding */
        ImVec2 bubble_size;
        if (typing) {
            bubble_size = ImVec2(padding.x * 2.0f + 3.0f * 12.0f + 18.0f,
                                 padding.y * 2.0f + 26.0f);
        } else {
            const ImVec2 ts = f->CalcTextSizeA(text_h, FLT_MAX,
                                               max_text_w, content.c_str());
            bubble_size = ImVec2(ts.x + padding.x * 2.0f,
                                 ts.y + padding.y * 2.0f);
        }

        /* position: right-aligned for user, left-aligned for AI.
         * 16px base margin + 30px inset pulls both sides toward the center
         * so bubbles no longer hug the screen edges. */
        const float margin_side = 16.0f + 30.0f;
        ImVec2 p_min = ImGui::GetCursorScreenPos();
        if (user)
            p_min.x = win_pos.x + ImGui::GetWindowWidth() - bubble_size.x - margin_side;
        else
            p_min.x = win_pos.x + margin_side;
        const ImVec2 p_max(p_min.x + bubble_size.x, p_min.y + bubble_size.y);

        /* smooth Material-style rounded background (rubber-band shifted) */
        const ImU32 bg = to32(user ? colUserBg() : colAiBg());
        dl->AddRectFilled(ImVec2(p_min.x, p_min.y + ob),
                          ImVec2(p_max.x, p_max.y + ob), bg, BUBBLE_R);

        if (typing) {
            /* animated bouncing dots driven by ImGui time */
            const float dot_r = 4.0f, dot_s = 12.0f;
            const float base_x = p_min.x + padding.x + 14.0f;
            const float base_y = p_min.y + bubble_size.y * 0.5f + ob;
            for (int d = 0; d < 3; d++) {
                float off = sinf(time * 6.0f + (float)d * 0.8f) * 4.0f;
                dl->AddCircleFilled(ImVec2(base_x + (float)d * dot_s, base_y + off),
                                    dot_r, IM_COL32(224, 224, 224, 255));
            }
        } else {
            /* text offset exactly by the symmetric padding */
            ImGui::SetCursorScreenPos(ImVec2(p_min.x + padding.x,
                                             p_min.y + padding.y + ob));
            /* NOTE: PushTextWrapPos expects a WINDOW-LOCAL x. In ImGui 1.79
             * CalcWrapWidthForPos adds window->Pos.x - window->Scroll.x to it
             * before comparing against the (screen-space) cursor, so passing an
             * absolute position double-adds the window origin and the text
             * stops wrapping once the chat child is shifted right by the
             * open sidebar. */
            ImGui::PushTextWrapPos(p_min.x + padding.x + max_text_w
                                   - win_pos.x);
            ImGui::PushFont(f);
            ImGui::TextUnformatted(content.c_str());
            ImGui::PopFont();
            ImGui::PopTextWrapPos();
        }

        /* advance cursor Y for the next bubble (14px vertical gap) */
        ImGui::SetCursorScreenPos(ImVec2(win_pos.x + margin_side,
                                         p_max.y + 14.0f));
        ImGui::Dummy(ImVec2(1.0f, 14.0f));
    }

    /* restore default font scale for the rest of the UI */
    ImGui::SetWindowFontScale(1.0f);

    /* ---- bottom padding so the newest message is never glued to the  */
    /*      edge of the input bar.                                      */
    ImGui::Dummy(ImVec2(0.0f, PAD));

    /* ---- direct touch drag (swipe) + elastic overscroll + flick ----
     * The chat and the sidebar each own a SwipeState so a gesture never
     * scrolls both panels at once. When the drawer is open we hand every
     * touch inside its width to the sidebar exclusively.                 */
    SceTouchData td;
    memset(&td, 0, sizeof(td));
    bool touch = (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) >= 0 && td.reportNum > 0);

    /* Zone-ownership X. While a finger is down io.MousePos.x is live; after
     * the backend invalidates it on touch-up we fall back to the last finger
     * X the gesture recorded, so the flick capture on the lift frame still
     * knows the gesture happened on the chat side.                     */
    const float ref_x = touch ? io.MousePos.x : g_chat_swipe.lastX;
    const float max_scroll = ImGui::GetScrollMaxY();
    const bool  over_sidebar = g_app.sidebar_open && ref_x < current_sidebar_width;
    bool pulling = false;   /* gesture is actively stretching the list     */

    if (touch && !over_sidebar) {
        float my = io.MousePos.y;               /* finger Y, pre-mapped */
        g_chat_swipe.lastX = io.MousePos.x;
        if (!g_chat_swipe.down) {
            g_chat_swipe.down        = true;
            g_chat_swipe.scrolling   = false;
            g_chat_swipe.flicking    = false;
            g_chat_swipe.startY      = my;
            g_chat_swipe.lastY       = my;
            g_chat_swipe.velocity    = 0.0f;
        } else {
            if (!g_chat_swipe.scrolling &&
                std::fabs(my - g_chat_swipe.startY) > 8.0f)
                g_chat_swipe.scrolling = true; /* moved => swipe, not tap   */

            /* finger-to-content grab using PER-FRAME DELTAS, not an absolute
             * anchor. `dy = lastY - my` is only the travel since the last
             * frame, so the list tracks the finger smoothly even when it
             * crosses the slab's top/bottom edge -- an absolute anchor
             * (startScroll - (my - startY)) froze the scroll in that dead
             * zone and then jumped when the finger came back into range.  */
            float dy = g_chat_swipe.lastY - my;
            g_chat_swipe.lastY = my;

            if (g_chat_swipe.scrolling) {
                float target = ImGui::GetScrollY() + dy;
                update_overscroll(g_chat_overscroll, target, max_scroll,
                                  true, true, io.DeltaTime);
                ImGui::SetScrollY(ImClamp(target, 0.0f, max_scroll));
                pulling = true;

                /* flick velocity: smoothed px/sec (scroll-down = +). */
                float dt = std::max(io.DeltaTime, 1e-4f);
                float inst = dy / dt;
                g_chat_swipe.velocity = g_chat_swipe.velocity * 0.85f
                                      + inst * 0.15f;
            }
        }
    } else {
        /* finger lifted (or moved over the sidebar): capture the momentum
         * ONCE so a quick flick keeps scrolling after the release.       */
        if (g_chat_swipe.down && !g_chat_swipe.flicking && !over_sidebar &&
            std::fabs(g_chat_swipe.velocity) >= g_flick_trigger)
            g_chat_swipe.flicking = true;
        g_chat_swipe.down        = false;
        g_chat_swipe.scrolling   = false;
    }

    /* momentum: keep rolling while velocity decays with friction; a hard
     * boundary (top/bottom) or a slow crawl ends the flick.              */
    if (g_chat_swipe.flicking && !touch && !over_sidebar && max_scroll > 0.0f) {
        float dt = std::max(io.DeltaTime, 1e-4f);
        float y  = ImGui::GetScrollY() + g_chat_swipe.velocity * dt;
        g_chat_swipe.velocity *= expf(-g_flick_friction * dt);
        ImGui::SetScrollY(ImClamp(y, 0.0f, max_scroll));
        if (std::fabs(g_chat_swipe.velocity) < g_flick_stop ||
            y <= 0.0f || y >= max_scroll)
            g_chat_swipe.flicking = false;
    }

    /* fallback: only when there is no touch at all (mouse / emulator
     * pointer with the left button held). Content follows the cursor:
     * the mouse delta is SUBTRACTED, so dragging down scrolls the list
     * down and reveals the top of the history.                       */
    if (!touch && !over_sidebar &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        float dy = io.MouseDelta.y;
        if (std::fabs(dy) > 0.0f) {
            float target = ImGui::GetScrollY() - dy;
            update_overscroll(g_chat_overscroll, target, max_scroll,
                              true, true, io.DeltaTime);
            ImGui::SetScrollY(ImClamp(target, 0.0f, max_scroll));
            pulling = true;
        }
    }

    /* not being dragged: let the rubber band spring back to zero */
    if (!pulling)
        update_overscroll(g_chat_overscroll, ImGui::GetScrollY(), max_scroll,
                          false, false, io.DeltaTime);

    /* ---- auto-follow-to-bottom: lock to newest message while tokens
     *      are streaming OR the user is near the bottom. Uses the frame
     *      snapshot `msgs_cp`, so it works even during context prefill. ---- */
    const bool at_bottom  = (max_scroll <= 0.0f) ||
                            (ImGui::GetScrollY() >= max_scroll - 4.0f);
    if (at_bottom && !g_chat_swipe.scrolling && !g_chat_swipe.flicking &&
        (g_lastMsgCount != msgs_cp.size() || cur().generating)) {
        ImGui::SetScrollHereY(1.0f);   /* keep newest message visible */
    }
    g_lastMsgCount = msgs_cp.size();

    ImGui::EndChild();
    ImGui::PopStyleVar();               /* WindowPadding */
    ImGui::PopStyleVar();               /* ItemSpacing   */
    ImGui::PopStyleColor();
}

/* ------------------------------------------------------------------ */
/* ZERO mode: big title + centered input bar.                           */
/* ------------------------------------------------------------------ */

} // namespace

/* ===========================================================================
 * Public C bridge
 * ========================================================================== */
extern "C" {

/* ------------------------------------------------------------------ */
/* Build a white RGBA texture from a packed 1bpp alpha mask.            */
/*                                                                      */
/* The mask bytes are LSB-first (bit i = pixel i = y*w+x). Each set bit */
/* becomes an opaque white texel (A8B8G8R8 = R,G,B,A in memory), which  */
/* AddImage tints at draw time. Stride is set explicitly so any width   */
/* renders correctly regardless of the allocation padding.              */
/* ------------------------------------------------------------------ */
static vita2d_texture *MakeIconTexture(const unsigned char *bits,
                                       unsigned int w, unsigned int h)
{
    const unsigned int stride = (w + 7) & ~7u;   /* pixels per row      */
    vita2d_texture *tex = vita2d_create_empty_texture_format(
        w, h, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
    if (!tex) return NULL;
    sceGxmTextureSetStride(&tex->gxm_tex, stride * 4);
    vita2d_texture_set_filters(tex, SCE_GXM_TEXTURE_FILTER_LINEAR,
                               SCE_GXM_TEXTURE_FILTER_LINEAR);
    unsigned int *px = (unsigned int *)vita2d_texture_get_datap(tex);
    for (unsigned int i = 0; i < w * h; i++) {
        unsigned int a = ((bits[i >> 3] >> (i & 7)) & 1) ? 255u : 0u;
        px[(i / w) * stride + (i % w)] = 0x00FFFFFFu | (a << 24);
    }
    return tex;
}

int imgui_ui_init(void)
{
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.MouseDrawCursor = false;      /* hide the virtual mouse arrow */
    apply_theme();

    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    ImVector<ImWchar> ranges;
    builder.BuildRanges(&ranges);

    /* Body font is loaded at NATIVELY higher px (28.0f ≈ 23 * 1.22) with
     * oversampling, so we do NOT need ImGui's texture-scaling hack later:
     * scaling a rasterized font texture produces pixelated "voxel" text. */
    ImFontConfig cfgBody;
    cfgBody.OversampleH = 3;
    cfgBody.OversampleV = 3;
    cfgBody.PixelSnapH  = true;

    ImFontConfig cfg;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;

    /* body chat font: 28px, high oversample -> crisp on handed screens */
    g_fontBody   = io.Fonts->AddFontFromMemoryTTF(
        (void *)g_font_inter_ttf, (int)g_font_data_ttf_size, 28.0f * UI_SCALE, &cfgBody, ranges.Data);
    g_fontSmall  = io.Fonts->AddFontFromMemoryTTF(
        (void *)g_font_inter_ttf, (int)g_font_data_ttf_size, 28.0f * UI_SCALE, &cfgBody, ranges.Data);
    /* title fonts switch to the bold Impact face for the WoozyLLM wordmark */
    g_fontHeader = io.Fonts->AddFontFromMemoryTTF(
        (void *)g_font_impact_ttf, (int)g_font_impact_ttf_size, 40.0f * UI_SCALE, &cfg, ranges.Data);
    g_fontHero   = io.Fonts->AddFontFromMemoryTTF(
        (void *)g_font_impact_ttf, (int)g_font_impact_ttf_size, 60.0f * UI_SCALE, &cfg, ranges.Data);
    io.Fonts->Build();

    ImGui_ImplVita2D_Init();
    ImGui_ImplVita2D_TouchUsage(true);
    ImGui_ImplVita2D_UseIndirectFrontTouch(false);
    ImGui_ImplVita2D_UseRearTouch(true);
    ImGui_ImplVita2D_GamepadUsage(true);

    /* the backend forces MouseDrawCursor=true on init; hide it again. */
    ImGui::GetIO().MouseDrawCursor = false;

    /* touch panel -> screen-pixel scale factors (for direct swipe) */
    SceTouchPanelInfo pi;
    if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &pi) == 0) {
        float aw = (float)(pi.maxAaX - pi.minAaX);
        float ah = (float)(pi.maxAaY - pi.minAaY);
        if (aw > 0.0f) g_touchSX = VIEWPORT_W / aw;
        if (ah > 0.0f) g_touchSY = VIEWPORT_H / ah;
    }

    /* rebuild the embedded 1bpp glyph masks into GPU RGBA textures        */
    g_texMenu     = MakeIconTexture(g_icon_menu_bits, g_icon_menu_w, g_icon_menu_h);
    g_texBack     = MakeIconTexture(g_icon_back_bits, g_icon_back_w, g_icon_back_h);
    g_texSend     = MakeIconTexture(g_icon_send_bits, g_icon_send_w, g_icon_send_h);
    g_texSettings = MakeIconTexture(g_icon_settings_bits, g_icon_settings_w, g_icon_settings_h);
    g_texLogo     = MakeIconTexture(g_icon_logo_title_bits, g_icon_logo_title_w,
                                    g_icon_logo_title_h);

    start_new_session();
    return 0;
}

void imgui_ui_begin(void)
{
    /* the backend can flip this on; keep the cursor hidden every frame. */
    ImGui::GetIO().MouseDrawCursor = false;
    ImGui_ImplVita2D_NewFrame();
}

void imgui_ui_draw(void)
{
    ensure_session();

    ImGuiIO &io = ImGui::GetIO();

    /* glow now comes solely from the input bar halo (see draw_input_bar). */
    bool active = !cur().messages.empty() || !g_app.draft.empty() || cur().generating;

    /* ---------------------------------------------------------------
     * Zero <-> Chat page transition, driven by DeltaTime.
     *
     * FORWARD (center -> bottom): starts once the current session has
     *   real history (a loaded chat or a message submitted this session;
     *   g_first_sent). Typing alone never animates.
     * REVERSE (bottom -> center): happens when leaving a chat (New Chat)
     *   and no draft / messages remain.
     * An ease-out curve is applied to the movement.
     * --------------------------------------------------------------- */
    static float s_prog = 0.0f;
    const float dur = 0.35f;                       /* seconds             */
    float dt = std::max(io.DeltaTime, 1.0f / 60.0f);
    const bool has_history = !cur().messages.empty() || cur().generating;
    if ((g_first_sent || has_history) && active && s_prog < 1.0f)
        s_prog = std::min(1.0f, s_prog + dt / dur);
    else if (!active && s_prog > 0.0f)
        s_prog = std::max(0.0f, s_prog - dt / dur);
    float ease = 1.0f - std::pow(1.0f - s_prog, 3.0f);   /* ease-out cubic */

    /* ---- Input bar geometry keyframes ---- */
    /* The chat offset follows the ANIMATED drawer width (not the open/close
     * flag), so the bubbles slide back smoothly on close instead of snapping
     * to x=0 the instant sidebar_open flips to false.                    */
    const float sx      = current_sidebar_width;
    const float avail_w = VIEWPORT_W - sx;             /* chat zone width   */
    const float iw0 = avail_w * 0.55f;                 /* zero: centered 55% */
    const float iw1 = avail_w * 0.65f;                 /* chat: centered 65% */
    const float iy0 = 235.0f * UI_SCALE;                        /* zero: near center*/
    const float iy1 = VIEWPORT_H - 64.0f * UI_SCALE - 32.0f; /* chat: bottom dock*/
    const float ih  = 64.0f * UI_SCALE;
    float iw = iw0 + (iw1 - iw0) * ease;
    float iy = iy0 + (iy1 - iy0) * ease;
    float ihh = ih;
    float ix = sx + (avail_w - iw) * 0.5f;             /* centered in zone   */

    /* ---- Title keyframes (moves up + fades slightly, font shrinks) ----
     * ZERO mode: large Impact "WoozyLLM" centered in the chat zone.
     * CHAT mode: the title centers in the gray header bar. Both size and
     * position interpolate with `ease` so the transition stays smooth. */
    ImVec2 tc(sx + avail_w * 0.5f
              + (VIEWPORT_W * 0.5f - (sx + avail_w * 0.5f)) * ease,
              170.0f + (HEADER_H * 0.5f - 170.0f) * ease);
    float  ta = 1.0f - 0.35f * ease;                 /* alpha fade          */
    ImFont *titleFont = (s_prog < 0.5f) ? g_fontHero : g_fontHeader;
    ImVec4 titleCol = colText(); titleCol.w = ta;

    /* the root window is fully transparent so the background glow shows
     * through it; all accents are drawn by the children below. */
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(VIEWPORT_W, VIEWPORT_H));
    ImGui::Begin("##root", NULL,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ease > 0.45f) {
        /* TOP CHAT HEADER BAR: solid background identical to the gray
         * input field (#121212 from colField), so the title strip matches
         * the "Message WoozyLLM..." box instead of showing through black. */
        ImDrawList *rdl = ImGui::GetWindowDrawList();
        rdl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(VIEWPORT_W, HEADER_H),
                           to32(colField()));
        /* 1px seam under the bar so bubbles don't blend into the strip */
        rdl->AddLine(ImVec2(0.0f, HEADER_H), ImVec2(VIEWPORT_W, HEADER_H),
                     IM_COL32(45, 45, 45, 255));
    }

    /* Title drawn AFTER the header fill so it is never covered by it. */
    drawCentered(titleFont, tc, "WoozyLLM", to32(titleCol), false);

    /* Chat bubbles appear once we are far enough into the transition. */
    if (ease > 0.45f)
        drawChat(HEADER_H, sx);

    /* Input bar always rendered at the lerped position, strictly below
     * the chat slab (its own height is reserved inside drawChat). */
    draw_input_bar(ix, iy, iw, ihh);

    /* Model caption only near the zero state, fading out. */
    if (!g_app.status.empty() && ease < 0.7f) {
        ImVec4 sc = colDim(); sc.w = 1.0f - ease / 0.7f;
        ImVec2 c(sx + avail_w * 0.5f, iy + ihh + 48.0f);
        drawCentered(g_fontBody, c, g_app.status.c_str(), to32(sc), false);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    /* Sidebar drawn LAST -> placed on top of the chat draw stack, with a
     * solid opaque background so messages never shine through. */
    DrawSidebar(dt);

    /* Fullscreen Settings overlay (pure black, zero margin, Columns layout) */
    if (g_app.show_settings) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(VIEWPORT_W, VIEWPORT_H));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse;

        if (ImGui::Begin("SettingsView", nullptr, flags)) {
            const float SETTINGS_HEADER_H = 52.0f;
            float win_w = ImGui::GetWindowWidth();

            /* --- TOP HEADER PANEL (#121212 edge-to-edge) --- */
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.071f, 0.071f, 0.071f, 1.0f));

            if (ImGui::BeginChild("SettingsHeader", ImVec2(win_w, SETTINGS_HEADER_H), false,
                                  ImGuiWindowFlags_NoScrollbar)) {
                /* Back button (40x40, transparent, Y-centered) */
                ImGui::SetCursorPos(ImVec2(12.0f, (SETTINGS_HEADER_H - 40.0f) * 0.5f));

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

                if (ImGui::ImageButton((ImTextureID)g_texBack, ImVec2(40.0f, 40.0f))) {
                    g_app.show_settings = false;
                }

                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(3);

                /* Centered white title */
                ImGui::SetWindowFontScale(1.25f);
                const char* title_str = "Settings";
                ImVec2 t_size = ImGui::CalcTextSize(title_str);
                ImGui::SetCursorPos(ImVec2((win_w - t_size.x) * 0.5f,
                                           (SETTINGS_HEADER_H - t_size.y) * 0.5f));
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", title_str);
                ImGui::SetWindowFontScale(1.0f);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(1);
            ImGui::PopStyleVar(3);

            /* Divider line under header */
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
            ImGui::Separator();
            ImGui::PopStyleColor(1);

            /* --- SCROLLABLE CONTENT REGION --- */
            float avail_h = ImGui::GetContentRegionAvail().y;

            static float temp = 0.7f;
            static int max_tokens = 2048;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

            ImGui::BeginChild("SettingsScrollContent", ImVec2(win_w, avail_h), false,
                              ImGuiWindowFlags_NoScrollbar);
            {
                /* Touch swipe + overscroll + flick (same as chat) */
                ImGuiIO &sio = ImGui::GetIO();
                SceTouchData stdt;
                memset(&stdt, 0, sizeof(stdt));
                bool stouch = (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &stdt, 1) >= 0 &&
                               stdt.reportNum > 0);
                const float smax_scroll = ImGui::GetScrollMaxY();
                bool spulling = false;

                if (stouch) {
                    float smy = sio.MousePos.y;
                    g_settings_swipe.lastX = sio.MousePos.x;
                    if (!g_settings_swipe.down) {
                        g_settings_swipe.down      = true;
                        g_settings_swipe.scrolling = false;
                        g_settings_swipe.flicking  = false;
                        g_settings_swipe.startY    = smy;
                        g_settings_swipe.lastY     = smy;
                        g_settings_swipe.velocity  = 0.0f;
                    } else {
                        if (!g_settings_swipe.scrolling &&
                            std::fabs(smy - g_settings_swipe.startY) > 8.0f)
                            g_settings_swipe.scrolling = true;

                        float sdy = g_settings_swipe.lastY - smy;
                        g_settings_swipe.lastY = smy;

                        if (g_settings_swipe.scrolling) {
                            float starget = ImGui::GetScrollY() + sdy;
                            update_overscroll(g_settings_overscroll, starget,
                                              smax_scroll, true, true, sio.DeltaTime);
                            ImGui::SetScrollY(ImClamp(starget, 0.0f, smax_scroll));
                            spulling = true;

                            float sdt = std::max(sio.DeltaTime, 1e-4f);
                            float sinst = sdy / sdt;
                            g_settings_swipe.velocity =
                                g_settings_swipe.velocity * 0.85f + sinst * 0.15f;
                        }
                    }
                } else {
                    if (g_settings_swipe.down && !g_settings_swipe.flicking &&
                        std::fabs(g_settings_swipe.velocity) >= g_flick_trigger)
                        g_settings_swipe.flicking = true;
                    g_settings_swipe.down      = false;
                    g_settings_swipe.scrolling = false;
                }

                if (g_settings_swipe.flicking && !stouch && smax_scroll > 0.0f) {
                    float sdt = std::max(sio.DeltaTime, 1e-4f);
                    float sy = ImGui::GetScrollY() + g_settings_swipe.velocity * sdt;
                    g_settings_swipe.velocity *= expf(-g_flick_friction * sdt);
                    ImGui::SetScrollY(ImClamp(sy, 0.0f, smax_scroll));
                    if (std::fabs(g_settings_swipe.velocity) < g_flick_stop ||
                        sy <= 0.0f || sy >= smax_scroll)
                        g_settings_swipe.flicking = false;
                }

                if (!stouch &&
                    ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    float sdy = sio.MouseDelta.y;
                    if (std::fabs(sdy) > 0.0f) {
                        float starget = ImGui::GetScrollY() - sdy;
                        update_overscroll(g_settings_overscroll, starget,
                                          smax_scroll, true, true, sio.DeltaTime);
                        ImGui::SetScrollY(ImClamp(starget, 0.0f, smax_scroll));
                        spulling = true;
                    }
                }

                if (!spulling)
                    update_overscroll(g_settings_overscroll, ImGui::GetScrollY(),
                                      smax_scroll, false, false, sio.DeltaTime);

                if (std::fabs(g_settings_overscroll.offset) > 0.1f)
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                                         g_settings_overscroll.offset);

                /* 2-column layout: 65% labels, 35% controls */
                ImGui::Columns(2, "settings_layout", false);
                ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.65f);

                /* --- SECTION 1: API & NETWORK --- */
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.0f, 1.0f), "API & NETWORK");
                ImGui::NextColumn();
                ImGui::Spacing();
                ImGui::NextColumn();

                /* Row: API Key */
                ImGui::TextUnformatted("API Key");
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                                   "Local model runner or cloud service");
                ImGui::NextColumn();
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (ImGui::Button("Configure##apikey", ImVec2(-1.0f, 32.0f))) {
                    /* Configure Key action */
                }
                ImGui::PopStyleVar(1);
                ImGui::NextColumn();

                ImGui::Dummy(ImVec2(0, 10.0f));
                ImGui::NextColumn();
                ImGui::NextColumn();

                /* Row: Base URL */
                ImGui::TextUnformatted("Base URL");
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                                   "Provider endpoint URL");
                ImGui::NextColumn();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "api.openai.com");
                ImGui::NextColumn();

                ImGui::Dummy(ImVec2(0, 10.0f));
                ImGui::NextColumn();
                ImGui::NextColumn();

                /* Row: Refresh Models */
                ImGui::TextUnformatted("Refresh Models");
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                                   "Fetch available models list");
                ImGui::NextColumn();
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (ImGui::Button("Fetch##refresh", ImVec2(-1.0f, 32.0f))) {
                    /* Fetch models action */
                }
                ImGui::PopStyleVar(1);
                ImGui::NextColumn();

                /* --- SECTION 2: MODEL & GENERATION --- */
                ImGui::Dummy(ImVec2(0, 16.0f));
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.0f, 1.0f), "MODEL & GENERATION");
                ImGui::Spacing();
                ImGui::NextColumn();
                ImGui::Dummy(ImVec2(0, 16.0f));
                ImGui::NextColumn();

                /* Row: Active Model */
                ImGui::TextUnformatted("Active Model");
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                                   "Currently loaded LLM model");
                ImGui::NextColumn();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.0f, 1.0f), "SmolLM-135M");
                ImGui::NextColumn();

                ImGui::Dummy(ImVec2(0, 10.0f));
                ImGui::NextColumn();
                ImGui::NextColumn();

                /* Row: Temperature */
                ImGui::TextUnformatted("Temperature");
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                                   "Higher = more creative");
                ImGui::NextColumn();
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::SliderFloat("##temp_slider", &temp, 0.1f, 1.0f, "%.1f");
                ImGui::PopStyleVar(1);
                ImGui::NextColumn();

                ImGui::Dummy(ImVec2(0, 10.0f));
                ImGui::NextColumn();
                ImGui::NextColumn();

                /* Row: Max Tokens */
                ImGui::TextUnformatted("Max Tokens");
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                                   "Response length limit");
                ImGui::NextColumn();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
                char tok_buf[16];
                snprintf(tok_buf, sizeof(tok_buf), "%d", max_tokens);
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), tok_buf);
                ImGui::NextColumn();

                ImGui::Columns(1);
            }
            ImGui::EndChild();

            ImGui::PopStyleColor(1);
            ImGui::PopStyleVar(1);

            ImGui::End();
        }

        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar(3);
    }
}

void imgui_ui_end(void)
{
    ImGui::Render();
    ImGui_ImplVita2D_RenderDrawData(ImGui::GetDrawData());
}

void imgui_ui_fini(void)
{
    if (g_texMenu)     vita2d_free_texture(g_texMenu);
    if (g_texBack)     vita2d_free_texture(g_texBack);
    if (g_texSend)     vita2d_free_texture(g_texSend);
    if (g_texSettings) vita2d_free_texture(g_texSettings);
    if (g_texLogo)     vita2d_free_texture(g_texLogo);
    g_texMenu = g_texBack = g_texSend = g_texSettings = g_texLogo = NULL;
    ImGui_ImplVita2D_Shutdown();
    ImGui::DestroyContext();
}

/* ------------------------------------------------------------------ */
/* data injection                                                      */
/* ------------------------------------------------------------------ */
void imgui_ui_set_status(const char *s) { ensure_session(); g_app.status = s ? s : ""; }
void imgui_ui_set_draft(const char *t)  { ensure_session(); g_app.draft = t ? t : ""; }

const char *imgui_ui_take_draft(void)
{
    ensure_session();
    static char buf[1024];
    snprintf(buf, sizeof(buf), "%s", g_app.draft.c_str());
    g_app.draft.clear();
    return buf;
}

/* Atomically append BOTH message objects for one user submission:
 *
 *   1. the User bubble with the typed text,
 *   2. an empty Assistant placeholder that streaming will fill.
 *
 * `generating` is set right here (and cleared by end_assistant), so there is
 * never a window where a streamed token can land in the wrong bubble and the
 * UI shows the "AI is typing" indicator while we wait for the first token.
 *
 * Returns 1 on success, 0 if `text` is empty or a response is in progress.
 */
int imgui_ui_send_message(const char *t)
{
    ensure_session();
    if (!t || !*t || cur().generating)
        return 0;

    /* A) User prompt (right-aligned orange bubble) */
    g_first_sent = true;
    cur().messages.push_back(ChatMessage(t, true));
    if (cur().title == "New chat") {
        std::string first = std::string(t);
        size_t nl = first.find('\n');
        if (nl != std::string::npos) first = first.substr(0, nl);
        if (first.size() > 40) first = first.substr(0, 40);
        cur().title = first;
    }

    /* B) EMPTY Assistant placeholder (separate left-aligned gray bubble).
     * streaming appends strictly to THIS entry, never to the User one. */
    cur().messages.push_back(ChatMessage("", false));

    cur().generating = true;
    g_lastMsgCount = 0;                 /* force the follow-to-bottom    */
    autosave_current_session();
    return 1;
}

/* Stream callback: the worker hands us the FULL accumulated response
 * snapshot every frame (not per-token), which is equivalent to appending
 * each token to the placeholder. We must write into the LAST Assistant
 * message and never touch the User message. */
void imgui_ui_stream_assistant(const char *t)
{
    if (g_app.sessions.empty())
        return;

    std::vector<ChatMessage> &ms = cur().messages;

    /* find the trailing Assistant placeholder (streaming target) */
    size_t i = ms.size();
    while (i > 0 && ms[i - 1].is_user)
        i--;

    if (i == 0) {
        /* no placeholder: caller submitted without send_message().
         * create one so the response still lands in its own bubble. */
        ms.push_back(ChatMessage("", false));
        cur().generating = true;
    } else {
        ms[i - 1].text = t ? t : "";
    }
}

void imgui_ui_end_assistant(void)
{
    if (!g_app.sessions.empty())
        cur().generating = false;
    autosave_current_session();
}

void imgui_ui_new_chat(void) { start_new_session(); }

int  imgui_ui_want_input(void)      { return (int)g_app.want_input; }
void imgui_ui_clear_want_input(void){ g_app.want_input = false; }
int  imgui_ui_want_send(void)       { return (int)g_app.want_send; }
void imgui_ui_clear_want_send(void) { g_app.want_send = false; }
int  imgui_ui_want_stop(void)       { return (int)g_app.want_stop; }
void imgui_ui_clear_want_stop(void) { g_app.want_stop = false; }

} // extern "C"
