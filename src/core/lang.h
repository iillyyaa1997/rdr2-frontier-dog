// Texts: FrontierDog\Lang.<code>.dat (UTF-8 key=value). The language follows the game (GET_CURRENT_LANGUAGE) unless
// FrontierDog.ini sets Language=<code>; a key missing in that file falls back to Lang.en.dat, then to the key itself.
// A female dog uses '<key>.f' when it exists.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "core/kvfile.h"

namespace fd {

class Lang {
public:
    // gameLanguage: the game's enum (0 English … 7 Russian …); forced: ini override ("" or "auto" = follow the game)
    void load(const std::string& dataDir, int gameLanguage, const std::string& forced);
    const std::string& code() const { return code_; }
    std::string text(const std::string& key, bool female = false) const;
    std::string format(const std::string& key, const std::vector<std::pair<std::string, std::string>>& args,
                       bool female = false) const;

    static std::string codeForGameLanguage(int gameLanguage);

private:
    std::string code_ = "en";
    KvFile main_, fallback_;
};

}  // namespace fd
