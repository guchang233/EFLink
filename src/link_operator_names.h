#pragma once
// Translates Endfield model paths or character template IDs (e.g.
// "Assets/.../chr_0017_yvonne_postmodel.prefab" or "chr_0017_yvonne")
// into compact human-readable operator names for HUD & combat stats.
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace eflink {

inline std::string extractOperatorCode(std::string_view raw) {
    if (raw.empty()) return {};
    // Strip path prefix if present.
    if (const auto slash = raw.find_last_of("/\\"); slash != std::string_view::npos) {
        raw = raw.substr(slash + 1);
    }
    // Strip file extension.
    if (const auto dot = raw.find('.'); dot != std::string_view::npos) {
        raw = raw.substr(0, dot);
    }
    std::string lower(raw.begin(), raw.end());
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return char(std::tolower(c)); });
    // Trim common asset suffixes.
    for (const char* suffix : {"_postmodel", "_model", "_prefab", "_skin", "_lod0", "_battle"}) {
        const auto pos = lower.find(suffix);
        if (pos != std::string::npos) lower.resize(pos);
    }
    // Extract token after "chr_XXXX_" when present.
    if (lower.rfind("chr_", 0) == 0) {
        const auto second = lower.find('_', 4);
        if (second != std::string::npos && second + 1 < lower.size()) {
            return lower.substr(second + 1);
        }
    }
    return lower;
}

inline std::string operatorDisplayName(std::string_view raw) {
    const std::string code = extractOperatorCode(raw);
    if (code.empty()) return {};
    if (code.rfind("endmin", 0) == 0) return "管理员";
    if (code == "pelica" || code == "perlica") return "佩丽卡";
    if (code == "chen" || code == "chenqianyu") return "陈千语";
    if (code == "wolfgd" || code == "wulfgard") return "狼卫";
    if (code == "ember" || code == "ikut") return "余烬";
    if (code == "avywen" || code == "avywenna") return "艾维文娜";
    if (code == "aglina" || code == "angelina" || code == "gilberta") return "洁尔佩塔";
    if (code == "aurora" || code == "snowshine") return "昼雪";
    if (code == "lifeng") return "黎风";
    if (code == "laevat" || code == "laevatain" || code == "surtr") return "莱万汀";
    if (code == "yvonne") return "伊冯";
    if (code == "dapan") return "大潘";
    if (code == "ardelia" || code == "eyja") return "艾尔黛拉";
    if (code == "xaihi") return "赛希";
    if (code == "pogranichnik" || code == "pog") return "骏卫";
    if (code == "last_rite" || code == "lastrite") return "别礼";
    if (code == "alesh") return "阿列什";
    if (code == "antal") return "安塔尔";
    if (code == "arclight") return "弧光";
    if (code == "catch" || code == "catcher") return "卡契尔";
    if (code == "estella") return "埃特拉";
    if (code == "fluorite") return "萤石";
    if (code == "akaguri" || code == "akekuri") return "秋栗";
    if (code == "rossi") return "洛茜";
    // Fallback: capitalize the codename cleanly.
    std::string pretty = code.substr(0, (std::min<std::size_t>)(code.size(), 24));
    if (!pretty.empty()) pretty[0] = char(std::toupper(static_cast<unsigned char>(pretty[0])));
    return pretty;
}

} // namespace eflink
