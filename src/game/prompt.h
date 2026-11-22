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
    void setGroup(int group, int tab = 0);  // 0 = free prompt; tab = the page of a group with pages
    void show(bool visible, bool enabled = true);  // wanted this frame; applyFrame decides what the game gets
    void setRank(int rank) { rank_ = rank; }      // lower = more important when a group is full
    bool fired();                    // completed press/hold this frame (once)
    // once per frame after every module: the game shows at most kGroupMax prompts of one group (measured 16.11: the
    // dog's 6th prompt - Follow - silently dropped), so the best ranked wanted ones are shown, the rest hidden
    static void applyFrame();
    static constexpr int kGroupMax = 5;
#ifdef FD_TEST
    bool visibleForTest() const { return handle_ && visible_; }
    const std::string& textForTest() const { return text_; }
#endif

private:
    Prompt handle_ = 0;
    int holdMs_ = 0;
    Hash control_ = 0;
    bool latched_ = false;  // fired; the key must be let go before it can fire again
    std::string text_;
    Entity target_ = 0;
    int group_ = 0, tab_ = 0;
    bool visible_ = false, enabled_ = false;  // what the game has
    bool want_ = false, wantEnabled_ = false;  // what the module asked for this frame
    int rank_ = 50, order_ = 0;
};

#ifdef FD_TEST
void testListPrompts();
void testPress(const std::string& part);
#endif

}  // namespace fd::game
