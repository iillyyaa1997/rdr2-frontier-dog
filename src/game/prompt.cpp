#include "game/prompt.h"

#include "game/game.h"

#include <algorithm>
#include <map>
#include <vector>

#ifdef FD_TEST
#include "core/log.h"
#endif

namespace fd::game {

static std::vector<UiPrompt*> g_registry;  // every created prompt (applyFrame)
static int g_nextOrder = 0;

#ifdef FD_TEST
// test hooks: 'prompts' lists the visible prompts, 'press <text part>' fires the first visible one whose text contains it
// (checks the logic behind a prompt; the game input binding itself still needs a real button)
static std::vector<UiPrompt*> g_all;
static std::string g_press;
void testListPrompts() {
    for (UiPrompt* p : g_all)
        if (p->visibleForTest()) FD_INFO("test: prompt '%s'", p->textForTest().c_str());
}
void testPress(const std::string& part) { g_press = part; }
#endif

UiPrompt::~UiPrompt() {
    destroy();
    g_registry.erase(std::remove(g_registry.begin(), g_registry.end(), this), g_registry.end());
#ifdef FD_TEST
    g_all.erase(std::remove(g_all.begin(), g_all.end(), this), g_all.end());
#endif
}

void UiPrompt::create(const std::string& control, const std::string& text, int holdMs, int priority) {
    destroy();
#ifdef FD_TEST
    if (std::find(g_all.begin(), g_all.end(), this) == g_all.end()) g_all.push_back(this);
#endif
    if (std::find(g_registry.begin(), g_registry.end(), this) == g_registry.end()) {
        g_registry.push_back(this);
        order_ = g_nextOrder++;
    }
    holdMs_ = holdMs;
    handle_ = HUD::_UI_PROMPT_REGISTER_BEGIN();
    control_ = MISC::GET_HASH_KEY(control.c_str());
    latched_ = false;
    HUD::_UI_PROMPT_SET_CONTROL_ACTION(handle_, control_);
    text_ = text;
    HUD::_UI_PROMPT_SET_TEXT(handle_, literal(text));
    if (holdMs > 0) HUD::_UI_PROMPT_SET_HOLD_MODE(handle_, holdMs);
    else HUD::_UI_PROMPT_SET_STANDARD_MODE(handle_, false);
    HUD::_UI_PROMPT_SET_PRIORITY(handle_, priority);
    HUD::_UI_PROMPT_SET_ATTRIBUTE(handle_, 18, true);  // as the game's own entity prompts
    HUD::_UI_PROMPT_REGISTER_END(handle_);
    HUD::_UI_PROMPT_SET_VISIBLE(handle_, false);
    HUD::_UI_PROMPT_SET_ENABLED(handle_, false);
    visible_ = enabled_ = want_ = wantEnabled_ = false;
    target_ = 0;
    group_ = 0;
}

void UiPrompt::destroy() {
    if (handle_) HUD::_UI_PROMPT_DELETE(handle_);
    handle_ = 0;
}

void UiPrompt::setText(const std::string& text) {
    if (!handle_ || text == text_) return;
    text_ = text;
    HUD::_UI_PROMPT_SET_TEXT(handle_, literal(text));
}

void UiPrompt::setTarget(Entity target) {
    if (!handle_) return;
    int group = target ? HUD::_UI_PROMPT_GET_GROUP_ID_FOR_TARGET_ENTITY(target) : 0;
    target_ = target;
    setGroup(group);
}

void UiPrompt::setGroup(int group) {
    if (!handle_ || group == group_) return;
    group_ = group;
    HUD::_UI_PROMPT_SET_GROUP(handle_, group, 0);
}

void UiPrompt::show(bool visible, bool enabled) {
    if (!handle_) return;
    // hidden = disabled too, as the game's scripts and the old mod do: a hidden prompt left enabled still claims its
    // button, and ~40 of them took the game's own prompts away (no pickup, no camp prompts; 16.11 his play)
    want_ = visible;
    wantEnabled_ = visible && enabled;
}

void UiPrompt::applyFrame() {
    std::map<int, std::vector<UiPrompt*>> groups;
    for (UiPrompt* p : g_registry)
        if (p->handle_ && p->want_) groups[p->group_].push_back(p);
    std::vector<UiPrompt*> shown;
    for (auto& [group, list] : groups) {
        std::sort(list.begin(), list.end(), [](const UiPrompt* a, const UiPrompt* b) {
            return a->rank_ != b->rank_ ? a->rank_ < b->rank_ : a->order_ < b->order_;
        });
        for (size_t i = 0; i < list.size() && int(i) < kGroupMax; ++i) shown.push_back(list[i]);
    }
    for (UiPrompt* p : g_registry) {
        if (!p->handle_) continue;
        bool in = std::find(shown.begin(), shown.end(), p) != shown.end();
        bool visible = p->want_ && in, enabled = p->wantEnabled_ && in;
        if (visible != p->visible_) HUD::_UI_PROMPT_SET_VISIBLE(p->handle_, visible);
        if (enabled != p->enabled_) HUD::_UI_PROMPT_SET_ENABLED(p->handle_, enabled);
        p->visible_ = visible;
        p->enabled_ = enabled;
    }
}

bool UiPrompt::fired() {
    if (!handle_ || !visible_ || !enabled_) return false;
    bool done = holdMs_ > 0 ? HUD::_UI_PROMPT_HAS_HOLD_MODE_COMPLETED(handle_) != 0
                            : HUD::_UI_PROMPT_HAS_STANDARD_MODE_COMPLETED(handle_, 0) != 0;
    // once per press: a hold prompt re-armed while the key is still down completes again at once (17.11: the stable
    // hand put the collar on and took it off ten times in a second, $8 each). Wait for the key to be let go.
    if (latched_) {
        bool held = PAD::IS_CONTROL_PRESSED(0, control_) || PAD::IS_DISABLED_CONTROL_PRESSED(0, control_);
        if (done) {
            HUD::_UI_PROMPT_SET_ENABLED(handle_, false);
            HUD::_UI_PROMPT_SET_ENABLED(handle_, true);
        }
        if (held) return false;
        latched_ = false;
        if (done) return false;
    }
#ifdef FD_TEST
    if (!done && !g_press.empty() && text_.find(g_press) != std::string::npos) {
        FD_INFO("test: pressed '%s'", text_.c_str());
        g_press.clear();
        return true;
    }
#endif
    if (done) {
        // re-arm: a completed prompt stays 'completed' until it is toggled
        HUD::_UI_PROMPT_SET_ENABLED(handle_, false);
        HUD::_UI_PROMPT_SET_ENABLED(handle_, true);
        latched_ = true;
    }
    return done;
}

}  // namespace fd::game
