#include "ui/talk.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "game/game.h"
#include "mod/context.h"
#include "sdk/natives.gen.h"

namespace fd::ui {

// The layout is the one of the mock-up (1600 x 900 px), so every number below is a pixel of it; X()/Y() turn it into
// the game's 0..1 screen coordinates. Text: the game's fonts at a scale per mock-up pixel (talk.px in Companion.dat)
// and a width per character (talk.cw) - the game cannot measure a string, so right-aligned text is placed by that.
static const char* kDict = "frontier_dog_ui";
static float X(float px) { return px / 1600.0f; }
static float Y(float px) { return px / 900.0f; }

struct Rgba {
    int r, g, b, a;
};
static const Rgba kInk{43, 38, 32, 255}, kLabel{91, 71, 48, 255}, kNumber{138, 42, 30, 255}, kSeal{140, 32, 24, 225},
    kGold{216, 181, 106, 255}, kLight{239, 231, 214, 255}, kDim{207, 198, 179, 255};

static float pxScale() { return float(ctx().tunef("talk.px", 0.018)); }
// width of an average character per unit of scale, per font face (measured 24.11 in game: a 40-character line and the
// right-aligned answers against their key boxes); talk.cw scales them all
static float charW(const char* font) {
    std::string f = font;
    float w = f == "$body1" ? 0.0183f : f == "$title" ? 0.0152f : 0.0176f;
    return w * float(ctx().tunef("talk.cw", 1.0));
}

static int u8len(const std::string& s) {
    int n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

// width of a string in screen units (an estimate: characters × the font's width × scale)
static float textW(const std::string& s, float px, const char* font = "$body") {
    return u8len(s) * charW(font) * px * pxScale();
}
static float textPx(const std::string& s, float px, const char* font = "$body") { return textW(s, px, font) * 1600.0f; }

enum Align { Left, Center, Right };

static std::string clean(const std::string& s) {
    std::string out;
    for (char ch : s)
        if (ch != '<' && ch != '>' && ch != '\'') out += ch;
    return out;
}

// text at mock-up pixel (x, y) = its top-left (or top-centre / top-right), px = the mock-up font size
static void say(const std::string& t, float x, float y, float px, Rgba c, Align al = Left, const char* font = "$body") {
    if (t.empty()) return;
    float sx = X(x);
    if (al == Right) sx -= textW(t, px, font);
    float sc = px * pxScale();
    UIDEBUG::_BG_SET_TEXT_COLOR(c.r, c.g, c.b, c.a);
    UIDEBUG::_BG_SET_TEXT_SCALE(sc, sc);
    fd::invoke<void>(0xBE5261939FBECB8CULL, al == Center);  // SET_TEXT_CENTRE
    // the colour also in the tag: the key boxes' dark digits came out white with the tag's #ffffff (24.11)
    char hex[8];
    std::snprintf(hex, sizeof hex, "#%02x%02x%02x", c.r, c.g, c.b);
    std::string tpl = std::string("<FONT FACE='") + font + "' COLOR='" + hex + "'>" + clean(t) + "</FONT>";
    UIDEBUG::_BG_DISPLAY_TEXT(MISC::VAR_STRING(42, "COLOR_STRING", 0, game::literal(tpl)), sx, Y(y));
}

static void box(float x, float y, float w, float h, Rgba c) {
    GRAPHICS::DRAW_RECT(X(x + w / 2), Y(y + h / 2), X(w), Y(h), c.r, c.g, c.b, c.a, true, false);
}

static bool dictReady(const char* dict) {
    if (TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(dict)) return true;
    TXD::REQUEST_STREAMED_TEXTURE_DICT(dict, false);
    return false;
}

static void pic(const char* dict, const char* name, float x, float y, float w, float h, Rgba c, float heading = 0) {
    GRAPHICS::DRAW_SPRITE(dict, name, X(x + w / 2), Y(y + h / 2), X(w), Y(h), heading, c.r, c.g, c.b, c.a, true);
}

bool talkTexturesReady() {
    if (!TXD::DOES_STREAMED_TEXTURE_DICT_EXIST(kDict)) return false;
    return dictReady(kDict);
}

static std::vector<std::string> wrap(const std::string& s, float px, float maxW, const char* font = "$body") {
    std::vector<std::string> lines;
    std::string line, word;
    auto flush = [&]() {
        if (word.empty()) return;
        std::string next = line.empty() ? word : line + " " + word;
        if (!line.empty() && textW(next, px, font) > X(maxW)) {
            lines.push_back(line);
            line = word;
        } else {
            line = next;
        }
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ') flush();
        else word += ch;
    }
    flush();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

static const char* roman(int n) {
    static const char* r[] = {"", "I", "II", "III", "IV", "V"};
    return r[std::clamp(n, 0, 5)];
}

// the game's satchel drawing of the breed (satchel_textures animal_dog_*), by the dog's model
static const char* breedPicture(const std::string& model) {
    static const std::pair<const char*, const char*> kMap[] = {
        {"australiansheperd", "animal_dog_australianshepherd"}, {"bluetickcoonhound", "animal_dog_bluetickcoonhound"},
        {"catahoulacur", "animal_dog_catahoularcur"},           {"chesbayretriever", "animal_dog_chesbayretriever"},
        {"americanfoxhound", "animal_dog_americanfoxhound"},     {"collie", "animal_dog_collie"},
        {"husky", "animal_dog_husky"},                           {"poodle", "animal_dog_poodle"},
        {"doglab", "animal_dog_lab"},                            {"doghound", "animal_dog_hound"},
        {"hobo", "animal_dog_street"},                           {"street", "animal_dog_street"},
        {"rufus", "animal_dog_street"}};
    std::string m;
    for (char ch : model) m += char(std::tolower((unsigned char)ch));
    for (const auto& [part, tex] : kMap)
        if (m.find(part) != std::string::npos) return tex;
    return "animal_dog_street";
}

static std::string paperNumber(const DogRecord& d) {
    unsigned h = 2166136261u;
    for (char ch : d.uid) h = (h ^ (unsigned char)ch) * 16777619u;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04u", h % 10000u);
    return buf;
}

static std::string gameDate() {
    auto& c = ctx();
    int day = CLOCK::GET_CLOCK_DAY_OF_MONTH(), month = CLOCK::GET_CLOCK_MONTH(), year = CLOCK::GET_CLOCK_YEAR();
    return c.f("talk.date", {{"day", std::to_string(day)}, {"month", c.t("talk.month." + std::to_string(month + 1))},
                             {"year", std::to_string(year)}});
}

// ---- the paper: content box 168..686 x 82..816 (the sheet at 110,20 634x860); text in the game's book face ----
static const char* kPaper = "$body1";

static void drawSheet(const TalkSheet& s) {
    auto& c = ctx();
    const DogRecord& d = *s.dog;
    const float L = 168, R = 686, T = 82, B = 816;
    if (talkTexturesReady()) {
        pic(kDict, "fd_sheet", 110 + 7, 20 + 14, 634, 860, {0, 0, 0, 150});  // its own ragged shadow
        pic(kDict, "fd_sheet", 110, 20, 634, 860, {255, 255, 255, 255});
    } else {
        box(110, 20, 634, 860, {214, 196, 160, 250});
    }
    // header and title
    say(c.t("talk.society"), L, T, 12, kInk, Left, kPaper);
    say(d.papers ? "№ " + paperNumber(d) : "№ —", R, T, 12, kNumber, Right, kPaper);
    say(c.t("talk.title1"), (L + R) / 2, T + 22, 27, kInk, Center, "$title");
    say(c.t("talk.title2"), (L + R) / 2, T + 52, 27, kInk, Center, "$title");
    // divider with three dots
    float dy = T + 98;
    box(L, dy, (R - L) / 2 - 26, 1.2f, kLabel);
    box((L + R) / 2 + 26, dy, (R - L) / 2 - 26, 1.2f, kLabel);
    for (int k = -1; k <= 1; ++k) {
        float sz = k == 0 ? 11.0f : 7.0f;
        pic("generic_textures", "filter_dot", (L + R) / 2 + k * 15 - sz / 2, dy - sz / 2, sz, sz, kLabel, 45);
    }
    // identity: the game's drawing of the breed, then the lines
    float iy = T + 114;
    box(L, iy, 128, 150, {243, 234, 212, 255});
    if (dictReady("satchel_textures"))
        pic("satchel_textures", breedPicture(d.model), L + 5, iy + 5, 118, 140, {235, 222, 196, 255});
    float tx = L + 146;
    say(s.noName || d.name.empty() ? c.t("talk.no_name") : d.name, tx, iy - 4, 32, kInk, Left, "$title");
    float ly = iy + 38;
    const float row = 25, fs = 15;
    auto field = [&](const std::string& label, const std::string& value) {
        say(label, tx, ly, fs, kLabel, Left, kPaper);
        say(value, tx + textPx(label + " ", fs, kPaper), ly, fs, kInk, Left, kPaper);
        ly += row;
    };
    field(c.t("talk.breed"), c.breedText(d) + " · " + c.t(d.female ? "talk.female" : "talk.male"));
    field(c.t("talk.grade"), c.gradeText(d) + (d.papers ? c.t("talk.with_papers", d.female) : c.t("talk.no_papers", d.female)));
    field(c.t("talk.owner"), s.owner);
    // bond: level, a bar, points
    double p2 = c.tunef("bond.points.2", 100), p3 = c.tunef("bond.points.3", 400);
    double need = d.bondLevel <= 1 ? p2 : p3;
    double frac = d.bondLevel >= 3 ? 1.0 : std::clamp(d.bondPoints / std::max(1.0, need), 0.0, 1.0);
    std::string bl = c.t("talk.bond") + " " + roman(d.bondLevel);
    say(bl, tx, ly, fs, kLabel, Left, kPaper);
    char pts[32];
    std::snprintf(pts, sizeof pts, "%d / %d", int(d.bondPoints), int(d.bondLevel >= 3 ? p3 : need));
    float bx = tx + textPx(bl, fs, kPaper) + 12, bw = R - bx - textPx(pts, 13, kPaper) - 12;
    box(bx, ly + 8, bw, 8, kLabel);
    box(bx + 1, ly + 9, bw - 2, 6, {222, 207, 172, 255});
    box(bx + 1, ly + 9, (bw - 2) * float(frac), 6, kLabel);
    say(pts, R, ly + 1, 13, kInk, Right, kPaper);
    ly += row;
    int hp = s.healthPct >= 0 ? s.healthPct : int(d.healthFrac * 100);
    int fed = std::clamp(int(100 - d.hunger), 0, 100);
    std::string coat = c.t(d.dirt < 30 ? "talk.coat.clean" : (d.dirt < 70 ? "talk.coat.dusty" : "talk.coat.dirty"));
    say(c.f("talk.condition", {{"hp", std::to_string(hp)}, {"fed", std::to_string(fed)}, {"coat", coat}}), tx, ly, 14, kInk,
        Left, kPaper);
    // two columns: stats, then traits | skills
    float cy = iy + 166;
    box(L, cy, R - L, 1.2f, kLabel);
    cy += 10;
    const float colL = L, colLR = L + 247, colR = L + 271;
    const float srow = 23, cs = 15;
    say(c.t("talk.stats"), colL, cy, 12, kLabel, Left, kPaper);
    say(c.t("talk.skills"), colR, cy, 12, kLabel, Left, kPaper);
    float sy = cy + 21;
    for (int i = 0; i < StatCount; ++i) {
        say(c.t("stat." + std::to_string(i + 1)), colL, sy + i * srow, cs, kInk, Left, kPaper);
        for (int k = 0; k < 5; ++k) {
            bool on = k < d.stats[i];
            pic("generic_textures", "filter_dot", colLR - 5 * 13 + k * 13, sy + i * srow + 5, 10, 10,
                on ? kInk : Rgba{kInk.r, kInk.g, kInk.b, 60});
        }
    }
    float ky = sy;
    for (const auto& sk : c.breeds.skills()) {
        say(c.t("skill." + sk), colR, ky, cs, kInk, Left, kPaper);
        say(roman(d.skill(sk)), R, ky, cs, kInk, Right, kPaper);
        ky += srow;
    }
    // traits in the left column under the stats: a name and its description wrapped to the column (two traits);
    // more traits: one line each, the description cut short; never into the foot
    float ty = sy + StatCount * srow + 10;
    say(c.t("talk.traits"), colL, ty, 12, kLabel, Left, kPaper);
    ty += 21;
    const float foot = B - 82, colW = colLR - colL + 6;
    if (d.traits.empty()) say(c.t("card.no_traits"), colL, ty, cs, kInk, Left, kPaper);
    bool roomy = d.traits.size() <= 2;
    size_t shown = 0;
    for (const auto& id : d.traits) {
        if (ty + srow > foot) break;
        std::string name = c.t("trait." + id + ".name", d.female);
        std::string desc = c.t("trait." + id + ".desc", d.female);
        say(name, colL, ty, cs, kInk, Left, "$title");
        ++shown;
        if (roomy) {
            ty += srow - 2;
            auto lines = wrap(desc, 13, colW, kPaper);
            for (size_t k = 0; k < lines.size() && k < 3 && ty + 18 <= foot; ++k) {
                say(lines[k], colL + 10, ty, 13, kInk, Left, kPaper);
                ty += 18;
            }
            ty += 4;
        } else {
            float nameW = textPx(name, cs, "$title");
            auto lines = wrap("— " + desc, 13, colW - nameW - 6, kPaper);
            if (!lines.empty()) say(lines[0] + (lines.size() > 1 ? "…" : ""), colL + nameW + 6, ty + 1, 13, kInk, Left, kPaper);
            ty += srow;
        }
    }
    if (shown < d.traits.size())
        say(c.f("talk.more_traits", {{"n", std::to_string(d.traits.size() - shown)}}), colL, ty, 13, kLabel, Left, kPaper);
    // the foot: the trader's price, place and date, his hand; the society's seal when the dog has papers
    float fy = B - 72;
    say(c.f("talk.value", {{"price", std::to_string(s.value)}}), L, fy, 15, kInk, Left, kPaper);
    say(s.town.empty() ? gameDate() : s.town + ", " + gameDate(), L, fy + 22, 15, kInk, Left, kPaper);
    box(L, fy + 70, 210, 1.2f, kLabel);
    // his hand: the game's handwriting captured from the screen (it has no Cyrillic, and Telegraph would translate
    // the Latin name if it were drawn as text)
    // (512x128, the hand 464x116 inside it at 24,6: a power-of-two texture)
    if (talkTexturesReady()) pic(kDict, "fd_sign", L + 2 - 9, fy + 38 - 2, 194, 48.5f, {kInk.r, kInk.g, kInk.b, 235}, -2);
    else say(c.t("talk.sign"), L + 6, fy + 44, 20, kInk, Left, kPaper);
    if (d.papers && talkTexturesReady()) {
        float sz = 140, sx = R - sz + 8, sy2 = B - sz + 24;
        pic(kDict, "fd_seal", sx, sy2, sz, sz, kSeal, -9);
        say(c.t("talk.seal1"), sx + sz / 2, sy2 + 47, 9, kSeal, Center, "$title");
        say(c.t("talk.seal2"), sx + sz / 2, sy2 + 58, 18, kSeal, Center, "$title");
        say(c.t("talk.seal3"), sx + sz / 2, sy2 + 82, 12, kSeal, Center, "$title");
    }
}

// ---- the talk: the trader's line and the answers on the right (right edge 1520) ----
static const int kRowsMax = 7;
int talkVisibleRows() { return kRowsMax; }

static void drawTalk(const TalkView& v) {
    auto& c = ctx();
    // on a pad the game's own prompts (Select / Back) sit in the bottom right corner: the answers move up for them
    const float RE = 1520, rowH = 44, bottom = v.keyboard ? 844.0f : 784.0f;
    // money, top right: dollars big, cents small and raised
    int cents = game::cash();
    std::string dollars = "$" + std::to_string(cents / 100);
    char cc[8];
    std::snprintf(cc, sizeof cc, "%02d", cents % 100);
    say(cc, RE, 36, 15, kLight, Right, "$title");
    say(dollars, RE - textPx(cc, 15, "$title") - 3, 30, 28, kLight, Right, "$title");
    // the answers, from the bottom up; a window of kRowsMax rows around the selected one
    int n = int(v.options.size());
    int first = 0;
    if (n > kRowsMax) first = std::clamp(v.selected - kRowsMax / 2, 0, n - kRowsMax);
    int shown = std::min(n, kRowsMax);
    float top = bottom - shown * rowH;
    for (int k = 0; k < shown; ++k) {
        int i = first + k;
        float y = top + k * rowH;
        bool sel = i == v.selected;
        bool last = i == n - 1;
        if (sel) {
            // a red band fading to the left, a solid edge on the right (the game's own selection is a band too)
            const int slices = 12;
            const float bw = 470;
            for (int s = 0; s < slices; ++s) {
                float x0 = RE - bw + s * (bw / slices);
                int a = int(150.0f * (s + 1) / slices);
                box(x0, y + 2, bw / slices + 0.5f, rowH - 4, {179, 32, 26, a});
            }
            box(RE - 4, y + 2, 4, rowH - 4, {179, 32, 26, 255});
        }
        float textRight = RE - 14;
        if (v.keyboard) {
            std::string key = last ? "←" : std::to_string(i + 1);
            if (last || i < 9) {
                box(RE - 14 - 30, y + 9, 30, 26, kLight);
                say(key, RE - 14 - 15, y + 10, 17, {17, 17, 17, 255}, Center, "$body");
            }
            textRight = RE - 14 - 30 - 12;
        }
        say(v.options[i], textRight, y + 9, 21, last && !sel ? kDim : kLight, Right);
    }
    // more answers above / below the window: the game's selection arrow turned up / down
    if (first > 0) pic("generic_textures", "selection_arrow_right", RE - 52, top - 22, 16, 16, kDim, -90);
    if (first + shown < n) pic("generic_textures", "selection_arrow_right", RE - 52, bottom + 4, 16, 16, kDim, 90);
    // the trader's line above the answers: his name in gold, then his words, right-aligned
    auto lines = wrap(v.line, 24, 640);
    float lineH = 35;
    float ly = std::max(110.0f, top - 34 - lineH * float(lines.size()) - 30);
    say(c.t("stable.trader") + ":", RE, ly, 20, kGold, Right, "$title");
    ly += 32;
    for (const auto& l : lines) {
        say(l, RE, ly, 24, kLight, Right);
        ly += lineH;
    }
}

void talkDraw(const TalkView& v) {
    GRAPHICS::SET_SCRIPT_GFX_DRAW_ORDER(4);
    // the world dimmed, darker on the right where the talk is
    box(0, 0, 1600, 900, {8, 6, 4, 110});
    for (int k = 0; k < 16; ++k)  // darker to the right, where the talk is, in soft steps
        box(760 + k * 52.5f, 0, 53, 900, {8, 6, 4, 8 + k * 7});
    if (v.sheet.dog) drawSheet(v.sheet);
    drawTalk(v);
}

#ifdef FD_TEST
void talkFontSurvey() {
    static const char* kFaces[] = {"$body", "$body1", "$body2", "$body3", "$title", "$title1", "$title2", "$hud",
                                   "$handwritten", "$Handwriting", "$journal", "$ledger", "$catalog1", "$catalog2",
                                   "$catalog3", "$catalog4", "$catalog5", "$chalk", "$wanted", "$gtaCash"};
    box(0, 0, 1600, 900, {20, 16, 12, 200});
    float y = 20;
    for (const char* f : kFaces) {
        say(std::string(f) + "  Свидетельство о происхождении · Мики · J. Whitaker · 1234567890", 40, y, 20, kLight, Left, f);
        y += 42;
    }
    // width calibration: a run of 40 characters and a bar 40 × talk.cw × scale long under it
    std::string run = "СВИДЕТЕЛЬСТВО о происхождении собаки 1234";
    say(run, 900, 40, 20, kGold);
    box(900, 66, textPx(run, 20), 3, kGold);
    say(run, 900, 90, 20, kGold, Left, "$title");
    box(900, 116, textPx(run, 20, "$title"), 3, kGold);
    say(run, 900, 140, 20, kGold, Left, "$body1");
    box(900, 166, textPx(run, 20, "$body1"), 3, kGold);
}
#endif

}  // namespace fd::ui
