#include <Windows.h>
#include <Debug.h>
#include <string>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <core/Functions.h>
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/combat/CombatClass.h>
#include <kenshi/combat/CombatTechniqueData.h>
#include <kenshi/Damages.h>

// Guard Break & Bash v1.0.0
// KenshiLib is a hard requirement.
// Two independent effects are evaluated after a successful block:
// Bash staggers the attacker; GuardBreak staggers the blocker.
// Stat reference modes and probability curves are configurable through GuardBreakBash.ini.

class CombatClassAccess : public CombatClass
{
public:
    static intptr_t ResolveBlockHit()
    {
        return KenshiLib::GetRealAddress(&CombatClassAccess::_blockHit);
    }

    static Character* Me(const CombatClass* value)
    {
        return reinterpret_cast<const CombatClassAccess*>(value)->me;
    }

    static float PrepareNativeStumble(CombatClass* value, float requestedForce)
    {
        if (value == NULL)
            return 0.0f;

        CombatClassAccess* access = reinterpret_cast<CombatClassAccess*>(value);
        float force = requestedForce;
        if (force < 0.0f)
            force = 0.0f;

        float timer = 0.0f;
        if (access->stats != NULL)
            timer = access->stats->calculateStumbleBlockTimer(force);

        if (timer <= 0.0f)
            timer = 0.25f;

        access->isStumbleBlocking = true;
        access->stumbleForce = force;
        access->stumbleTimer = timer;
        access->whenCanStopStumble = timer;
        access->changeState(STUMBLE, timer);
        return timer;
    }
};

namespace
{
    typedef void (__fastcall* BlockHitFn)(
        CombatClass* combat,
        CutDirection direction,
        const Damages& damage,
        RootObject* source);

    typedef HitMaterialType (__fastcall* HitByMeleeAttackFn)(
        Character* victim,
        CutDirection direction,
        Damages& damage,
        Character* attacker,
        CombatTechniqueData* attack,
        int comboID);

    typedef void (__fastcall* StartStumbleFn)(
        Character* character,
        CutDirection direction,
        Damages& damage,
        GameData* bodyPart,
        Character* attacker);

    struct StatSnapshot
    {
        float strength;
        float dexterity;
        float toughness;
        bool valid;

        StatSnapshot()
            : strength(0.0f), dexterity(0.0f), toughness(0.0f), valid(false)
        {
        }
    };

    struct BlockEvent
    {
        Character* attacker;
        Character* defender;
        CutDirection direction;
        Damages damage;
        StatSnapshot attackerStats;
        StatSnapshot defenderStats;
        bool valid;

        BlockEvent()
            : attacker(NULL), defender(NULL),
              direction(static_cast<CutDirection>(0)), valid(false)
        {
        }
    };

    struct ActiveMeleeHitContext
    {
        Character* victim;
        Character* attacker;
        CombatTechniqueData* technique;
        CutDirection direction;
        int comboID;
        ULONGLONG tick;
        bool active;
        BlockEvent blockEvent;

        ActiveMeleeHitContext()
            : victim(NULL), attacker(NULL), technique(NULL),
              direction(static_cast<CutDirection>(0)), comboID(0),
              tick(0), active(false)
        {
        }
    };


    struct NativeStumbleTemplate
    {
        Damages damage;
        GameData* bodyPart;
        Character* attacker;
        CutDirection direction;
        ULONGLONG tick;
        unsigned int observations;
        bool valid;

        NativeStumbleTemplate()
            : bodyPart(NULL), attacker(NULL),
              direction(static_cast<CutDirection>(0)), tick(0),
              observations(0), valid(false)
        {
        }
    };

    enum ReferenceMode
    {
        REFERENCE_MAX_ALL = 0,
        REFERENCE_MAX_STRENGTH_DEXTERITY = 1,
        REFERENCE_WEIGHTED = 2
    };

    struct EffectSettings
    {
        bool enabled;
        float baseChance;
        float minimumChance;
        float maximumChance;
        float negativeMidDifference;
        float negativeMidChance;
        float negativeCapDifference;
        float positiveMidDifference;
        float positiveMidChance;
        float positiveCapDifference;
        int sourceReferenceMode;
        int targetReferenceMode;
        float sourceStrengthWeight;
        float sourceDexterityWeight;
        float sourceToughnessWeight;
        float targetStrengthWeight;
        float targetDexterityWeight;
        float targetToughnessWeight;
        int cooldownMs;
        float forceMultiplier;
        float minimumForce;

        EffectSettings()
            : enabled(true),
              baseChance(17.5f),
              minimumChance(2.5f), maximumChance(47.5f),
              negativeMidDifference(30.0f), negativeMidChance(7.5f),
              negativeCapDifference(60.0f),
              positiveMidDifference(30.0f), positiveMidChance(35.0f),
              positiveCapDifference(60.0f),
              sourceReferenceMode(REFERENCE_MAX_ALL),
              targetReferenceMode(REFERENCE_MAX_ALL),
              sourceStrengthWeight(1.0f), sourceDexterityWeight(0.5f),
              sourceToughnessWeight(0.0f),
              targetStrengthWeight(0.0f), targetDexterityWeight(0.5f),
              targetToughnessWeight(1.0f),
              cooldownMs(1500), forceMultiplier(1.0f), minimumForce(10.0f)
        {
        }
    };

    struct Settings
    {
        bool enabled;
        bool useBaseStats;
        bool allowBothEffects;
        int balancePreset;
        EffectSettings bash;
        EffectSettings guardBreak;
        bool ignoreAlreadyStumbling;
        int templateMaxAgeMs;
        int minimumTemplateObservations;
        bool logBlocks;
        bool logNativeTemplates;
        bool logStats;
        bool logRolls;
        bool logStagger;
        int maxLogLines;

        Settings()
            : enabled(true), useBaseStats(false), allowBothEffects(true),
              balancePreset(1),
              ignoreAlreadyStumbling(true), templateMaxAgeMs(10000),
              minimumTemplateObservations(1),
              logBlocks(true), logNativeTemplates(false), logStats(true),
              logRolls(true), logStagger(true), maxLogLines(3000)
        {
            // Bash: both sides use max(Strength, Dexterity, Toughness).
            bash.sourceReferenceMode = REFERENCE_MAX_ALL;
            bash.targetReferenceMode = REFERENCE_MAX_ALL;

            // GuardBreak attacker uses max(Strength, Dexterity).
            // GuardBreak blocker uses max(Strength, Dexterity, Toughness).
            guardBreak.sourceReferenceMode = REFERENCE_MAX_STRENGTH_DEXTERITY;
            guardBreak.targetReferenceMode = REFERENCE_MAX_ALL;
        }
    };

    Settings g_settings;
    ActiveMeleeHitContext g_context;
    BlockHitFn g_originalBlockHit = NULL;
    HitByMeleeAttackFn g_originalHitByMeleeAttack = NULL;
    StartStumbleFn g_originalStartStumble = NULL;
    LONG g_logLines = 0;
    LONG g_blockCount = 0;
    LONG g_bashCount = 0;
    LONG g_guardBreakCount = 0;
    LONG g_nativeStumbleCount = 0;
    std::string g_iniPath;
    bool g_applyingStagger = false;
    CRITICAL_SECTION g_templateLock;
    bool g_templateLockInitialised = false;
    std::unordered_map<Character*, NativeStumbleTemplate> g_nativeTemplates;
    std::unordered_map<Character*, ULONGLONG> g_lastBashTicks;
    std::unordered_map<Character*, ULONGLONG> g_lastGuardBreakTicks;
    NativeStumbleTemplate g_latestNativeTemplate;

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
            DebugLog(std::string("[GuardBreakBash] ") + message);
        else if (line == g_settings.maxLogLines + 1)
            DebugLog("[GuardBreakBash] Log limit reached; further lines suppressed.");
    }

    float ReadFloat(const char* section, const char* key, float defaultValue)
    {
        char defaultText[64] = { 0 };
        char valueText[64] = { 0 };
        sprintf_s(defaultText, sizeof(defaultText), "%.6f", defaultValue);
        GetPrivateProfileStringA(section, key, defaultText,
            valueText, sizeof(valueText), g_iniPath.c_str());
        return static_cast<float>(atof(valueText));
    }

    float Clamp(float value, float minimum, float maximum)
    {
        if (value < minimum)
            return minimum;
        if (value > maximum)
            return maximum;
        return value;
    }

    void LoadEffectSettings(
        const char* section, const char* sourceSection,
        const char* targetSection, EffectSettings& effect,
        const EffectSettings& defaults)
    {
        effect.enabled =
            GetPrivateProfileIntA(section, "Enabled", defaults.enabled ? 1 : 0,
                g_iniPath.c_str()) != 0;
        effect.baseChance = ReadFloat(section, "BaseChance", defaults.baseChance);
        effect.minimumChance = ReadFloat(
            section, "MinimumChance", defaults.minimumChance);
        effect.maximumChance = ReadFloat(
            section, "MaximumChance", defaults.maximumChance);
        effect.negativeMidDifference = ReadFloat(
            section, "NegativeMidDifference", defaults.negativeMidDifference);
        effect.negativeMidChance = ReadFloat(
            section, "NegativeMidChance", defaults.negativeMidChance);
        effect.negativeCapDifference = ReadFloat(
            section, "NegativeCapDifference", defaults.negativeCapDifference);
        effect.positiveMidDifference = ReadFloat(
            section, "PositiveMidDifference", defaults.positiveMidDifference);
        effect.positiveMidChance = ReadFloat(
            section, "PositiveMidChance", defaults.positiveMidChance);
        effect.positiveCapDifference = ReadFloat(
            section, "PositiveCapDifference", defaults.positiveCapDifference);
        effect.cooldownMs = GetPrivateProfileIntA(
            section, "CooldownMs", defaults.cooldownMs, g_iniPath.c_str());
        effect.forceMultiplier = ReadFloat(
            section, "ForceMultiplier", defaults.forceMultiplier);
        effect.minimumForce = ReadFloat(
            section, "MinimumForce", defaults.minimumForce);

        effect.sourceReferenceMode = GetPrivateProfileIntA(
            sourceSection, "ReferenceMode", defaults.sourceReferenceMode,
            g_iniPath.c_str());
        effect.targetReferenceMode = GetPrivateProfileIntA(
            targetSection, "ReferenceMode", defaults.targetReferenceMode,
            g_iniPath.c_str());

        if (effect.sourceReferenceMode < REFERENCE_MAX_ALL ||
            effect.sourceReferenceMode > REFERENCE_WEIGHTED)
            effect.sourceReferenceMode = defaults.sourceReferenceMode;
        if (effect.targetReferenceMode < REFERENCE_MAX_ALL ||
            effect.targetReferenceMode > REFERENCE_WEIGHTED)
            effect.targetReferenceMode = defaults.targetReferenceMode;

        effect.sourceStrengthWeight = ReadFloat(
            sourceSection, "StrengthWeight", defaults.sourceStrengthWeight);
        effect.sourceDexterityWeight = ReadFloat(
            sourceSection, "DexterityWeight", defaults.sourceDexterityWeight);
        effect.sourceToughnessWeight = ReadFloat(
            sourceSection, "ToughnessWeight", defaults.sourceToughnessWeight);

        effect.targetStrengthWeight = ReadFloat(
            targetSection, "StrengthWeight", defaults.targetStrengthWeight);
        effect.targetDexterityWeight = ReadFloat(
            targetSection, "DexterityWeight", defaults.targetDexterityWeight);
        effect.targetToughnessWeight = ReadFloat(
            targetSection, "ToughnessWeight", defaults.targetToughnessWeight);

        effect.minimumChance = Clamp(effect.minimumChance, 0.0f, 100.0f);
        effect.maximumChance = Clamp(effect.maximumChance, 0.0f, 100.0f);
        if (effect.maximumChance < effect.minimumChance)
            std::swap(effect.maximumChance, effect.minimumChance);
        if (effect.negativeMidDifference <= 0.0f)
            effect.negativeMidDifference = defaults.negativeMidDifference;
        if (effect.negativeCapDifference < effect.negativeMidDifference)
            effect.negativeCapDifference = effect.negativeMidDifference;
        if (effect.positiveMidDifference <= 0.0f)
            effect.positiveMidDifference = defaults.positiveMidDifference;
        if (effect.positiveCapDifference < effect.positiveMidDifference)
            effect.positiveCapDifference = effect.positiveMidDifference;

        effect.baseChance = Clamp(
            effect.baseChance, effect.minimumChance, effect.maximumChance);
        effect.negativeMidChance = Clamp(
            effect.negativeMidChance, effect.minimumChance, effect.baseChance);
        effect.positiveMidChance = Clamp(
            effect.positiveMidChance, effect.baseChance, effect.maximumChance);

        if (effect.cooldownMs < 0)
            effect.cooldownMs = 0;
        if (effect.forceMultiplier < 0.0f)
            effect.forceMultiplier = 0.0f;
        if (effect.minimumForce < 0.0f)
            effect.minimumForce = 0.0f;
    }

    void ApplyBalancePreset(int preset, EffectSettings& effect)
    {
        // All presets retain half-vanilla limits and quarter-vanilla
        // equal-skill chance. Only the shape and detection range change.
        effect.minimumChance = 2.5f;
        effect.baseChance = 17.5f;
        effect.maximumChance = 47.5f;

        switch (preset)
        {
        case 2: // Growth-focused: linear to the limits at +/-60.
            effect.negativeMidDifference = 30.0f;
            effect.negativeMidChance = 10.0f;
            effect.negativeCapDifference = 60.0f;
            effect.positiveMidDifference = 30.0f;
            effect.positiveMidChance = 32.5f;
            effect.positiveCapDifference = 60.0f;
            break;

        case 3: // Vanilla-shaped: reaches the limits at +/-30.
            effect.negativeMidDifference = 15.0f;
            effect.negativeMidChance = 10.0f;
            effect.negativeCapDifference = 30.0f;
            effect.positiveMidDifference = 15.0f;
            effect.positiveMidChance = 32.5f;
            effect.positiveCapDifference = 30.0f;
            break;

        case 1: // Recommended: strong early effect, slower final growth.
        default:
            effect.negativeMidDifference = 30.0f;
            effect.negativeMidChance = 7.5f;
            effect.negativeCapDifference = 60.0f;
            effect.positiveMidDifference = 30.0f;
            effect.positiveMidChance = 35.0f;
            effect.positiveCapDifference = 60.0f;
            break;
        }
    }

    void LoadSettings()
    {
        g_iniPath = GetDirectory(GetThisModulePath()) + "\\GuardBreakBash.ini";

        Settings defaults;
        g_settings = defaults;

        g_settings.enabled =
            GetPrivateProfileIntA("General", "Enabled", 1, g_iniPath.c_str()) != 0;
        g_settings.allowBothEffects =
            GetPrivateProfileIntA("General", "AllowBothEffects", 1,
                g_iniPath.c_str()) != 0;
        g_settings.balancePreset = GetPrivateProfileIntA(
            "General", "BalancePreset", 1, g_iniPath.c_str());
        if (g_settings.balancePreset < 0 || g_settings.balancePreset > 3)
            g_settings.balancePreset = 1;
        g_settings.useBaseStats =
            GetPrivateProfileIntA("Stats", "ValueMode", 1,
                g_iniPath.c_str()) == 0;

        LoadEffectSettings(
            "Bash", "Bash.BlockerForce", "Bash.AttackerResistance",
            g_settings.bash, defaults.bash);
        LoadEffectSettings(
            "GuardBreak", "GuardBreak.AttackerForce",
            "GuardBreak.BlockerResistance",
            g_settings.guardBreak, defaults.guardBreak);

        // Presets 1-3 override only chance-curve values.
        // Preset 0 leaves every custom INI curve value untouched.
        if (g_settings.balancePreset != 0)
        {
            ApplyBalancePreset(g_settings.balancePreset, g_settings.bash);
            ApplyBalancePreset(g_settings.balancePreset, g_settings.guardBreak);
        }

        g_settings.ignoreAlreadyStumbling = GetPrivateProfileIntA(
            "Eligibility", "IgnoreAlreadyStumbling", 1,
            g_iniPath.c_str()) != 0;
        g_settings.templateMaxAgeMs = GetPrivateProfileIntA(
            "Safety", "TemplateMaxAgeMs", 10000, g_iniPath.c_str());
        g_settings.minimumTemplateObservations = GetPrivateProfileIntA(
            "Safety", "MinimumTemplateObservations", 1, g_iniPath.c_str());

        g_settings.logBlocks = GetPrivateProfileIntA(
            "Logging", "LogBlocks", 1, g_iniPath.c_str()) != 0;
        g_settings.logNativeTemplates = GetPrivateProfileIntA(
            "Logging", "LogNativeTemplates", 0, g_iniPath.c_str()) != 0;
        g_settings.logStats = GetPrivateProfileIntA(
            "Logging", "LogStats", 1, g_iniPath.c_str()) != 0;
        g_settings.logRolls = GetPrivateProfileIntA(
            "Logging", "LogRolls", 1, g_iniPath.c_str()) != 0;
        g_settings.logStagger = GetPrivateProfileIntA(
            "Logging", "LogStagger", 1, g_iniPath.c_str()) != 0;
        g_settings.maxLogLines = GetPrivateProfileIntA(
            "Logging", "MaxLines", 3000, g_iniPath.c_str());

        if (g_settings.maxLogLines < 10)
            g_settings.maxLogLines = 10;
        if (g_settings.templateMaxAgeMs < 1)
            g_settings.templateMaxAgeMs = 1;
        if (g_settings.minimumTemplateObservations < 1)
            g_settings.minimumTemplateObservations = 1;
    }

    StatSnapshot ReadStats(Character* character)
    {
        StatSnapshot result;
        if (character == NULL)
            return result;

        CharStats* stats = character->getStats();
        if (stats == NULL)
            return result;

        result.strength = stats->getStat(STAT_STRENGTH, g_settings.useBaseStats);
        result.dexterity = stats->getStat(STAT_DEXTERITY, g_settings.useBaseStats);
        result.toughness = stats->getStat(STAT_TOUGHNESS, g_settings.useBaseStats);
        result.valid = true;
        return result;
    }

    float WeightedScore(const StatSnapshot& stats,
        float strengthWeight, float dexterityWeight, float toughnessWeight)
    {
        const float strength = std::max(0.0f, strengthWeight);
        const float dexterity = std::max(0.0f, dexterityWeight);
        const float toughness = std::max(0.0f, toughnessWeight);
        const float totalWeight = strength + dexterity + toughness;
        if (totalWeight <= 0.0001f)
            return 0.0f;

        return (stats.strength * strength +
                stats.dexterity * dexterity +
                stats.toughness * toughness) / totalWeight;
    }


    float HighestCombatStat(const StatSnapshot& stats, bool includeToughness)
    {
        float value = stats.strength;
        if (stats.dexterity > value)
            value = stats.dexterity;
        if (includeToughness && stats.toughness > value)
            value = stats.toughness;
        return value;
    }

    float ReferenceScore(
        const StatSnapshot& stats,
        int referenceMode,
        float strengthWeight,
        float dexterityWeight,
        float toughnessWeight)
    {
        if (referenceMode == REFERENCE_MAX_STRENGTH_DEXTERITY)
            return HighestCombatStat(stats, false);
        if (referenceMode == REFERENCE_WEIGHTED)
            return WeightedScore(
                stats, strengthWeight, dexterityWeight, toughnessWeight);
        return HighestCombatStat(stats, true);
    }

    bool IsEligible(Character* character)
    {
        if (character == NULL || character->isDead() || character->isUnconcious())
            return false;
        if (g_settings.ignoreAlreadyStumbling && character->stumbleState())
            return false;
        return true;
    }


    void CaptureNativeTemplate(
        Character* character, CutDirection direction, const Damages& damage,
        GameData* bodyPart, Character* attacker)
    {
        if (!g_templateLockInitialised || character == NULL || bodyPart == NULL)
            return;

        EnterCriticalSection(&g_templateLock);
        NativeStumbleTemplate& slot = g_nativeTemplates[character];
        slot.damage = damage;
        slot.bodyPart = bodyPart;
        slot.attacker = attacker;
        slot.direction = direction;
        slot.tick = GetTickCount64();
        slot.observations += 1;
        slot.valid = true;
        g_latestNativeTemplate = slot;
        LeaveCriticalSection(&g_templateLock);
    }

    bool ReadNativeTemplate(Character* character, NativeStumbleTemplate& result)
    {
        if (!g_templateLockInitialised || character == NULL)
            return false;

        bool found = false;
        EnterCriticalSection(&g_templateLock);
        const std::unordered_map<Character*, NativeStumbleTemplate>::const_iterator it =
            g_nativeTemplates.find(character);
        if (it != g_nativeTemplates.end() && it->second.valid)
        {
            result = it->second;
            found = true;
        }
        LeaveCriticalSection(&g_templateLock);
        return found;
    }

    bool ReadLatestNativeTemplate(NativeStumbleTemplate& result)
    {
        if (!g_templateLockInitialised)
            return false;

        bool found = false;
        EnterCriticalSection(&g_templateLock);
        if (g_latestNativeTemplate.valid && g_latestNativeTemplate.bodyPart != NULL)
        {
            result = g_latestNativeTemplate;
            found = true;
        }
        LeaveCriticalSection(&g_templateLock);
        return found;
    }

    bool IsOnCooldown(
        std::unordered_map<Character*, ULONGLONG>& cooldowns,
        Character* character, int cooldownMs,
        ULONGLONG now, ULONGLONG& remainingMs)
    {
        remainingMs = 0;
        if (!g_templateLockInitialised || character == NULL || cooldownMs <= 0)
            return false;

        bool coolingDown = false;
        EnterCriticalSection(&g_templateLock);
        const std::unordered_map<Character*, ULONGLONG>::const_iterator it =
            cooldowns.find(character);
        if (it != cooldowns.end() && now >= it->second)
        {
            const ULONGLONG elapsed = now - it->second;
            if (elapsed < static_cast<ULONGLONG>(cooldownMs))
            {
                remainingMs = static_cast<ULONGLONG>(cooldownMs) - elapsed;
                coolingDown = true;
            }
        }
        LeaveCriticalSection(&g_templateLock);
        return coolingDown;
    }

    void RecordCooldown(
        std::unordered_map<Character*, ULONGLONG>& cooldowns,
        Character* character, ULONGLONG now)
    {
        if (!g_templateLockInitialised || character == NULL)
            return;

        EnterCriticalSection(&g_templateLock);
        cooldowns[character] = now;
        LeaveCriticalSection(&g_templateLock);
    }

    float RollPercent()
    {
        return (static_cast<float>(rand()) /
            (static_cast<float>(RAND_MAX) + 1.0f)) * 100.0f;
    }

    float InterpolateChance(
        float x, float x0, float y0, float x1, float y1)
    {
        if (x1 <= x0)
            return y1;
        const float t = Clamp((x - x0) / (x1 - x0), 0.0f, 1.0f);
        return y0 + (y1 - y0) * t;
    }

    float CalculateCurveChance(
        const EffectSettings& effect,
        float difference,
        const char*& curveSegment)
    {
        if (difference >= 0.0f)
        {
            if (difference >= effect.positiveCapDifference)
            {
                curveSegment = "positive-cap";
                return effect.maximumChance;
            }
            if (difference <= effect.positiveMidDifference)
            {
                curveSegment = "positive-inner";
                return InterpolateChance(
                    difference, 0.0f, effect.baseChance,
                    effect.positiveMidDifference, effect.positiveMidChance);
            }

            curveSegment = "positive-outer";
            return InterpolateChance(
                difference,
                effect.positiveMidDifference, effect.positiveMidChance,
                effect.positiveCapDifference, effect.maximumChance);
        }

        const float disadvantage = -difference;
        if (disadvantage >= effect.negativeCapDifference)
        {
            curveSegment = "negative-cap";
            return effect.minimumChance;
        }
        if (disadvantage <= effect.negativeMidDifference)
        {
            curveSegment = "negative-inner";
            return InterpolateChance(
                disadvantage, 0.0f, effect.baseChance,
                effect.negativeMidDifference, effect.negativeMidChance);
        }

        curveSegment = "negative-outer";
        return InterpolateChance(
            disadvantage,
            effect.negativeMidDifference, effect.negativeMidChance,
            effect.negativeCapDifference, effect.minimumChance);
    }

    bool EvaluateEffect(
        const char* effectName,
        const EffectSettings& effect,
        const StatSnapshot& sourceStats,
        const StatSnapshot& targetStats,
        float& sourceScore,
        float& targetScore,
        float& difference,
        float& chance,
        float& roll)
    {
        sourceScore = ReferenceScore(
            sourceStats, effect.sourceReferenceMode,
            effect.sourceStrengthWeight,
            effect.sourceDexterityWeight,
            effect.sourceToughnessWeight);
        targetScore = ReferenceScore(
            targetStats, effect.targetReferenceMode,
            effect.targetStrengthWeight,
            effect.targetDexterityWeight,
            effect.targetToughnessWeight);
        difference = sourceScore - targetScore;
        const char* curveSegment = "unknown";
        chance = CalculateCurveChance(effect, difference, curveSegment);
        chance = Clamp(
            chance, effect.minimumChance, effect.maximumChance);
        roll = RollPercent();
        const bool success = roll < chance;

        if (g_settings.logStats || g_settings.logRolls)
        {
            std::ostringstream out;
            out << effectName
                << " evaluation: sourceMode=" << effect.sourceReferenceMode << " targetMode=" << effect.targetReferenceMode << " sourceScore=" << sourceScore
                << " targetScore=" << targetScore
                << " difference=" << difference
                << " preset=" << g_settings.balancePreset
                << " curveSegment=" << curveSegment
                << " chance=" << chance
                << " roll=" << roll
                << " success=" << (success ? "true" : "false");
            Log(out.str());
        }

        return success;
    }

    bool ApplyStumbleEffect(
        const char* effectName,
        Character* target,
        Character* reactionSource,
        CutDirection direction,
        const EffectSettings& effect,
        std::unordered_map<Character*, ULONGLONG>& cooldowns,
        LONG& effectCounter)
    {
        if (!IsEligible(target))
        {
            if (g_settings.logStagger)
            {
                Log(std::string(effectName) +
                    " skipped: target was not eligible for stagger.");
            }
            return false;
        }

        NativeStumbleTemplate nativeTemplate;
        bool usedGlobalFallback = false;
        bool hasTemplate = ReadNativeTemplate(target, nativeTemplate);

        const ULONGLONG now = GetTickCount64();

        // Prefer a valid same-target template. If it is stale or unusable,
        // retry with the newest global template instead of aborting immediately.
        bool sameTargetUsable = false;
        if (hasTemplate)
        {
            const ULONGLONG sameTargetAgeMs =
                now >= nativeTemplate.tick ? now - nativeTemplate.tick : 0;

            sameTargetUsable =
                nativeTemplate.bodyPart != NULL &&
                sameTargetAgeMs <= static_cast<ULONGLONG>(
                    g_settings.templateMaxAgeMs) &&
                nativeTemplate.observations >= static_cast<unsigned int>(
                    g_settings.minimumTemplateObservations);

            if (!sameTargetUsable && g_settings.logStagger)
            {
                std::ostringstream out;
                out << effectName
                    << " same-target template unusable; trying global fallback."
                    << " ageMs=" << sameTargetAgeMs
                    << " bodyPartNull="
                    << (nativeTemplate.bodyPart == NULL ? "true" : "false")
                    << " observations=" << nativeTemplate.observations;
                Log(out.str());
            }
        }

        if (!sameTargetUsable)
        {
            NativeStumbleTemplate globalTemplate;
            if (ReadLatestNativeTemplate(globalTemplate))
            {
                nativeTemplate = globalTemplate;
                hasTemplate = true;
                usedGlobalFallback = true;
            }
            else
            {
                hasTemplate = false;
            }
        }

        const ULONGLONG templateAgeMs =
            hasTemplate && now >= nativeTemplate.tick ? now - nativeTemplate.tick : 0;

        if (!hasTemplate)
        {
            Log(std::string(effectName) +
                " skipped: no native stumble template was available.");
            return false;
        }
        if (nativeTemplate.bodyPart == NULL)
        {
            Log(std::string(effectName) +
                " skipped: selected native bodyPart was null.");
            return false;
        }
        if (templateAgeMs > static_cast<ULONGLONG>(g_settings.templateMaxAgeMs))
        {
            std::ostringstream out;
            out << effectName
                << " skipped: selected native template expired. ageMs="
                << templateAgeMs
                << " maximumMs=" << g_settings.templateMaxAgeMs
                << " source="
                << (usedGlobalFallback ? "global-fallback" : "same-target");
            Log(out.str());
            return false;
        }
        if (nativeTemplate.observations <
            static_cast<unsigned int>(g_settings.minimumTemplateObservations))
        {
            std::ostringstream out;
            out << effectName
                << " skipped: selected template has insufficient observations. observations="
                << nativeTemplate.observations
                << " required=" << g_settings.minimumTemplateObservations
                << " source="
                << (usedGlobalFallback ? "global-fallback" : "same-target");
            Log(out.str());
            return false;
        }

        ULONGLONG cooldownRemainingMs = 0;
        if (IsOnCooldown(
                cooldowns, target, effect.cooldownMs,
                now, cooldownRemainingMs))
        {
            if (g_settings.logStagger)
            {
                std::ostringstream out;
                out << effectName
                    << " skipped: target cooldown active. remainingMs="
                    << cooldownRemainingMs;
                Log(out.str());
            }
            return false;
        }

        Damages replayDamage = nativeTemplate.damage;
        CombatClass* combat = target->getCombatClass();
        if (combat == NULL)
        {
            Log(std::string(effectName) +
                " skipped: target CombatClass was null.");
            return false;
        }

        float requestedForce =
            replayDamage.total() * effect.forceMultiplier;
        if (requestedForce < effect.minimumForce)
            requestedForce = effect.minimumForce;

        const LONG count = InterlockedIncrement(&effectCounter);

        if (g_settings.logStagger)
        {
            std::ostringstream out;
            out << effectName << " replay begin: count=" << count
                << " target=0x" << std::hex << std::uppercase
                << reinterpret_cast<uintptr_t>(target)
                << " reactionSource=0x"
                << reinterpret_cast<uintptr_t>(reactionSource)
                << " bodyPart=0x"
                << reinterpret_cast<uintptr_t>(nativeTemplate.bodyPart)
                << std::dec
                << " direction=" << static_cast<int>(direction)
                << " templateAgeMs=" << templateAgeMs
                << " templateSource="
                << (usedGlobalFallback ? "global-fallback" : "same-target")
                << " templateObservations=" << nativeTemplate.observations
                << " damageTotal=" << replayDamage.total();
            Log(out.str());
        }

        const float appliedTimer =
            CombatClassAccess::PrepareNativeStumble(combat, requestedForce);

        g_applyingStagger = true;
        g_originalStartStumble(
            target, direction, replayDamage,
            nativeTemplate.bodyPart, reactionSource);
        g_applyingStagger = false;

        RecordCooldown(cooldowns, target, now);

        if (g_settings.logStagger)
        {
            std::ostringstream out;
            out << effectName << " replay completed: count=" << count
                << " target=0x" << std::hex << std::uppercase
                << reinterpret_cast<uintptr_t>(target)
                << std::dec
                << " requestedForce=" << requestedForce
                << " appliedTimer=" << appliedTimer
                << " targetStumbleState="
                << (target->stumbleState() ? "true" : "false")
                << " combatState="
                << static_cast<int>(combat->getCombatState());
            Log(out.str());
        }

        return true;
    }

    void ResolveBlockEffects(const BlockEvent& event)
    {
        if (!event.valid || !g_settings.enabled)
            return;
        if (!event.attackerStats.valid || !event.defenderStats.valid)
        {
            Log("Block effects skipped: stat snapshot was unavailable.");
            return;
        }

        bool bashApplied = false;

        if (g_settings.bash.enabled)
        {
            float sourceScore = 0.0f;
            float targetScore = 0.0f;
            float difference = 0.0f;
            float chance = 0.0f;
            float roll = 0.0f;

            // Bash: blocker overpowers the attacker after a successful block.
            const bool success = EvaluateEffect(
                "Bash",
                g_settings.bash,
                event.defenderStats,
                event.attackerStats,
                sourceScore, targetScore, difference, chance, roll);

            if (success)
            {
                bashApplied = ApplyStumbleEffect(
                    "Bash",
                    event.attacker,
                    event.defender,
                    event.direction,
                    g_settings.bash,
                    g_lastBashTicks,
                    g_bashCount);
            }
        }

        if (g_settings.guardBreak.enabled &&
            (g_settings.allowBothEffects || !bashApplied))
        {
            float sourceScore = 0.0f;
            float targetScore = 0.0f;
            float difference = 0.0f;
            float chance = 0.0f;
            float roll = 0.0f;

            // Guard Break: attacker force overcomes the blocker.
            const bool success = EvaluateEffect(
                "GuardBreak",
                g_settings.guardBreak,
                event.attackerStats,
                event.defenderStats,
                sourceScore, targetScore, difference, chance, roll);

            if (success)
            {
                ApplyStumbleEffect(
                    "GuardBreak",
                    event.defender,
                    event.attacker,
                    event.direction,
                    g_settings.guardBreak,
                    g_lastGuardBreakTicks,
                    g_guardBreakCount);
            }
        }
    }

    void __fastcall StartStumbleHook(
        Character* character,
        CutDirection direction,
        Damages& damage,
        GameData* bodyPart,
        Character* attacker)
    {
        CaptureNativeTemplate(character, direction, damage, bodyPart, attacker);

        const LONG count = InterlockedIncrement(&g_nativeStumbleCount);
        if (g_settings.logNativeTemplates)
        {
            std::ostringstream out;
            out << "Native startStumble observed: count=" << count
                << " character=0x" << std::hex << std::uppercase
                << reinterpret_cast<uintptr_t>(character)
                << " attacker=0x" << reinterpret_cast<uintptr_t>(attacker)
                << " bodyPart=0x" << reinterpret_cast<uintptr_t>(bodyPart)
                << std::dec
                << " bodyPartNull=" << (bodyPart == NULL ? "true" : "false")
                << " direction=" << static_cast<int>(direction)
                << " cut=" << damage.cut
                << " blunt=" << damage.blunt
                << " pierce=" << damage.pierce
                << " extraStun=" << damage.extraStun
                << " total=" << damage.total();
            Log(out.str());
        }

        g_originalStartStumble(character, direction, damage, bodyPart, attacker);
    }

    void __fastcall BlockHitHook(
        CombatClass* combat,
        CutDirection direction,
        const Damages& damage,
        RootObject* source)
    {
        Character* blocker = CombatClassAccess::Me(combat);
        Character* attacker = NULL;
        bool correlated = false;
        ULONGLONG ageMs = 0;

        if (!g_applyingStagger && g_context.active && g_context.victim == blocker)
        {
            const ULONGLONG now = GetTickCount64();
            ageMs = now >= g_context.tick ? now - g_context.tick : 0;
            if (ageMs <= 50)
            {
                attacker = g_context.attacker;
                correlated = attacker != NULL;
            }
        }

        if (g_settings.enabled && g_settings.logBlocks)
        {
            const LONG count = InterlockedIncrement(&g_blockCount);
            std::ostringstream out;
            out << "Successful block detected: count=" << count
                << " blocker=0x" << std::hex << std::uppercase
                << reinterpret_cast<uintptr_t>(blocker)
                << " attacker=0x" << reinterpret_cast<uintptr_t>(attacker)
                << " source=0x" << reinterpret_cast<uintptr_t>(source)
                << std::dec
                << " correlated=" << (correlated ? "true" : "false")
                << " contextAgeMs=" << ageMs
                << " direction=" << static_cast<int>(direction)
                << " damageTotal=" << damage.total();
            Log(out.str());
        }

        if (correlated && blocker != attacker)
        {
            BlockEvent event;
            event.attacker = attacker;
            event.defender = blocker;
            event.direction = direction;
            event.damage = damage;
            event.attackerStats = ReadStats(attacker);
            event.defenderStats = ReadStats(blocker);
            event.valid = true;
            g_context.blockEvent = event;
        }

        g_originalBlockHit(combat, direction, damage, source);
    }

    HitMaterialType __fastcall HitByMeleeAttackHook(
        Character* victim,
        CutDirection direction,
        Damages& damage,
        Character* attacker,
        CombatTechniqueData* attack,
        int comboID)
    {
        if (g_applyingStagger)
            return g_originalHitByMeleeAttack(
                victim, direction, damage, attacker, attack, comboID);

        const ActiveMeleeHitContext previous = g_context;
        g_context = ActiveMeleeHitContext();
        g_context.victim = victim;
        g_context.attacker = attacker;
        g_context.technique = attack;
        g_context.direction = direction;
        g_context.comboID = comboID;
        g_context.tick = GetTickCount64();
        g_context.active = true;

        const HitMaterialType result = g_originalHitByMeleeAttack(
            victim, direction, damage, attacker, attack, comboID);

        const BlockEvent event = g_context.blockEvent;
        g_context = previous;

        // Resolve only after Kenshi's original melee-hit function has returned.
        // This avoids mutating the defender's state inside CombatClass::_blockHit.
        ResolveBlockEffects(event);
        return result;
    }
}

__declspec(dllexport) void startPlugin()
{
    LoadSettings();
    srand(static_cast<unsigned int>(GetTickCount()));

    Log("Guard Break & Bash v1.0.0 starting.");
    Log("KenshiLib is a hard requirement; Bash and GuardBreak are independently configurable.");

    if (!g_settings.enabled)
    {
        Log("Plugin disabled by GuardBreakBash.ini.");
        return;
    }

    InitializeCriticalSection(&g_templateLock);
    g_templateLockInitialised = true;

    const intptr_t blockAddress = CombatClassAccess::ResolveBlockHit();
    const intptr_t meleeAddress = KenshiLib::GetRealAddress(&Character::_NV_hitByMeleeAttack);
    const intptr_t stumbleAddress = KenshiLib::GetRealAddress(&Character::_startStumble);

    {
        std::ostringstream out;
        out << "Address resolution: blockHit=0x" << std::hex << std::uppercase
            << static_cast<uintptr_t>(blockAddress)
            << " meleeHit=0x" << static_cast<uintptr_t>(meleeAddress)
            << " startStumble=0x" << static_cast<uintptr_t>(stumbleAddress);
        Log(out.str());
    }

    if (blockAddress == 0 || meleeAddress == 0 || stumbleAddress == 0)
    {
        Log("Startup aborted: required KenshiLib address resolution failed.");
        return;
    }

    const KenshiLib::HookStatus blockResult = KenshiLib::AddHook(
        blockAddress, reinterpret_cast<void*>(&BlockHitHook), &g_originalBlockHit);
    const KenshiLib::HookStatus meleeResult = KenshiLib::AddHook(
        meleeAddress, reinterpret_cast<void*>(&HitByMeleeAttackHook),
        &g_originalHitByMeleeAttack);
    const KenshiLib::HookStatus stumbleResult = KenshiLib::AddHook(
        stumbleAddress, reinterpret_cast<void*>(&StartStumbleHook),
        &g_originalStartStumble);

    {
        std::ostringstream out;
        out << "Hook installation: blockResult=" << std::dec
            << static_cast<int>(blockResult)
            << " meleeResult=" << static_cast<int>(meleeResult)
            << " stumbleResult=" << static_cast<int>(stumbleResult)
            << " blockOriginal=0x" << std::hex << std::uppercase
            << reinterpret_cast<uintptr_t>(g_originalBlockHit)
            << " meleeOriginal=0x"
            << reinterpret_cast<uintptr_t>(g_originalHitByMeleeAttack)
            << " stumbleOriginal=0x"
            << reinterpret_cast<uintptr_t>(g_originalStartStumble);
        Log(out.str());
    }

    if (blockResult != KenshiLib::SUCCESS ||
        meleeResult != KenshiLib::SUCCESS ||
        stumbleResult != KenshiLib::SUCCESS ||
        g_originalBlockHit == NULL ||
        g_originalHitByMeleeAttack == NULL ||
        g_originalStartStumble == NULL)
    {
        Log("Startup aborted: hook installation failed.");
        return;
    }

    {
        std::ostringstream out;
        out << "Integrated settings: ValueMode="
            << (g_settings.useBaseStats ? "base" : "effective")
            << " AllowBothEffects="
            << (g_settings.allowBothEffects ? "true" : "false")
            << " BashEnabled=" << (g_settings.bash.enabled ? "true" : "false")
            << " BashBaseChance=" << g_settings.bash.baseChance
            << " BalancePreset=" << g_settings.balancePreset
            << " BashNegativeMidChance="
            << g_settings.bash.negativeMidChance
            << " BashPositiveMidChance="
            << g_settings.bash.positiveMidChance
            << " GuardBreakEnabled="
            << (g_settings.guardBreak.enabled ? "true" : "false")
            << " GuardBreakBaseChance="
            << g_settings.guardBreak.baseChance
            << " GuardBreakNegativeMidChance="
            << g_settings.guardBreak.negativeMidChance
            << " GuardBreakPositiveMidChance="
            << g_settings.guardBreak.positiveMidChance
            << " BashSourceMode=" << g_settings.bash.sourceReferenceMode
            << " BashTargetMode=" << g_settings.bash.targetReferenceMode
            << " GuardBreakSourceMode="
            << g_settings.guardBreak.sourceReferenceMode
            << " GuardBreakTargetMode="
            << g_settings.guardBreak.targetReferenceMode
            << " TemplateMaxAgeMs=" << g_settings.templateMaxAgeMs
            << " LogNativeTemplates="
            << (g_settings.logNativeTemplates ? "true" : "false");
        Log(out.str());
    }

    {
        std::ostringstream out;
        out << "Diagnostic structure sizes: sizeof(Damages)=" << sizeof(Damages)
            << " sizeof(GameData*)=" << sizeof(GameData*)
            << " sizeof(Character*)=" << sizeof(Character*);
        Log(out.str());
    }

    Log("Guard Break & Bash v1.0.0 loaded successfully.");
}
