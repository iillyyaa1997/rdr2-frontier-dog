// A game prompt (button hint) — standard press or hold — optionally shown in a target entity's prompt group (the
// prompts you see when you focus the dog, like the horse's). Text can change at any time.
#pragma once
#include <string>

#include "sdk/natives.gen.h"

namespace fd::game {

class UiPrompt {
public:
    UiPrompt() = default;
    UiPrompt(const UiPrompt&) = delete;
    UiPrompt& operator=(const UiPrompt&) = delete;
    ~UiPrompt();

    void create(const std::string& control, const std::string& text, int holdMs = 0, int priority = 1);
    void destroy();
    bool created() const { return handle_ != 0; }

    void setText(const std::string& text);
    void setTarget(Entity target);   // 0 = free prompt (bottom right, no group)
    void setGroup(int group);
    void show(bool visible, bool enabled = true);
    bool fired();                    // completed press/hold this frame (once)

private:
    Prompt handle_ = 0;
    int holdMs_ = 0;
    std::string text_;
    Entity target_ = 0;
    int group_ = 0;
    bool visible_ = false, enabled_ = false;
};

}  // namespace fd::game
