#include "ui/card.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "dog/companion.h"
#include "dog/home.h"
#include "dog/stables.h"
#include "dog/strays.h"
#include "game/game.h"
#include "game/prompt.h"
#include "mod/context.h"
#include "ui/draw.h"

namespace fd::ui {

static int g_page = 0;
static game::UiPrompt g_info;  // our "Show info" in the dog's prompt group (the game's one hides with its control)
static DogRecord g_dog;
static Ped g_ped = 0;

void cardShow(const DogRecord& d, Ped ped) {
    g_dog = d;
    g_ped = ped;
    if (!g_page) g_page = 1;
}

void cardPage(int page) {
    if (g_page) g_page = std::clamp(page, 1, 3);
}

void cardHide() {
    g_page = 0;
    g_ped = 0;
}

static void header(const std::string& t, float x, float y, float w) {
    text(t, x, y, 0.32f, 190, 182, 165, 255, "$title");
    sprite("generic_textures", "list_item_h_line_narrow", x, y + 0.030f, w, 0.0025f, 233, 228, 216, 90);
}

static void stars(float x, float y, int on, int total) {
    for (int i = 0; i < total; ++i)
        sprite("generic_textures", i < on ? "star" : "star_outline", x + i * 0.0145f, y, 0.0105f, 0.0187f, 233, 228, 216, 255);
}

// the page counter; the Show-info prompt (bottom right, with the button of the player's device) names the next step:
// one button cycles card -> page 2 -> closed (18.11 his pick: the D-pad arrows fought the game's shoulder swap while
// aiming, and letting go of the aim closes the card)
static void pagerHint(float x, float y, const std::string& label) {
    text(label, x, y, 0.32f, 190, 182, 165, 255);
}

// Page 2 can run off the card (Nexus 22.11, tzverg: «интерфейс с кириллицей на второй странице порой выходит за
// рамки» on dogs with many traits): its height is measured first, the card grows to it, long trait descriptions wrap
// onto a second line, and when traits and skills together do not fit the screen the skills move to a page 3.
static bool g_split = false;
static int lastPage() { return g_split ? 3 : 2; }

static std::vector<std::string> wrapWords(const std::string& s, size_t maxChars) {
    std::vector<std::string> out;
    std::string line, word;
    auto len = [](const std::string& t) { size_t n = 0; for (unsigned char ch : t) if ((ch & 0xC0) != 0x80) ++n; return n; };
    auto flush = [&]() {
        if (word.empty()) return;
        if (!line.empty() && len(line) + 1 + len(word) > maxChars) { out.push_back(line); line = word; }
        else line = line.empty() ? word : line + " " + word;
        word.clear();
    };
    for (char ch : s) { if (ch == ' ') flush(); else word += ch; }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

static void draw() {
    auto& c = ctx();
    const DogRecord& d = g_dog;
    const BreedDef* b = c.breeds.find(d.breed);
    const size_t descChars = size_t(c.tune("card.desc_chars", 52));
    std::vector<std::pair<std::string, bool>> traits;
    for (const auto& id : d.traits) traits.push_back({id, true});
    if (b && d.grade == Grade::Pedigree)
        for (const auto& id : b->bond3Traits) traits.push_back({id, d.bondLevel >= 3});
    bool hardening = b && b->mutt && d.grade == Grade::Ordinary;
    // the heights of page 2's parts (the same steps as the drawing below)
    float traitsH = 0.040f + (traits.empty() ? 0.031f : 0.0f) + (hardening ? 0.031f : 0.0f);
    for (const auto& [id, on] : traits) {
        size_t lines = on ? std::max<size_t>(1, wrapWords(c.t("trait." + id + ".desc", d.female), descChars).size()) : 1;
        traitsH += 0.054f + 0.022f * float(std::min<size_t>(lines, 3) - 1);
    }
    float skillsH = 0.004f + 0.040f + 0.028f * float(c.breeds.skills().size()) + 0.008f;
    float head = 0.016f + 0.058f + 0.032f + 0.016f, foot = 0.040f;
    g_split = 0.12f + head + traitsH + skillsH + foot > 0.95f;
    if (g_page == 3 && !g_split) g_page = 2;
    float x = 0.70f, y = 0.12f, w = 0.27f, pad = 0.016f;
    float h = g_page == 1 ? 0.71f : head + foot + (g_page == 3 ? skillsH : traitsH + (g_split ? 0.0f : skillsH));
    float ix = x + pad, iw = w - 2 * pad;
    sprite("generic_textures", "inkroller_1a", x - 0.02f, y - 0.02f, w + 0.04f, h + 0.04f, 0, 0, 0, 235);
    rect(x, y, w, h, 0, 0, 0, 120);
    float cy = y + pad;
    text(d.name.empty() ? c.t("dog.default_name") : d.name, ix, cy, 0.85f, 233, 228, 216, 255, "$title");
    cy += 0.058f;
    std::string sub = c.gradeText(d) + " · " + c.breedText(d) + " · " + c.t(d.female ? "card.female" : "card.male");
    text(g_page == 1 ? sub : c.t(g_page == 3 ? "card.sub3" : (g_split ? "card.sub2_traits" : "card.sub2")), ix, cy, 0.38f, 190, 182, 165, 255);
    cy += 0.032f;
    sprite("generic_textures", "title_divider", ix, cy, iw, 0.006f, 233, 228, 216, 200);
    cy += 0.016f;
    const float row = 0.031f, label = 0.36f;
    if (g_page == 1) {
        int hp = g_ped && game::alive(g_ped)
                     ? int(100.0f * ENTITY::GET_ENTITY_HEALTH(g_ped) / std::max(1, ENTITY::GET_ENTITY_MAX_HEALTH(g_ped, false)))
                     : int(d.healthFrac * 100);
        text(c.t("card.health") + "  " + std::to_string(hp) + "%", ix, cy, label, 233, 228, 216, 255);
        cy += row + 0.006f;
        header(c.t("card.stats"), ix, cy, iw);
        cy += 0.040f;
        float segX = ix + 0.105f, segW = 0.021f, segH = 0.009f, gap = 0.004f;
        for (int i = 0; i < StatCount; ++i) {
            int v = d.stats[i];
            bool nat5 = d.fiveStat == i;
            text(c.t("stat." + std::to_string(i + 1)), ix, cy, label, 233, 228, 216, 255);
            for (int k = 1; k <= 5; ++k) {
                int r = 233, g = 228, bl = 216, a = k <= v ? 235 : 45;
                if (k == 5 && nat5) { r = 201; g = 164; bl = 90; a = v >= 5 ? 255 : 90; }
                rect(segX + (k - 1) * (segW + gap), cy + 0.012f, segW, segH, r, g, bl, a);
            }
            text(std::to_string(v), segX + 5 * (segW + gap) + 0.006f, cy, label, 190, 182, 165, 255);
            cy += row - 0.004f;
            // what the stat does, small under it (his ask 17.11: «не понятно за что отвечает каждая характеристика»)
            text(c.t("stat." + std::to_string(i + 1) + ".what"), ix + 0.008f, cy, 0.28f, 170, 163, 148, 255);
            cy += 0.022f;
        }
        cy += 0.006f;
        header(c.t("card.bonding"), ix, cy, iw);
        cy += 0.040f;
        stars(ix, cy + 0.004f, d.bondLevel, 3);
        double need = d.bondLevel == 1 ? c.tunef("bond.points.2", 100) : c.tunef("bond.points.3", 400);
        double from = d.bondLevel == 1 ? 0 : (d.bondLevel == 2 ? c.tunef("bond.points.2", 100) : need);
        int pct = d.bondLevel >= 3 ? 100 : int(100.0 * (d.bondPoints - from) / std::max(1.0, need - from));
        std::string bond = d.bondLevel >= 3 ? c.f("card.bond_level", {{"level", "3"}})
                                            : c.f("card.bond_progress", {{"level", std::to_string(d.bondLevel)}, {"pct", std::to_string(std::max(0, pct))}});
        text(bond, ix + 0.050f, cy, label, 190, 182, 165, 255);
        cy += row + 0.012f;
        pagerHint(ix, cy, c.f("card.page_of", {{"n", "1"}, {"of", std::to_string(lastPage())}}));
        return;
    }
    if (g_page == 3) {
        cy -= 0.004f;
        goto skills;
    }
    header(c.t("card.traits"), ix, cy, iw);
    cy += 0.040f;
    if (traits.empty()) {
        text(c.t("card.no_traits"), ix, cy, label, 190, 182, 165, 255);
        cy += row;
    }
    for (const auto& [id, on] : traits) {
        const TraitDef* td = c.breeds.trait(id);
        int a = on ? 255 : 110;
        text(std::to_string(td ? td->price : 1), ix, cy, label, 201, 164, 90, a, "$title");
        text(c.t("trait." + id + ".name", d.female), ix + 0.016f, cy, label, 233, 228, 216, a);
        std::vector<std::string> lines = on ? wrapWords(c.t("trait." + id + ".desc", d.female), descChars)
                                            : std::vector<std::string>{c.t("card.locked")};
        if (lines.empty()) lines.push_back("");
        if (lines.size() > 3) { lines.resize(3); lines[2] += "…"; }
        for (size_t k = 0; k < lines.size(); ++k)
            text(lines[k], ix + 0.016f, cy + 0.024f + 0.022f * float(k), 0.30f, 190, 182, 165, a);
        cy += 0.054f + 0.022f * float(lines.size() - 1);
    }
    if (hardening) {
        text(c.f("card.hardening", {{"n", std::to_string(d.hardenStats + d.hardenTraits)}}), ix, cy, label, 201, 164, 90, 255);
        cy += row;
    }
    if (g_split) {
        cy += 0.008f;
        pagerHint(ix, cy, c.f("card.page_of", {{"n", "2"}, {"of", "3"}}));
        return;
    }
skills:
    cy += 0.004f;
    header(c.t("card.skills"), ix, cy, iw);
    cy += 0.040f;
    for (const auto& sk : c.breeds.skills()) {
        text(c.t("skill." + sk), ix, cy, label, 233, 228, 216, 255);
        stars(ix + 0.105f, cy + 0.004f, d.skill(sk), 3);
        cy += 0.028f;
    }
    cy += 0.008f;
    pagerHint(ix, cy, c.f("card.page_of", {{"n", std::to_string(lastPage())}, {"of", std::to_string(lastPage())}}));
}

// The card a focused dog of ours gets: the companion, a stray (known before taming, D102), a dog for sale, a dog at home.
static bool cardFor(Ped focus, DogRecord& out) {
    auto& comp = companion();
    if (!focus || !game::alive(focus)) return false;
    if (comp.present() && focus == comp.ped()) {
        if (DogRecord* d = comp.record()) out = *d;
        return comp.record() != nullptr;
    }
    if (stables::saleCard(focus, out) || home::homeCard(focus, out)) return true;
    if (!comp.isShopOrHomeDog(focus) && ctx().breeds.byAnimalType(ENTITY::_GET_PED_ANIMAL_TYPE(focus))) {
        out = strays::cardFor(focus);
        return true;
    }
    return false;
}

void cardStart() {
    // a control the focus prompt list shows on the same key (Q; measured 16.11: cover is on Q too but its prompt never
    // shows while aiming); the press is read on it and on the taken-over Show-info control
    g_info.create(ctx().ini.get("InfoPromptControl", "INPUT_INTERACT_LOCKON_STUDY_BINOCULARS"), ctx().t("prompt.show_info"));
    g_info.setRank(10);
    cardHide();
}

#ifdef FD_TEST
void cardInfoControl(const std::string& control) { g_info.create(control, ctx().t("prompt.show_info")); }
#endif

void cardTick() {
    auto& comp = companion();
    Entity focus = 0;
    PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY(PLAYER::PLAYER_ID(), &focus, false, false);
    // the game's own "Show info" (its control measured 16.11: Q / R1) shows the model's species card - "Siberian Husky"
    // on the Shiba - so for our dogs that control is taken over and opens our card instead (again: closes it)
    static const Hash kShow = MISC::GET_HASH_KEY(ctx().ini.get("InfoControl", "INPUT_INTERACT_LOCKON_TARGET_INFO").c_str());
    DogRecord rec;
    bool ours = cardFor(focus, rec);
    if (ours) g_info.setTarget(focus);
    const char* next = !(g_page && g_ped == focus) ? "prompt.show_info"
                       : g_page == 1                     ? "prompt.next_page"
                       : (g_page == 2 && g_split)        ? "prompt.next_page3"
                                                         : "prompt.hide_info";
    g_info.setText(ctx().t(next));
    g_info.show(ours);
    if (ours) {
        PAD::DISABLE_CONTROL_ACTION(0, kShow, false);
        static const Hash kPrompt = MISC::GET_HASH_KEY(ctx().ini.get("InfoPromptControl", "INPUT_INTERACT_LOCKON_STUDY_BINOCULARS").c_str());
        if (PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, kShow) || PAD::IS_CONTROL_JUST_PRESSED(0, kPrompt)) {
            if (g_page == 1 && g_ped == focus) g_page = 2;
            else if (g_page == 2 && g_split && g_ped == focus) g_page = 3;
            else if (g_page && g_ped == focus) cardHide();
            else {
                g_page = 0;
                cardShow(rec, focus);
            }
        }
    }
    if (!g_page) return;
    // the card stays while Arthur keeps aiming at the dog, also from afar (18.11: «если собака даже не далеко отойдет, то
    // пропадает информация о ней» — it closed beyond 6 m)
    bool held = focus == g_ped || (g_ped == comp.ped() && comp.aimedFromAfar());
    if (g_ped && (!held || game::dist(g_ped, game::player()) > comp.whistleRange())) {
        g_page = 0;
        g_ped = 0;
        return;
    }
    if (g_ped == comp.ped())
        if (DogRecord* d = comp.record()) g_dog = *d;  // live values
    draw();
}

}  // namespace fd::ui
