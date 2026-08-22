#include <Windows.h>
#include <sstream>
#include <iomanip>
#include <string>
#include <map>
#include <algorithm>
#include <vector>
#include <set>

#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/CharStats.h>
#include <kenshi/Character.h>
#include <kenshi/combat/CombatTechniqueData.h>

namespace
{
    enum PluginMode
    {
        MODE_OBSERVE_ONLY = 0,
        MODE_DODGE_THEN_BLOCK = 1
    };

    enum AnimationMode
    {
        ANIMATION_VANILLA_ONLY = 0,
        ANIMATION_COMPATIBLE = 1,
        ANIMATION_CUSTOM_ALLOWLIST = 2
    };

    struct Settings
    {
        int mode;
        bool verbose;
        bool logSelections;
        int maxLogLines;
        float dodgeChanceScale;
        float minimumDodgeChance;
        float maximumDodgeChance;
        bool allowInDefensiveMode;
        bool requireNormalProneState;
        DWORD dodgeCooldownMs;
        int animationMode;
        bool logDiscoveredTechniques;
        bool logSelectedAnimation;
        bool adaptivePriorityEnabled;
        bool logPriorityDecisions;
        std::set<std::string> allowedAnimations;
        std::set<std::string> blockedAnimations;

        Settings()
            : mode(MODE_OBSERVE_ONLY),
              verbose(false),
              logSelections(true),
              maxLogLines(2000),
              dodgeChanceScale(1.0f),
              minimumDodgeChance(0.0f),
              maximumDodgeChance(95.0f),
              allowInDefensiveMode(true),
              requireNormalProneState(true),
              dodgeCooldownMs(0),
              animationMode(ANIMATION_VANILLA_ONLY),
              logDiscoveredTechniques(false),
              logSelectedAnimation(false),
              adaptivePriorityEnabled(false),
              logPriorityDecisions(false)
        {
        }
    };

    Settings g_settings;
    LONG g_logLines = 0;
    LONG g_dodgeSelected = 0;
    LONG g_blockFallback = 0;
    LONG g_vanillaDodgePreserved = 0;
    LONG g_ineligible = 0;
    std::string g_iniPath;
    CombatTechniqueData* g_canonicalStandingDodge = NULL;
    std::vector<CombatTechniqueData*> g_normalStandingDodges;
    uintptr_t g_discoveredTechniqueListAddress = 0;
    std::map<Character*, DWORD> g_lastDodgeTime;

    typedef CombatTechniqueData* (__fastcall* ChooseBlockFn)(
        CharStats* thisptr,
        CutDirection dir,
        float opponentAttackSkill,
        CutOrigination from,
        Character* opponent);

    ChooseBlockFn g_chooseBlockOriginal = NULL;
    intptr_t g_chooseBlockTarget = 0;
    bool g_autoDiscoveryAttempted = false;

    std::string GetThisModulePath()
    {
        HMODULE module = NULL;
        char path[MAX_PATH] = { 0 };
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&GetThisModulePath), &module))
            return std::string();

        const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return std::string();
        return std::string(path, length);
    }

    std::string GetDirectory(const std::string& path)
    {
        const std::string::size_type slash = path.find_last_of("\\/");
        return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
    }

    void Log(const std::string& message)
    {
        const LONG line = InterlockedIncrement(&g_logLines);
        if (line <= g_settings.maxLogLines)
            DebugLog(std::string("[WeaponDodgeGuard] ") + message);
        else if (line == g_settings.maxLogLines + 1)
            DebugLog("[WeaponDodgeGuard] Log limit reached; further plugin lines suppressed.");
    }

    const char* BoolText(bool value) { return value ? "true" : "false"; }

    float Clamp(float value, float lo, float hi)
    {
        return value < lo ? lo : (value > hi ? hi : value);
    }

    float ReadFloat(const char* section, const char* key, float fallback)
    {
        char buffer[64] = { 0 };
        std::ostringstream def;
        def << fallback;
        GetPrivateProfileStringA(section, key, def.str().c_str(), buffer, sizeof(buffer), g_iniPath.c_str());
        return static_cast<float>(atof(buffer));
    }

    std::string TechniqueDescription(const CombatTechniqueData* technique)
    {
        if (!technique) return "<null>";
        std::ostringstream out;
        out << "animation=\"" << technique->animation << "\""
            << " block=" << BoolText(technique->isBlock)
            << " dodge=" << BoolText(technique->isDodge)
            << " stumbleDodge=" << BoolText(technique->stumbleDodge)
            << " prone=" << BoolText(technique->isProne)
            << " minSkill=" << technique->minSkill
            << " maxSkill=" << technique->maxSkill
            << " maxEncumbrance=" << technique->maxEncumbrance
            << " chanceMult=" << technique->chanceMult;
        return out.str();
    }

    std::string Trim(const std::string& value)
    {
        const std::string whitespace = " \t\r\n";
        const std::string::size_type first = value.find_first_not_of(whitespace);
        if (first == std::string::npos) return std::string();
        const std::string::size_type last = value.find_last_not_of(whitespace);
        return value.substr(first, last - first + 1);
    }

    std::string ToLowerAscii(const std::string& value)
    {
        std::string result(value);
        for (size_t i = 0; i < result.size(); ++i)
        {
            const unsigned char ch = static_cast<unsigned char>(result[i]);
            if (ch >= 'A' && ch <= 'Z')
                result[i] = static_cast<char>(ch - 'A' + 'a');
        }
        return result;
    }

    bool ReadBool(const char* section, const char* key, bool fallback)
    {
        char buffer[64] = { 0 };
        const char* fallbackText = fallback ? "1" : "0";

        GetPrivateProfileStringA(
            section,
            key,
            fallbackText,
            buffer,
            sizeof(buffer),
            g_iniPath.c_str());

        std::string value = ToLowerAscii(Trim(buffer));

        if (value == "1" || value == "true" ||
            value == "yes" || value == "on")
            return true;

        if (value == "0" || value == "false" ||
            value == "no" || value == "off")
            return false;

        return fallback;
    }

    void ParseAnimationList(
        const char* section,
        const char* key,
        std::set<std::string>& output)
    {
        output.clear();

        char buffer[4096] = { 0 };
        GetPrivateProfileStringA(
            section, key, "", buffer, sizeof(buffer), g_iniPath.c_str());

        std::string raw(buffer);
        std::string token;
        for (size_t i = 0; i <= raw.size(); ++i)
        {
            const bool separator =
                i == raw.size() || raw[i] == ',' || raw[i] == ';' || raw[i] == '|';

            if (!separator)
            {
                token.push_back(raw[i]);
                continue;
            }

            const std::string cleaned = Trim(token);
            if (!cleaned.empty())
                output.insert(ToLowerAscii(cleaned));
            token.clear();
        }
    }

    bool ContainsName(
        const std::set<std::string>& values,
        const std::string& animation)
    {
        return values.find(ToLowerAscii(Trim(animation))) != values.end();
    }

    bool IsImplicitlyBlockedAnimation(const std::string& animation)
    {
        const std::string lowered = ToLowerAscii(animation);

        // These records may be marked isDodge=true by animation packs, but are
        // not evasive movement techniques and should not be selected as an
        // armed dodge.
        return lowered.find("taunt") != std::string::npos ||
               lowered.find("battlecry") != std::string::npos ||
               lowered.find("battle cry") != std::string::npos;
    }

    void LoadSettings()
    {
        g_iniPath = GetDirectory(GetThisModulePath()) + "\\WeaponDodgeGuard.ini";
        g_settings.mode = GetPrivateProfileIntA("General", "Mode", MODE_OBSERVE_ONLY, g_iniPath.c_str());
        g_settings.verbose = GetPrivateProfileIntA("Logging", "Verbose", 0, g_iniPath.c_str()) != 0;
        g_settings.logSelections = GetPrivateProfileIntA("Logging", "LogSelections", 1, g_iniPath.c_str()) != 0;
        g_settings.maxLogLines = GetPrivateProfileIntA("Logging", "MaxLines", 2000, g_iniPath.c_str());
        g_settings.dodgeChanceScale = ReadFloat("Dodge", "ChanceScale", 1.0f);
        g_settings.minimumDodgeChance = ReadFloat("Dodge", "MinimumChance", 0.0f);
        g_settings.maximumDodgeChance = ReadFloat("Dodge", "MaximumChance", 95.0f);
        g_settings.allowInDefensiveMode =
            GetPrivateProfileIntA("Restrictions", "AllowInDefensiveMode", 1, g_iniPath.c_str()) != 0;
        g_settings.requireNormalProneState =
            GetPrivateProfileIntA("Restrictions", "RequireNormalProneState", 1, g_iniPath.c_str()) != 0;
        int cooldown = GetPrivateProfileIntA("Restrictions", "DodgeCooldownMs", 0, g_iniPath.c_str());
        g_settings.animationMode =
            GetPrivateProfileIntA("Animations", "AnimationMode", ANIMATION_VANILLA_ONLY, g_iniPath.c_str());
        g_settings.logDiscoveredTechniques =
            GetPrivateProfileIntA("Animations", "LogDiscoveredTechniques", 0, g_iniPath.c_str()) != 0;
        g_settings.logSelectedAnimation =
            GetPrivateProfileIntA("Animations", "LogSelectedAnimation", 0, g_iniPath.c_str()) != 0;
        // Adaptive Priority is opt-in. Missing settings preserve the
        // pre-v1.2 Weapon Dodge & Guard behavior.
        //
        // Official key:
        //   AdaptivePriority=0 / 1 / false / true
        //
        // The old QA key "Enabled" is accepted as a compatibility fallback.
        char adaptivePriorityBuffer[64] = { 0 };
        GetPrivateProfileStringA(
            "Priority",
            "AdaptivePriority",
            "",
            adaptivePriorityBuffer,
            sizeof(adaptivePriorityBuffer),
            g_iniPath.c_str());

        if (adaptivePriorityBuffer[0] != '\0')
            g_settings.adaptivePriorityEnabled =
                ReadBool("Priority", "AdaptivePriority", false);
        else
            g_settings.adaptivePriorityEnabled =
                ReadBool("Priority", "Enabled", false);

        g_settings.logPriorityDecisions =
            ReadBool("Priority", "LogDecisions", false);
        ParseAnimationList(
            "Animations", "AllowedAnimations", g_settings.allowedAnimations);
        ParseAnimationList(
            "Animations", "BlockedAnimations", g_settings.blockedAnimations);

        if (g_settings.maxLogLines < 100) g_settings.maxLogLines = 100;
        g_settings.dodgeChanceScale = Clamp(g_settings.dodgeChanceScale, 0.0f, 10.0f);
        g_settings.minimumDodgeChance = Clamp(g_settings.minimumDodgeChance, 0.0f, 100.0f);
        g_settings.maximumDodgeChance = Clamp(g_settings.maximumDodgeChance, g_settings.minimumDodgeChance, 100.0f);
        if (cooldown < 0) cooldown = 0;
        if (cooldown > 10000) cooldown = 10000;
        g_settings.dodgeCooldownMs = static_cast<DWORD>(cooldown);
        if (g_settings.animationMode != ANIMATION_VANILLA_ONLY &&
            g_settings.animationMode != ANIMATION_COMPATIBLE &&
            g_settings.animationMode != ANIMATION_CUSTOM_ALLOWLIST)
            g_settings.animationMode = ANIMATION_VANILLA_ONLY;

        std::ostringstream out;
        out << "Settings loaded: Mode=" << g_settings.mode
            << ", Verbose=" << BoolText(g_settings.verbose)
            << ", LogSelections=" << BoolText(g_settings.logSelections)
            << ", ChanceScale=" << g_settings.dodgeChanceScale
            << ", MinChance=" << g_settings.minimumDodgeChance
            << ", MaxChance=" << g_settings.maximumDodgeChance
            << ", AllowInDefensiveMode=" << BoolText(g_settings.allowInDefensiveMode)
            << ", RequireNormalProneState=" << BoolText(g_settings.requireNormalProneState)
            << ", DodgeCooldownMs=" << g_settings.dodgeCooldownMs
            << ", AnimationMode=" << g_settings.animationMode
            << ", LogDiscoveredTechniques=" << BoolText(g_settings.logDiscoveredTechniques)
            << ", LogSelectedAnimation=" << BoolText(g_settings.logSelectedAnimation)
            << ", AdaptivePriority=" << BoolText(g_settings.adaptivePriorityEnabled)
            << ", LogPriorityDecisions=" << BoolText(g_settings.logPriorityDecisions)
            << ", AllowedAnimations=" << g_settings.allowedAnimations.size()
            << ", BlockedAnimations=" << g_settings.blockedAnimations.size()
            << ", MaxLines=" << g_settings.maxLogLines;
        Log(out.str());
    }

    bool IsCanonicalStandingDodge(const CombatTechniqueData* technique)
    {
        return technique &&
               technique->isDodge &&
               !technique->stumbleDodge &&
               !technique->isProne &&
               technique->animation == "dodgeback";
    }


    bool IsNormalStandingDodge(const CombatTechniqueData* technique)
    {
        return technique &&
               technique->isDodge &&
               !technique->stumbleDodge &&
               !technique->isProne &&
               !technique->animation.empty();
    }

    bool ContainsTechniquePointer(const std::vector<CombatTechniqueData*>& values,
                                  CombatTechniqueData* technique)
    {
        return std::find(values.begin(), values.end(), technique) != values.end();
    }

    void RegisterNormalStandingDodge(CombatTechniqueData* technique, const char* source)
    {
        if (!IsNormalStandingDodge(technique)) return;

        if (IsCanonicalStandingDodge(technique))
            g_canonicalStandingDodge = technique;

        if (ContainsTechniquePointer(g_normalStandingDodges, technique))
            return;

        g_normalStandingDodges.push_back(technique);

        if (g_settings.logDiscoveredTechniques || g_settings.verbose)
        {
            std::ostringstream out;
            out << "Discovered normal standing dodge"
                << " source=" << (source ? source : "unknown")
                << " index=" << (g_normalStandingDodges.size() - 1)
                << " {" << TechniqueDescription(technique) << "}";
            Log(out.str());
        }
    }



    bool IsReadableMemory(const void* address, size_t bytes)
    {
        if (!address || bytes == 0) return false;
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(address, &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        const uintptr_t start = reinterpret_cast<uintptr_t>(address);
        const uintptr_t end = start + bytes;
        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        return end >= start && end <= regionEnd;
    }

    bool LooksLikeTechnique(const CombatTechniqueData* technique)
    {
        if (!IsReadableMemory(technique, sizeof(CombatTechniqueData))) return false;
        __try
        {
            const std::string& name = technique->animation;
            if (name.size() == 0 || name.size() > 128) return false;
            const char* text = name.c_str();
            return text && IsReadableMemory(text, name.size() + 1);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct TechniqueListScanResult
    {
        unsigned int plausibleCount;
        unsigned int normalDodgeCount;
        CombatTechniqueData* canonical;
        CombatTechniqueData* normalDodges[512];
    };

    bool ScanTechniqueListSEH(
        lektor<CombatTechniqueData*>* list,
        TechniqueListScanResult* result)
    {
        if (!result) return false;
        memset(result, 0, sizeof(*result));

        if (!IsReadableMemory(list, sizeof(*list))) return false;

        // This function intentionally contains no STL containers or other
        // local objects that require C++ stack unwinding. Visual C++ 2010
        // does not permit __try in such functions.
        __try
        {
            const unsigned int count = list->size();
            const unsigned int capacity = list->capacity();

            if (count == 0 || count > 512 ||
                capacity < count || capacity > 4096)
                return false;

            if (!list->valid() ||
                !IsReadableMemory(
                    list->begin(),
                    sizeof(CombatTechniqueData*) * count))
                return false;

            for (unsigned int i = 0; i < count; ++i)
            {
                CombatTechniqueData* technique = (*list)[i];
                if (!LooksLikeTechnique(technique))
                    continue;

                ++result->plausibleCount;

                if (!IsNormalStandingDodge(technique))
                    continue;

                if (result->normalDodgeCount < 512)
                    result->normalDodges[result->normalDodgeCount++] = technique;

                if (IsCanonicalStandingDodge(technique))
                    result->canonical = technique;
            }

            return result->plausibleCount >= 3 &&
                   result->canonical != NULL;
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            memset(result, 0, sizeof(*result));
            return false;
        }
    }

    bool CollectNormalDodgesFromList(lektor<CombatTechniqueData*>* list)
    {
        TechniqueListScanResult result;
        if (!ScanTechniqueListSEH(list, &result))
            return false;

        // STL mutation and logging happen outside the SEH function.
        g_discoveredTechniqueListAddress =
            reinterpret_cast<uintptr_t>(list);

        for (unsigned int i = 0; i < result.normalDodgeCount; ++i)
            RegisterNormalStandingDodge(
                result.normalDodges[i], "auto-list");

        return g_canonicalStandingDodge != NULL;
    }

    bool TryCandidateAddress(uintptr_t candidate, std::set<uintptr_t>& tested)
    {
        // Machine code may reference the object itself or one of its fields.
        const size_t offsets[] = { 0, 8, 12, 16, 24 };
        for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
        {
            if (candidate < offsets[i]) continue;
            const uintptr_t base = candidate - offsets[i];
            if (!tested.insert(base).second) continue;
            if (CollectNormalDodgesFromList(
                    reinterpret_cast<lektor<CombatTechniqueData*>*>(base)))
            {
                std::ostringstream out;
                out << "Auto-discovered combat technique list from chooseBlock references:"
                    << " normalDodges=" << g_normalStandingDodges.size()
                    << " canonical={" << TechniqueDescription(g_canonicalStandingDodge) << "}"
                    << " list=" << reinterpret_cast<void*>(base);
                Log(out.str());
                return true;
            }
        }
        return false;
    }

    bool AutoDiscoverStandingDodge()
    {
        if (IsCanonicalStandingDodge(g_canonicalStandingDodge)) return true;
        if (!g_chooseBlockTarget || !IsReadableMemory(reinterpret_cast<void*>(g_chooseBlockTarget), 0x600)) return false;

        const unsigned char* code = reinterpret_cast<const unsigned char*>(g_chooseBlockTarget);
        std::set<uintptr_t> tested;

        // Scan common x64 RIP-relative MOV/LEA forms. The ModRM form mod=00,r/m=101
        // carries a signed disp32 relative to the next instruction.
        for (size_t i = 0; i + 7 <= 0x600; ++i)
        {
            size_t opcodePos = i;
            if ((code[opcodePos] & 0xF0) == 0x40) ++opcodePos; // optional REX
            if (opcodePos + 6 > 0x600) break;
            const unsigned char opcode = code[opcodePos];
            if (opcode != 0x8B && opcode != 0x8D && opcode != 0x89 && opcode != 0x3B && opcode != 0x39)
                continue;
            const unsigned char modrm = code[opcodePos + 1];
            if ((modrm & 0xC7) != 0x05) continue;

            int displacement = 0;
            memcpy(&displacement, code + opcodePos + 2, sizeof(displacement));
            const uintptr_t next = reinterpret_cast<uintptr_t>(code + opcodePos + 6);
            const uintptr_t target = static_cast<uintptr_t>(static_cast<intptr_t>(next) + displacement);
            if (TryCandidateAddress(target, tested)) return true;

            // Some instructions reference a global pointer rather than the object directly.
            if (IsReadableMemory(reinterpret_cast<void*>(target), sizeof(void*)))
            {
                uintptr_t indirect = 0;
                memcpy(&indirect, reinterpret_cast<void*>(target), sizeof(indirect));
                if (indirect && TryCandidateAddress(indirect, tested)) return true;
            }
        }
        return false;
    }

    void ObserveStandingDodge(CombatTechniqueData* technique)
    {
        if (!IsNormalStandingDodge(technique)) return;
        RegisterNormalStandingDodge(technique, "vanilla-runtime");
    }

    bool IsCooldownReady(Character* character, DWORD now)
    {
        if (!character || g_settings.dodgeCooldownMs == 0) return true;
        std::map<Character*, DWORD>::iterator it = g_lastDodgeTime.find(character);
        if (it == g_lastDodgeTime.end()) return true;
        return static_cast<DWORD>(now - it->second) >= g_settings.dodgeCooldownMs;
    }

    bool IsAnimationAllowedByConfiguration(
        const CombatTechniqueData* technique)
    {
        if (!technique) return false;
        const std::string animation = technique->animation;

        if (IsImplicitlyBlockedAnimation(animation))
            return false;

        if (ContainsName(g_settings.blockedAnimations, animation))
            return false;

        if (g_settings.animationMode == ANIMATION_CUSTOM_ALLOWLIST)
        {
            if (g_settings.allowedAnimations.empty())
                return false;
            return ContainsName(g_settings.allowedAnimations, animation);
        }

        if (!g_settings.allowedAnimations.empty() &&
            !ContainsName(g_settings.allowedAnimations, animation))
            return false;

        return true;
    }

    bool TechniqueMatchesCharacter(const CombatTechniqueData* technique, CharStats* stats)
    {
        if (!technique || !stats || !IsNormalStandingDodge(technique))
            return false;

        if (!IsAnimationAllowedByConfiguration(technique))
            return false;

        const float dodgeSkill = stats->getDodge(true);
        if (dodgeSkill < technique->minSkill || dodgeSkill > technique->maxSkill)
            return false;

        // maxEncumbrance is deliberately not enforced.
        // Runtime observation confirmed that it is not directly comparable to
        // CharStats::getDodgePenalty_encumbrance(). Its actual Kenshi-side
        // semantics remain unverified, so applying a guessed threshold would
        // reduce compatibility with animation packs.
        return true;
    }

    CombatTechniqueData* SelectVanillaStandingDodge(CharStats* stats)
    {
        if (!g_canonicalStandingDodge || !stats ||
            !IsCanonicalStandingDodge(g_canonicalStandingDodge))
            return NULL;

        const float dodgeSkill = stats->getDodge(true);
        if (dodgeSkill < g_canonicalStandingDodge->minSkill ||
            dodgeSkill > g_canonicalStandingDodge->maxSkill)
            return NULL;

        return g_canonicalStandingDodge;
    }

    CombatTechniqueData* SelectCompatibleStandingDodge(CharStats* stats)
    {
        if (!stats) return NULL;

        std::vector<CombatTechniqueData*> candidates;
        std::vector<float> weights;
        float totalWeight = 0.0f;

        for (size_t i = 0; i < g_normalStandingDodges.size(); ++i)
        {
            CombatTechniqueData* technique = g_normalStandingDodges[i];
            if (!TechniqueMatchesCharacter(technique, stats))
                continue;

            float weight = technique->chanceMult;
            if (weight <= 0.0f)
                continue;

            candidates.push_back(technique);
            weights.push_back(weight);
            totalWeight += weight;
        }

        if (candidates.empty() || totalWeight <= 0.0f)
            return SelectVanillaStandingDodge(stats);

        const float roll =
            (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * totalWeight;
        float cursor = 0.0f;
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            cursor += weights[i];
            if (roll <= cursor)
                return candidates[i];
        }

        return candidates.back();
    }

    CombatTechniqueData* SelectStandingDodge(CharStats* stats)
    {
        if (g_settings.animationMode == ANIMATION_COMPATIBLE ||
            g_settings.animationMode == ANIMATION_CUSTOM_ALLOWLIST)
            return SelectCompatibleStandingDodge(stats);
        return SelectVanillaStandingDodge(stats);
    }

    bool IsEligibleForAddedDodge(CharStats* stats, CombatTechniqueData* original, const char*& reason)
    {
        if (!stats || !stats->me)
        {
            reason = "missing-character";
            return false;
        }

        if (stats->isUnarmed())
        {
            reason = "unarmed-vanilla-controls";
            return false;
        }

        // Critical stability rule: only replace a real vanilla weapon-block
        // opportunity. Never create a dodge when vanilla returned null or a
        // non-block technique.
        if (!original)
        {
            reason = "vanilla-returned-null";
            return false;
        }

        if (original->isDodge)
        {
            reason = "vanilla-dodge-preserved";
            return false;
        }

        if (!original->isBlock || original->isProne)
        {
            reason = "not-standing-block";
            return false;
        }

        if (g_settings.requireNormalProneState &&
            stats->me->_currentProneState != PS_NORMAL)
        {
            reason = "character-not-standing";
            return false;
        }

        if (!g_settings.allowInDefensiveMode && stats->isDefensiveMode())
        {
            reason = "defensive-mode-block-only";
            return false;
        }

        if (!IsCanonicalStandingDodge(g_canonicalStandingDodge))
        {
            reason = "standing-dodge-not-discovered";
            return false;
        }

        if (g_settings.animationMode == ANIMATION_COMPATIBLE &&
            g_normalStandingDodges.empty())
        {
            reason = "compatible-dodge-list-empty";
            return false;
        }

        reason = "eligible";
        return true;
    }

    void LogDecision(
        CharStats* stats,
        Character* opponent,
        float opponentAttackSkill,
        float vanillaChance,
        float adjustedChance,
        float roll,
        const char* decision,
        CombatTechniqueData* original,
        CombatTechniqueData* selected)
    {
        if (!stats) return;
        if (!g_settings.verbose && !g_settings.logSelections) return;
        if (!g_settings.verbose &&
            std::string(decision) != "dodge-selected" &&
            std::string(decision) != "vanilla-dodge-preserved")
            return;

        std::ostringstream out;
        out << std::fixed << std::setprecision(3)
            << "chooseBlock:"
            << " character=" << stats->me
            << " opponent=" << opponent
            << " armed=" << BoolText(!stats->isUnarmed())
            << " opponentAttack=" << opponentAttackSkill
            << " dodgeEffective=" << stats->getDodge(true)
            << " meleeDefenceEffective=" << stats->getMeleeDefence(true)
            << " dodgePenaltyGear=" << stats->getDodgePenalty_gear()
            << " dodgePenaltyEncumbrance=" << stats->getDodgePenalty_encumbrance()
            << " dodgePenaltyInjuries=" << stats->getDodgePenalty_injuries()
            << " vanillaDodgeChance=" << vanillaChance
            << " adjustedDodgeChance=" << adjustedChance
            << " roll=" << roll
            << " decision=" << decision;

        if (g_settings.verbose)
        {
            out << " original={" << TechniqueDescription(original) << "}"
                << " selected={" << TechniqueDescription(selected) << "}";
        }
        Log(out.str());
    }

    bool ShouldSuppressWeaponDodgeByPriority(
        CharStats* stats,
        float& dodgeEffective,
        float& meleeDefenceEffective)
    {
        dodgeEffective = 0.0f;
        meleeDefenceEffective = 0.0f;

        if (!g_settings.adaptivePriorityEnabled || !stats)
            return false;

        dodgeEffective = stats->getDodge(true);
        meleeDefenceEffective = stats->getMeleeDefence(true);

        // Equal values preserve the traditional Weapon Dodge behavior.
        // Adaptive Priority suppresses only when effective Melee Defence is
        // strictly higher than effective Dodge.
        return dodgeEffective < meleeDefenceEffective;
    }

    CombatTechniqueData* __fastcall ChooseBlockHook(
        CharStats* thisptr,
        CutDirection dir,
        float opponentAttackSkill,
        CutOrigination from,
        Character* opponent)
    {
        CombatTechniqueData* original =
            g_chooseBlockOriginal(thisptr, dir, opponentAttackSkill, from, opponent);
        if (!IsCanonicalStandingDodge(g_canonicalStandingDodge) && !g_autoDiscoveryAttempted)
        {
            g_autoDiscoveryAttempted = true;
            if (!AutoDiscoverStandingDodge())
                Log("Automatic standing-dodge discovery did not find a validated technique; dynamic capture fallback remains active.");
        }
        ObserveStandingDodge(original);

        CombatTechniqueData* selected = original;
        float vanillaDodgeChance = 0.0f;
        float adjustedDodgeChance = 0.0f;
        float roll = -1.0f;
        const char* decision = "observe";

        if (original && original->isDodge)
        {
            decision = "vanilla-dodge-preserved";
            InterlockedIncrement(&g_vanillaDodgePreserved);
        }
        else if (g_settings.mode == MODE_DODGE_THEN_BLOCK)
        {
            const char* eligibilityReason = "unknown";
            if (IsEligibleForAddedDodge(thisptr, original, eligibilityReason))
            {
                float priorityDodge = 0.0f;
                float priorityDefence = 0.0f;

                if (ShouldSuppressWeaponDodgeByPriority(
                        thisptr, priorityDodge, priorityDefence))
                {
                    decision = "adaptive-priority-defense-higher";
                    InterlockedIncrement(&g_blockFallback);

                    if (g_settings.logPriorityDecisions)
                    {
                        std::ostringstream priorityLog;
                        priorityLog << std::fixed << std::setprecision(3)
                            << "Adaptive priority: character=" << thisptr->me
                            << " dodgeEffective=" << priorityDodge
                            << " meleeDefenceEffective=" << priorityDefence
                            << " priority=defence"
                            << " weaponDodge=suppressed";
                        Log(priorityLog.str());
                    }
                }
                else
                {
                    if (g_settings.logPriorityDecisions &&
                        g_settings.adaptivePriorityEnabled)
                    {
                        std::ostringstream priorityLog;
                        priorityLog << std::fixed << std::setprecision(3)
                            << "Adaptive priority: character=" << thisptr->me
                            << " dodgeEffective=" << priorityDodge
                            << " meleeDefenceEffective=" << priorityDefence
                            << " priority=dodge"
                            << " weaponDodge=enabled";
                        Log(priorityLog.str());
                    }

                    const DWORD now = GetTickCount();
                    if (!IsCooldownReady(thisptr->me, now))
                    {
                        decision = "dodge-cooldown-block-fallback";
                        InterlockedIncrement(&g_blockFallback);
                    }
                    else
                    {
                    vanillaDodgeChance =
                        Clamp(thisptr->calculateDodgeChance(opponentAttackSkill, false), 0.0f, 100.0f);
                    adjustedDodgeChance = Clamp(
                        vanillaDodgeChance * g_settings.dodgeChanceScale,
                        g_settings.minimumDodgeChance,
                        g_settings.maximumDodgeChance);
                    roll = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * 100.0f;

                    if (roll < adjustedDodgeChance)
                    {
                        CombatTechniqueData* dodge = SelectStandingDodge(thisptr);
                        if (dodge)
                        {
                            selected = dodge;
                            decision = "dodge-selected";
                            if (g_settings.dodgeCooldownMs > 0)
                                g_lastDodgeTime[thisptr->me] = now;
                            InterlockedIncrement(&g_dodgeSelected);

                            if (g_settings.logSelectedAnimation)
                            {
                                std::ostringstream selectedLog;
                                selectedLog
                                    << "Selected dodge animation=\""
                                    << dodge->animation
                                    << "\" chanceMult=" << dodge->chanceMult
                                    << " minSkill=" << dodge->minSkill
                                    << " maxSkill=" << dodge->maxSkill
                                    << " maxEncumbrance=" << dodge->maxEncumbrance
                                    << " effectiveDodge=" << thisptr->getDodge(true);
                                Log(selectedLog.str());
                            }
                        }
                        else
                        {
                            decision = "dodge-technique-invalid-block-fallback";
                            InterlockedIncrement(&g_blockFallback);
                        }
                    }
                    else
                    {
                        decision = "dodge-roll-failed-block-fallback";
                        InterlockedIncrement(&g_blockFallback);
                    }
                    }
                }
            }
            else
            {
                decision = eligibilityReason;
                InterlockedIncrement(&g_ineligible);
            }
        }

        LogDecision(
            thisptr, opponent, opponentAttackSkill,
            vanillaDodgeChance, adjustedDodgeChance, roll,
            decision, original, selected);

        return selected;
    }
}

__declspec(dllexport) void startPlugin()
{
    LoadSettings();
    srand(GetTickCount());

    Log("Weapon Dodge & Guard v1.2.0-rc1 starting. Animation compatibility modes are enabled; automatic technique-list discovery and runtime capture are active.");

    const intptr_t target = KenshiLib::GetRealAddress(&CharStats::chooseBlock);
    g_chooseBlockTarget = target;
    if (!target)
    {
        ErrorLog("[WeaponDodgeGuard] Could not resolve CharStats::chooseBlock.");
        return;
    }

    if (KenshiLib::SUCCESS != KenshiLib::AddHook(
            target, reinterpret_cast<void*>(&ChooseBlockHook), &g_chooseBlockOriginal))
    {
        ErrorLog("[WeaponDodgeGuard] Could not install CharStats::chooseBlock hook.");
        return;
    }

    Log("Weapon Dodge & Guard v1.2.0-rc1 loaded and chooseBlock hook installed.");
}
