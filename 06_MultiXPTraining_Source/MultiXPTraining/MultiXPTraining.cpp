#define NOMINMAX
#include <Windows.h>
#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/CharStats.h>
#include <kenshi/Character.h>
#include <kenshi/AI/AITaskSystem.h>
#include <kenshi/Building/Building.h>
#include <kenshi/Building/UseableStuff.h>
#include <kenshi/GameData.h>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstring>

class AI { public: AITaskSytem* getTaskSystem() const; };

namespace {
const char* kPluginName = "MultiXPTraining";
const char* kVersion = "1.0.0-rc2-quality-assurance";

struct Config {
    bool enabled;
    float globalXpMultiplier;
    float globalCapMultiplier;
    bool enableCaps;
    Config() : enabled(true), globalXpMultiplier(1.0f), globalCapMultiplier(1.0f), enableCaps(true) {}
};

struct SkillSetting {
    float multiplier;
    float cap;
    SkillSetting() : multiplier(0.0f), cap(0.0f) {}
};

struct SourceSettings {
    bool enabled;
    std::string displayName;
    std::map<int, SkillSetting> skills;
    SourceSettings() : enabled(false) {}
};

struct Identity {
    bool valid;
    std::string id;
    std::string name;
    Identity() : valid(false) {}
};

struct SkillDefinition {
    StatsEnumerated stat;
    const char* key;
};

static const SkillDefinition kSkills[] = {
    { STAT_STRENGTH,       "Strength" },
    { STAT_TOUGHNESS,      "Toughness" },
    { STAT_DEXTERITY,      "Dexterity" },
    { STAT_MELEE_ATTACK,   "MeleeAttack" },
    { STAT_MELEE_DEFENCE,  "MeleeDefense" },
    { STAT_DODGE,          "Dodge" },
    { STAT_MARTIALARTS,    "MartialArts" },
    { STAT_KATANAS,        "Katanas" },
    { STAT_SABRES,         "Sabres" },
    { STAT_HACKERS,        "Hackers" },
    { STAT_BLUNT,          "Blunt" },
    { STAT_POLEARMS,       "Polearms" },
    { STAT_HEAVYWEAPONS,   "HeavyWeapons" },
    { STAT_CROSSBOWS,      "Crossbows" },
    { STAT_FRIENDLY_FIRE,  "PrecisionShooting" },
    { STAT_PERCEPTION,     "Perception" },
    { STAT_LOCKPICKING,    "Lockpicking" },
    { STAT_THIEVING,       "Thieving" },
    { STAT_ASSASSINATION,  "Assassination" },
    { STAT_TURRETS,        "Turrets" }
};
static const size_t kSkillCount = sizeof(kSkills) / sizeof(kSkills[0]);

typedef void (*XpTrainingFn)(CharStats*, float, float, float&, float, StatsEnumerated);

HMODULE g_module = NULL;
Config g_config;
XpTrainingFn g_original = NULL;
std::map<std::string, SourceSettings> g_sources;

std::string Dir() {
    char p[MAX_PATH] = {0};
    GetModuleFileNameA(g_module, p, MAX_PATH);
    std::string s(p);
    size_t x = s.find_last_of("\\/");
    return x == std::string::npos ? "." : s.substr(0, x);
}

std::string TrimLower(std::string v) {
    v.erase(std::remove_if(v.begin(), v.end(), ::isspace), v.end());
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    return v;
}

bool ReadBool(const char* sec, const char* key, bool d, const std::string& f) {
    char b[32] = {0};
    GetPrivateProfileStringA(sec, key, d ? "true" : "false", b, sizeof(b), f.c_str());
    std::string v = TrimLower(b);
    if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
    if (v == "false" || v == "no" || v == "off" || v == "0") return false;
    return d;
}

float ReadFloat(const char* sec, const char* key, float d, const std::string& f) {
    char b[64] = {0};
    std::ostringstream ss;
    ss << d;
    GetPrivateProfileStringA(sec, key, ss.str().c_str(), b, sizeof(b), f.c_str());
    return (float)atof(b);
}

bool HasKey(const char* sec, const char* key, const std::string& f) {
    const char* sentinel = "__MULTIXP_MISSING_KEY__";
    char b[128] = {0};
    GetPrivateProfileStringA(sec, key, sentinel, b, sizeof(b), f.c_str());
    return strcmp(b, sentinel) != 0;
}

std::string ReadString(const char* sec, const char* key, const std::string& d, const std::string& f) {
    char b[512] = {0};
    GetPrivateProfileStringA(sec, key, d.c_str(), b, sizeof(b), f.c_str());
    return std::string(b);
}

bool GetIdentity(Character* c, Identity& out) {
    out = Identity();
    if (!c) return false;
    AI* ai = c->getAI();
    if (!ai) return false;
    AITaskSytem* ts = ai->getTaskSystem();
    if (!ts) return false;
    const TaskMatch& g = ts->getCurrentGoal();
    if (!g.subject.isValid() || g.subject.isNull()) return false;
    Building* b = g.subject.getBuilding();
    if (!b) return false;
    GameData* d = b->getGameData();
    UseableStuff* u = b->getUseableStuff();
    if (!d || !u || !d->isValid()) return false;
    out.valid = true;
    out.id = d->stringID;
    out.name = b->getName();
    return true;
}

void ApplySourceFile(const std::string& f) {
    char secs[32768] = {0};
    GetPrivateProfileSectionNamesA(secs, sizeof(secs), f.c_str());
    for (char* p = secs; *p; p += strlen(p) + 1) {
        std::string section = p;
        if (section == "Metadata") continue;

        SourceSettings& source = g_sources[section];
        if (HasKey(section.c_str(), "Enabled", f))
            source.enabled = ReadBool(section.c_str(), "Enabled", source.enabled, f);
        if (HasKey(section.c_str(), "DisplayName", f))
            source.displayName = ReadString(section.c_str(), "DisplayName", source.displayName, f);

        for (size_t i = 0; i < kSkillCount; ++i) {
            std::string multiplierKey = std::string(kSkills[i].key) + "Multiplier";
            std::string capKey = std::string(kSkills[i].key) + "Cap";
            SkillSetting& setting = source.skills[(int)kSkills[i].stat];
            if (HasKey(section.c_str(), multiplierKey.c_str(), f))
                setting.multiplier = ReadFloat(section.c_str(), multiplierKey.c_str(), setting.multiplier, f);
            if (HasKey(section.c_str(), capKey.c_str(), f))
                setting.cap = ReadFloat(section.c_str(), capKey.c_str(), setting.cap, f);
        }
    }
}

void Load() {
    std::string ini = Dir() + "\\MultiXPTraining.ini";
    g_config.enabled = ReadBool("General", "Enabled", true, ini);
    g_config.globalXpMultiplier = ReadFloat("General", "GlobalXpMultiplier", 1.0f, ini);
    g_config.globalCapMultiplier = ReadFloat("General", "GlobalCapMultiplier", 1.0f, ini);
    g_config.enableCaps = ReadBool("General", "EnableCaps", true, ini);

    g_sources.clear();
    const char* files[] = {
        "00_Vanilla.ini",
        "50_OfficialExtensions.ini",
        "90_ModSupport.ini",
        "99_UserOverride.ini"
    };
    int loadedFiles = 0;
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        std::string f = Dir() + "\\SourceData\\" + files[i];
        DWORD attrs = GetFileAttributesA(f.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            ApplySourceFile(f);
            ++loadedFiles;
        }
    }

    std::ostringstream m;
    m << "[" << kPluginName << "] [INFO] Config Enabled=" << (g_config.enabled ? "true" : "false")
      << " GlobalXpMultiplier=" << g_config.globalXpMultiplier
      << " GlobalCapMultiplier=" << g_config.globalCapMultiplier
      << " EnableCaps=" << (g_config.enableCaps ? "true" : "false")
      << " SourceFiles=" << loadedFiles
      << " Sources=" << g_sources.size();
    DebugLog(m.str());

    for (std::map<std::string, SourceSettings>::const_iterator it = g_sources.begin(); it != g_sources.end(); ++it) {
        std::ostringstream q;
        q << "[" << kPluginName << "] [INFO] Source " << it->first
          << " Enabled=" << (it->second.enabled ? "true" : "false")
          << " DisplayName=\"" << it->second.displayName << "\"";
        DebugLog(q.str());
    }
}

void Grant(CharStats* stats, float time, float mult, StatsEnumerated stat, const SkillSetting& setting) {
    if (!g_original || setting.multiplier <= 0.0f) return;
    float cap = setting.cap * g_config.globalCapMultiplier;
    if (g_config.enableCaps && (cap <= 0.0f || stats->getStat(stat, true) >= cap)) return;
    float& ref = stats->getStatRef(stat);
    g_original(stats, time, mult * setting.multiplier * g_config.globalXpMultiplier, ref, cap, stat);
}

void Hook(CharStats* stats, float time, float mult, float& statValue, float upperLimit, StatsEnumerated stat) {
    if (g_original) g_original(stats, time, mult, statValue, upperLimit, stat);
    if (!g_config.enabled || !stats || !stats->me) return;

    Identity id;
    if (!GetIdentity(stats->me, id)) return;
    std::map<std::string, SourceSettings>::const_iterator sourceIt = g_sources.find(id.id);
    if (sourceIt == g_sources.end() || !sourceIt->second.enabled) return;

    const SourceSettings& source = sourceIt->second;
    for (size_t i = 0; i < kSkillCount; ++i) {
        std::map<int, SkillSetting>::const_iterator skillIt = source.skills.find((int)kSkills[i].stat);
        if (skillIt != source.skills.end())
            Grant(stats, time, mult, kSkills[i].stat, skillIt->second);
    }
}
}

BOOL APIENTRY DllMain(HMODULE m, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) {
        g_module = m;
        DisableThreadLibraryCalls(m);
    }
    return TRUE;
}

__declspec(dllexport) void startPlugin() {
    DebugLog(std::string("[") + kPluginName + "] [INFO] Starting version " + kVersion);
    Load();
    intptr_t a = KenshiLib::GetRealAddress(&CharStats::xpTraining);
    KenshiLib::HookStatus s = KenshiLib::AddHook(a, reinterpret_cast<void*>(&Hook), reinterpret_cast<void**>(&g_original));
    std::ostringstream m;
    m << "[" << kPluginName << "] [INFO] xpTraining hook=" << (s == KenshiLib::SUCCESS ? "OK" : "FAILED") << " @0x" << std::hex << a;
    DebugLog(m.str());
}

__declspec(dllexport) void stopPlugin() {
    DebugLog(std::string("[") + kPluginName + "] [INFO] Stopping.");
}
