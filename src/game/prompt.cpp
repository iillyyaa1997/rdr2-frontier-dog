#include "game/prompt.h"

#include "game/game.h"

namespace fd::game {

UiPrompt::~UiPrompt() { destroy(); }

void UiPrompt::create(const std::string& control, const std::string& text, int holdMs, int priority) {
    destroy();
    holdMs_ = holdMs;
    handle_ = HUD::_UI_PROMPT_REGISTER_BEGIN();
    HUD::_UI_PROMPT_SET_CONTROL_ACTION(handle_, MISC::GET_HASH_KEY(control.c_str()));
    text_ = text;
    HUD::_UI_PROMPT_SET_TEXT(handle_, literal(text));
    if (holdMs > 0) HUD::_UI_PROMPT_SET_HOLD_MODE(handle_, holdMs);
    else HUD::_UI_PROMPT_SET_STANDARD_MODE(handle_, false);
    HUD::_UI_PROMPT_SET_PRIORITY(handle_, priority);
    HUD::_UI_PROMPT_SET_ATTRIBUTE(handle_, 18, true);  // as the game's own entity prompts
    HUD::_UI_PROMPT_REGISTER_END(handle_);
    HUD::_UI_PROMPT_SET_VISIBLE(handle_, false);
    HUD::_UI_PROMPT_SET_ENABLED(handle_, false);
    visible_ = enabled_ = false;
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
    if (visible != visible_) HUD::_UI_PROMPT_SET_VISIBLE(handle_, visible);
    if (enabled != enabled_) HUD::_UI_PROMPT_SET_ENABLED(handle_, enabled);
    visible_ = visible;
    enabled_ = enabled;
}

bool UiPrompt::fired() {
    if (!handle_ || !visible_ || !enabled_) return false;
    bool done = holdMs_ > 0 ? HUD::_UI_PROMPT_HAS_HOLD_MODE_COMPLETED(handle_) != 0
                            : HUD::_UI_PROMPT_HAS_STANDARD_MODE_COMPLETED(handle_, 0) != 0;
    if (done) {
        // re-arm: a completed prompt stays 'completed' until it is toggled
        HUD::_UI_PROMPT_SET_ENABLED(handle_, false);
        HUD::_UI_PROMPT_SET_ENABLED(handle_, true);
    }
    return done;
}

}  // namespace fd::game
