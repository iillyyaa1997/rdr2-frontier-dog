#include "core/lang.h"

#include "core/fs.h"
#include "core/strutil.h"

namespace fd {

std::string Lang::codeForGameLanguage(int g) {
    static const char* codes[] = {"en", "fr", "de", "it", "es", "pt-BR", "pl", "ru", "ko", "zh-TW", "ja", "es-MX", "zh-CN"};
    if (g >= 0 && g < int(sizeof codes / sizeof codes[0])) return codes[g];
    return "en";
}

void Lang::load(const std::string& dataDir, int gameLanguage, const std::string& forced) {
    std::string want = str::lower(str::trim(forced));
    code_ = (want.empty() || want == "auto") ? codeForGameLanguage(gameLanguage) : want;
    fallback_.load(fs::join(dataDir, "Lang.en.dat"));
    if (code_ == "en" || !main_.load(fs::join(dataDir, "Lang." + code_ + ".dat"))) {
        main_.clear();
        if (code_ != "en") code_ = "en";  // no file for this language: English, Telegraph may translate it
    }
}

std::string Lang::text(const std::string& key, bool female) const {
    if (female) {
        const std::string fk = key + ".f";
        if (main_.has(fk)) return main_.get(fk);
        if (!main_.has(key) && fallback_.has(fk)) return fallback_.get(fk);
    }
    if (main_.has(key)) return main_.get(key);
    if (fallback_.has(key)) return fallback_.get(key);
    return key;
}

std::string Lang::format(const std::string& key, const std::vector<std::pair<std::string, std::string>>& args,
                         bool female) const {
    return str::format(text(key, female), args);
}

}  // namespace fd
