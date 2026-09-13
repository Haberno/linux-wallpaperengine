#include <catch2/catch_test_macros.hpp>
#include "WallpaperEngine/Application/ApplicationContext.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include <cstdlib>

using WallpaperEngine::Application::ApplicationContext;

TEST_CASE ("wallpaper languages map locale regions to native UI codes", "[language]") {
    for (const auto& [locale, expected] : std::vector<std::pair<std::string, std::string>> {
        { "en", "en-us" }, { "en_CA.UTF-8", "en-us" }, { "fr_CA.UTF-8", "fr-fr" },
        { "de_DE@euro", "de-de" }, { "es_MX", "es-es" }, { "pt_BR.UTF-8", "pt-br" },
        { "pt", "pt-pt" }, { "pt_PT", "pt-pt" }, { "zh_CN", "zh-chs" },
        { "zh_SG", "zh-chs" }, { "zh_TW.UTF-8", "zh-cht" }, { "zh_HK", "zh-cht" },
        { "zh_MO", "zh-cht" }, { "zh-Hant-TW", "zh-cht" }, { "ZH-CHT", "zh-cht" },
        { "zh-chs", "zh-chs" }, { "no_NO", "nb-no" }, { "ja_JP", "ja-jp" },
        { "fr:en", "fr-fr" }, { "C.UTF-8", "en-us" }, { "POSIX", "en-us" },
        { "unsupported", "en-us" }, { "", "en-us" }
    }) {
        CAPTURE (locale);
        CHECK (ApplicationContext::normalizeLanguage (locale) == expected);
    }
    for (const std::string code : {
        "ar-sa", "be-by", "bg-bg", "cs-cz", "da-dk", "de-de", "el-gr", "en-us", "es-es", "eu-es",
        "fa-ir", "fi-fi", "fr-fr", "he-il", "hu-hu", "id-id", "it-it", "ja-jp", "ko-kr", "lt-lt",
        "nb-no", "nl-nl", "pl-pl", "pt-br", "pt-pt", "ro-ro", "ru-ru", "sk-sk", "sl-si", "sv-se",
        "th-th", "tr-tr", "uk-ua", "vi-vn", "zh-chs", "zh-cht"
    }) {
        CAPTURE (code);
        CHECK (ApplicationContext::normalizeLanguage (code) == code);
    }
}

TEST_CASE ("language defaults follow message locale precedence and allow explicit changes", "[language]") {
    std::vector<std::pair<const char*, std::optional<std::string>>> saved;
    for (const char* name : { "LC_ALL", "LC_MESSAGES", "LANG" }) {
        const char* value = std::getenv (name);
        saved.emplace_back (name, value ? std::optional<std::string> (value) : std::nullopt);
    }
    WallpaperEngine::Data::Utils::ScopeGuard restore ([&] {
        for (const auto& [name, value] : saved) {
            if (value) setenv (name, value->c_str (), 1);
            else unsetenv (name);
        }
    });
    setenv ("LANG", "de_DE.UTF-8", 1);
    setenv ("LC_MESSAGES", "fr_CA.UTF-8", 1);
    setenv ("LC_ALL", "C.UTF-8", 1);
    CHECK (ApplicationContext (0, nullptr).getLanguage () == "en-us");
    setenv ("LC_ALL", "", 1);
    CHECK (ApplicationContext (0, nullptr).getLanguage () == "fr-fr");
    unsetenv ("LC_MESSAGES");
    CHECK (ApplicationContext (0, nullptr).getLanguage () == "de-de");
    unsetenv ("LANG");
    ApplicationContext context (0, nullptr);
    CHECK (context.getLanguage () == "en-us");
    context.setLanguage ("zh_TW.UTF-8");
    CHECK (context.getLanguage () == "zh-cht");
    context.setLanguage ("unknown");
    CHECK (context.getLanguage () == "en-us");
}
