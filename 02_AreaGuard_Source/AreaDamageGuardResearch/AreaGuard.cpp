#include <Windows.h>
#include <Debug.h>
#include <string>
#include <sstream>
#include <core/Functions.h>
#include <kenshi/Character.h>
#include <kenshi/combat/CombatClass.h>
#include <kenshi/combat/CombatTechniqueData.h>
#include <kenshi/Damages.h>

class CombatClassCorrelationAccess : public CombatClass
{
public:
    static intptr_t ResolveBlockHit()
    {
        return KenshiLib::GetRealAddress(
            &CombatClassCorrelationAccess::_blockHit);
    }

    static Character* Me(const CombatClass* value)
    {
        return reinterpret_cast<const CombatClassCorrelationAccess*>(value)
            ->me;
    }

    static CombatTechniqueData* CurrentTechnique(
        const CombatClass* value)
    {
        return reinterpret_cast<const CombatClassCorrelationAccess*>(value)
            ->currentTechnique;
    }

    static const lektor<hand>& AttackZone(
        const CombatClass* value)
    {
        return reinterpret_cast<const CombatClassCorrelationAccess*>(value)
            ->targetsInAttackZone;
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

    struct RecentBlock
    {
        ULONGLONG tick;
        Character* blocker;
        Character* attacker;
        CombatTechniqueData* attackerTechnique;
        CutDirection direction;
        float damageTotal;
        bool valid;

        RecentBlock()
            : tick(0),
              blocker(NULL),
              attacker(NULL),
              attackerTechnique(NULL),
              direction(static_cast<CutDirection>(0)),
              damageTotal(0.0f),
              valid(false)
        {
        }
    };


    struct ActiveMeleeHitContext
    {
        Character* victim;
        Character* attacker;
        CombatTechniqueData* attackTechnique;
        CutDirection direction;
        int comboID;
        ULONGLONG tick;
        bool active;

        ActiveMeleeHitContext()
            : victim(NULL),
              attacker(NULL),
              attackTechnique(NULL),
              direction(static_cast<CutDirection>(0)),
              comboID(0),
              tick(0),
              active(false)
        {
        }
    };

    ActiveMeleeHitContext g_activeHitContext;

    BlockHitFn g_originalBlockHit = NULL;
    HitByMeleeAttackFn g_originalHitByMeleeAttack = NULL;
    RecentBlock g_recentBlocks[128];
    LONG g_blockWriteIndex = 0;
    LONG g_blockCount = 0;
    LONG g_hitCount = 0;
    LONG g_suppressionCount = 0;

    enum ProtectionMode
    {
        ProtectionMode_All = 1,
        ProtectionMode_Friendly = 2,
        ProtectionMode_NonEnemy = 3
    };

    enum ProtectedReactionMode
    {
        ProtectedReactionMode_NoReaction = 1,
        ProtectedReactionMode_HitReaction = 2
    };

    struct AreaGuardConfig
    {
        bool enabled;
        ProtectionMode protectionMode;
        ProtectedReactionMode protectedReactionMode;
        bool debugLogging;
        bool factorInDisguises;

        AreaGuardConfig()
            : enabled(true),
              protectionMode(ProtectionMode_All),
              protectedReactionMode(ProtectedReactionMode_NoReaction),
              debugLogging(true),
              factorInDisguises(true)
        {
        }
    };

    AreaGuardConfig g_config;

    std::string GetPluginDirectory()
    {
        HMODULE module = NULL;
        char path[MAX_PATH] = { 0 };

        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetPluginDirectory),
            &module);

        if (module == NULL ||
            GetModuleFileNameA(module, path, MAX_PATH) == 0)
        {
            return ".";
        }

        std::string result(path);
        const std::string::size_type separator =
            result.find_last_of("\\/");

        if (separator == std::string::npos)
            return ".";

        return result.substr(0, separator);
    }

    std::string ReadIniString(
        const char* section,
        const char* key,
        const char* defaultValue,
        const std::string& iniPath)
    {
        char buffer[256] = { 0 };
        GetPrivateProfileStringA(
            section,
            key,
            defaultValue,
            buffer,
            sizeof(buffer),
            iniPath.c_str());
        return std::string(buffer);
    }

    bool ReadIniBool(
        const char* section,
        const char* key,
        bool defaultValue,
        const std::string& iniPath)
    {
        const std::string value =
            ReadIniString(
                section,
                key,
                defaultValue ? "true" : "false",
                iniPath);

        return _stricmp(value.c_str(), "true") == 0 ||
               _stricmp(value.c_str(), "yes") == 0 ||
               value == "1" ||
               _stricmp(value.c_str(), "on") == 0;
    }

    ProtectionMode ParseProtectionMode(const std::string& value)
    {
        // Recommended public values:
        // 1 = All, 2 = Friendly, 3 = NonEnemy.
        // Legacy text values remain accepted for compatibility.
        if (value == "1" ||
            _stricmp(value.c_str(), "All") == 0)
        {
            return ProtectionMode_All;
        }

        if (value == "2" ||
            _stricmp(value.c_str(), "Friendly") == 0)
        {
            return ProtectionMode_Friendly;
        }

        if (value == "3" ||
            _stricmp(value.c_str(), "NonEnemy") == 0)
        {
            return ProtectionMode_NonEnemy;
        }

        return ProtectionMode_All;
    }

    const char* ProtectionModeName(ProtectionMode mode)
    {
        switch (mode)
        {
        case ProtectionMode_Friendly:
            return "Friendly";
        case ProtectionMode_NonEnemy:
            return "NonEnemy";
        default:
            return "All";
        }
    }

    ProtectedReactionMode ParseProtectedReactionMode(
        const std::string& value)
    {
        if (value == "2" ||
            _stricmp(value.c_str(), "HitReaction") == 0)
        {
            return ProtectedReactionMode_HitReaction;
        }

        return ProtectedReactionMode_NoReaction;
    }

    const char* ProtectedReactionModeName(
        ProtectedReactionMode mode)
    {
        if (mode == ProtectedReactionMode_HitReaction)
            return "HitReaction";

        return "NoReaction";
    }

    void LoadConfiguration()
    {
        const std::string iniPath =
            GetPluginDirectory() + "\\AreaGuard.ini";

        g_config.enabled =
            ReadIniBool(
                "AreaGuard",
                "Enabled",
                true,
                iniPath);

        g_config.protectionMode =
            ParseProtectionMode(
                ReadIniString(
                    "AreaGuard",
                    "ProtectionMode",
                    "1",
                    iniPath));

        g_config.protectedReactionMode =
            ParseProtectedReactionMode(
                ReadIniString(
                    "AreaGuard",
                    "ProtectedReactionMode",
                    "1",
                    iniPath));

        g_config.debugLogging =
            ReadIniBool(
                "AreaGuard",
                "DebugLogging",
                true,
                iniPath);

        g_config.factorInDisguises =
            ReadIniBool(
                "AreaGuard",
                "FactorInDisguises",
                true,
                iniPath);


        std::ostringstream out;
        out << "INI loaded:"
            << " path=" << iniPath
            << " Enabled="
            << (g_config.enabled ? "true" : "false")
            << " ProtectionMode="
            << ProtectionModeName(g_config.protectionMode)
            << " ProtectedReactionMode="
            << ProtectedReactionModeName(
                g_config.protectedReactionMode)
            << " DebugLogging="
            << (g_config.debugLogging ? "true" : "false")
            << " FactorInDisguises="
            << (g_config.factorInDisguises ? "true" : "false");

        DebugLog(
            std::string("[AreaGuard] ") + out.str());
    }

    struct RelationObservation
    {
        bool blockerCombatAvailable;
        bool victimCombatAvailable;
        bool attackerCombatAvailable;
        bool blockerSaysVictimFightsAlly;
        bool victimSaysBlockerFightsAlly;
        bool attackerSaysVictimFightsAlly;
        bool attackerSaysBlockerFightsAlly;
        bool blockerIsAllyWithVictim;
        bool blockerIsEnemyWithVictim;
        bool victimIsAllyWithBlocker;
        bool victimIsEnemyWithBlocker;

        RelationObservation()
            : blockerCombatAvailable(false),
              victimCombatAvailable(false),
              attackerCombatAvailable(false),
              blockerSaysVictimFightsAlly(false),
              victimSaysBlockerFightsAlly(false),
              attackerSaysVictimFightsAlly(false),
              attackerSaysBlockerFightsAlly(false),
              blockerIsAllyWithVictim(false),
              blockerIsEnemyWithVictim(false),
              victimIsAllyWithBlocker(false),
              victimIsEnemyWithBlocker(false)
        {
        }
    };

    bool IsProtectionAllowedByMode(
        Character* blocker,
        Character* victim,
        bool& outIsAlly,
        bool& outIsEnemy)
    {
        outIsAlly = false;
        outIsEnemy = false;

        if (g_config.protectionMode == ProtectionMode_All)
            return true;

        if (blocker == NULL || victim == NULL)
            return false;

        outIsAlly =
            blocker->isAlly(
                victim,
                g_config.factorInDisguises);

        outIsEnemy =
            blocker->isEnemy(
                victim,
                g_config.factorInDisguises);

        if (g_config.protectionMode == ProtectionMode_Friendly)
            return outIsAlly;

        if (g_config.protectionMode == ProtectionMode_NonEnemy)
            return !outIsEnemy;

        return true;
    }

    RelationObservation ObserveRelations(
        Character* blocker,
        Character* victim,
        Character* attacker)
    {
        RelationObservation result;

        CombatClass* blockerCombat =
            blocker != NULL ? blocker->getCombatClass() : NULL;
        CombatClass* victimCombat =
            victim != NULL ? victim->getCombatClass() : NULL;
        CombatClass* attackerCombat =
            attacker != NULL ? attacker->getCombatClass() : NULL;

        result.blockerCombatAvailable = blockerCombat != NULL;
        result.victimCombatAvailable = victimCombat != NULL;
        result.attackerCombatAvailable = attackerCombat != NULL;

        if (blockerCombat != NULL && victim != NULL)
        {
            result.blockerSaysVictimFightsAlly =
                blockerCombat->isFightingAnAllyOfMine(victim);
        }

        if (victimCombat != NULL && blocker != NULL)
        {
            result.victimSaysBlockerFightsAlly =
                victimCombat->isFightingAnAllyOfMine(blocker);
        }

        if (attackerCombat != NULL && victim != NULL)
        {
            result.attackerSaysVictimFightsAlly =
                attackerCombat->isFightingAnAllyOfMine(victim);
        }

        if (attackerCombat != NULL && blocker != NULL)
        {
            result.attackerSaysBlockerFightsAlly =
                attackerCombat->isFightingAnAllyOfMine(blocker);
        }

        if (blocker != NULL && victim != NULL)
        {
            result.blockerIsAllyWithVictim =
                blocker->isAlly(
                    victim,
                    g_config.factorInDisguises);

            result.blockerIsEnemyWithVictim =
                blocker->isEnemy(
                    victim,
                    g_config.factorInDisguises);

            result.victimIsAllyWithBlocker =
                victim->isAlly(
                    blocker,
                    g_config.factorInDisguises);

            result.victimIsEnemyWithBlocker =
                victim->isEnemy(
                    blocker,
                    g_config.factorInDisguises);
        }

        return result;
    }

    void LogInfo(const char* message)
    {
        DebugLog(std::string("[AreaGuard] ") + message);
    }

    Character* ValidateSourceAsCharacter(
        CombatClass* defenderCombat,
        RootObject* source)
    {
        if (defenderCombat == NULL || source == NULL)
            return NULL;

        const lektor<hand>& zone =
            CombatClassCorrelationAccess::AttackZone(defenderCombat);

        const uint32_t size = zone.size();
        const uint32_t scanCount = size < 12 ? size : 12;

        for (uint32_t i = 0; i < scanCount; ++i)
        {
            const hand& entry = zone[i];
            if (!entry.isValid())
                continue;

            Character* candidate = entry.getCharacter();
            if (reinterpret_cast<void*>(candidate) ==
                reinterpret_cast<void*>(source))
            {
                return candidate;
            }
        }

        return NULL;
    }

    void StoreRecentBlock(
        Character* blocker,
        Character* attacker,
        CombatTechniqueData* attackerTechnique,
        CutDirection direction,
        float damageTotal)
    {
        const LONG index =
            InterlockedIncrement(&g_blockWriteIndex) - 1;

        RecentBlock& record = g_recentBlocks[index % 128];
        record.tick = GetTickCount64();
        record.blocker = blocker;
        record.attacker = attacker;
        record.attackerTechnique = attackerTechnique;
        record.direction = direction;
        record.damageTotal = damageTotal;
        record.valid = true;
    }

    bool FindRecentBlock(
        Character* attacker,
        Character* victim,
        CombatTechniqueData* attack,
        CutDirection direction,
        RecentBlock& outRecord,
        ULONGLONG& outDeltaMs,
        bool& outSameAttacker,
        bool& outSameTechnique,
        bool& outSameDirection)
    {
        const ULONGLONG now = GetTickCount64();
        bool found = false;
        ULONGLONG bestDelta = ~static_cast<ULONGLONG>(0);

        for (int i = 0; i < 128; ++i)
        {
            const RecentBlock& record = g_recentBlocks[i];
            if (!record.valid)
                continue;

            if (record.blocker == victim)
                continue;

            const ULONGLONG delta =
                now >= record.tick ? now - record.tick : 0;

            if (delta > 750 || delta >= bestDelta)
                continue;

            const bool sameTechnique =
                record.attackerTechnique != NULL &&
                record.attackerTechnique == attack;

            const bool sameDirection =
                static_cast<int>(record.direction) ==
                static_cast<int>(direction);

            // Experiment004 deliberately allows attacker validation to fail.
            // Candidate correlation still requires same technique and direction.
            if (!sameTechnique || !sameDirection)
                continue;

            found = true;
            bestDelta = delta;
            outRecord = record;
        }

        if (!found)
            return false;

        outDeltaMs = bestDelta;
        outSameAttacker =
            outRecord.attacker != NULL &&
            outRecord.attacker == attacker;
        outSameTechnique =
            outRecord.attackerTechnique != NULL &&
            outRecord.attackerTechnique == attack;
        outSameDirection =
            static_cast<int>(outRecord.direction) ==
            static_cast<int>(direction);
        return true;
    }

    void __fastcall BlockHitHook(
        CombatClass* combat,
        CutDirection direction,
        const Damages& damage,
        RootObject* source)
    {
        const LONG count = InterlockedIncrement(&g_blockCount);

        Character* blocker =
            CombatClassCorrelationAccess::Me(combat);

        Character* attacker =
            ValidateSourceAsCharacter(combat, source);

        CombatTechniqueData* attackerTechnique = NULL;
        bool usedActiveHitContext = false;
        int contextComboID = -1;
        ULONGLONG contextAgeMs = 0;

        // _blockHit is normally called from inside the defender's
        // hitByMeleeAttack invocation. Prefer that already-known context
        // over reconstructing the attacker through targetsInAttackZone.
        if (g_activeHitContext.active &&
            g_activeHitContext.victim == blocker)
        {
            const ULONGLONG now = GetTickCount64();
            contextAgeMs =
                now >= g_activeHitContext.tick
                    ? now - g_activeHitContext.tick
                    : 0;

            if (contextAgeMs <= 50)
            {
                attacker = g_activeHitContext.attacker;
                attackerTechnique =
                    g_activeHitContext.attackTechnique;
                contextComboID = g_activeHitContext.comboID;
                usedActiveHitContext = true;
            }
        }

        if (!usedActiveHitContext && attacker != NULL)
        {
            CombatClass* attackerCombat =
                attacker->getCombatClass();

            if (attackerCombat != NULL)
            {
                attackerTechnique =
                    CombatClassCorrelationAccess::CurrentTechnique(
                        attackerCombat);
            }
        }

        StoreRecentBlock(
            blocker,
            attacker,
            attackerTechnique,
            direction,
            damage.total());

        if (g_config.debugLogging)
        {
            std::ostringstream out;
            out << "Block event:"
                << " count=" << std::dec << count
                << " tick=" << GetTickCount64()
                << " blocker=0x" << std::hex << std::uppercase
                << reinterpret_cast<uintptr_t>(blocker)
                << " source=0x"
                << reinterpret_cast<uintptr_t>(source)
                << " attackerValidated=0x"
                << reinterpret_cast<uintptr_t>(attacker)
                << " attackerTechnique=0x"
                << reinterpret_cast<uintptr_t>(attackerTechnique)
                << " usedActiveHitContext=" << std::dec
                << (usedActiveHitContext ? "true" : "false")
                << " contextComboID=" << contextComboID
                << " contextAgeMs=" << contextAgeMs
                << " direction="
                << static_cast<int>(direction)
                << " damageTotal=" << damage.total()
                << " cut=" << damage.cut
                << " blunt=" << damage.blunt
                << " pierce=" << damage.pierce;

            DebugLog(
                std::string("[AreaGuard] ") + out.str());
        }

        g_originalBlockHit(
            combat,
            direction,
            damage,
            source);
    }

    HitMaterialType __fastcall HitByMeleeAttackHook(
        Character* victim,
        CutDirection direction,
        Damages& damage,
        Character* attacker,
        CombatTechniqueData* attack,
        int comboID)
    {
        const LONG count = InterlockedIncrement(&g_hitCount);

        RecentBlock recent;
        ULONGLONG deltaMs = 0;
        bool sameAttacker = false;
        bool sameTechnique = false;
        bool sameDirection = false;

        const bool recentBlockFound =
            FindRecentBlock(
                attacker,
                victim,
                attack,
                direction,
                recent,
                deltaMs,
                sameAttacker,
                sameTechnique,
                sameDirection);

        RelationObservation relation;
        if (recentBlockFound)
        {
            relation =
                ObserveRelations(
                    recent.blocker,
                    victim,
                    attacker);
        }

        const float originalCut = damage.cut;
        const float originalBlunt = damage.blunt;
        const float originalPierce = damage.pierce;
        const float originalExtraStun = damage.extraStun;
        const float originalBleedMult = damage.bleedMult;
        const float originalArmourPenetration =
            damage.armourPenetration;
        const float originalTotal = damage.total();

        // First intervention is intentionally strict:
        // same attacker, different victim, exact same tick,
        // same attack technique, and same direction.
        bool modeIsAlly = false;
        bool modeIsEnemy = false;

        const bool protectionAllowedByMode =
            recentBlockFound &&
            IsProtectionAllowedByMode(
                recent.blocker,
                victim,
                modeIsAlly,
                modeIsEnemy);

        const bool suppressDamage =
            g_config.enabled &&
            recentBlockFound &&
            deltaMs == 0 &&
            sameAttacker &&
            sameTechnique &&
            sameDirection &&
            protectionAllowedByMode;

        LONG suppressionIndex = 0;
        bool nativeCallSkipped = false;
        bool protectedHitReactionUsed = false;
        HitMaterialType result =
            static_cast<HitMaterialType>(0);

        if (suppressDamage)
        {
            suppressionIndex =
                InterlockedIncrement(&g_suppressionCount);

            if (g_config.protectedReactionMode ==
                ProtectedReactionMode_NoReaction)
            {
                nativeCallSkipped = true;
            }
            else
            {
                protectedHitReactionUsed = true;
                damage.multiply(0.0f);
            }
        }

        if (!nativeCallSkipped)
        {
            const ActiveMeleeHitContext previousContext =
                g_activeHitContext;

            g_activeHitContext.victim = victim;
            g_activeHitContext.attacker = attacker;
            g_activeHitContext.attackTechnique = attack;
            g_activeHitContext.direction = direction;
            g_activeHitContext.comboID = comboID;
            g_activeHitContext.tick = GetTickCount64();
            g_activeHitContext.active = true;

            result =
                g_originalHitByMeleeAttack(
                    victim,
                    direction,
                    damage,
                    attacker,
                    attack,
                    comboID);

            g_activeHitContext = previousContext;
        }

        const float nativeInputTotal =
            nativeCallSkipped ? 0.0f : damage.total();

        // Restore the caller-visible object after the native call.
        damage.cut = originalCut;
        damage.blunt = originalBlunt;
        damage.pierce = originalPierce;
        damage.extraStun = originalExtraStun;
        damage.bleedMult = originalBleedMult;
        damage.armourPenetration = originalArmourPenetration;

        if (g_config.debugLogging)
        {
            std::ostringstream out;
            out << "Melee hit validation:"
                << " count=" << std::dec << count
                << " tick=" << GetTickCount64()
                << " victim=0x" << std::hex << std::uppercase
                << reinterpret_cast<uintptr_t>(victim)
                << " attacker=0x"
                << reinterpret_cast<uintptr_t>(attacker)
                << " attackTechnique=0x"
                << reinterpret_cast<uintptr_t>(attack)
                << " comboID=" << std::dec << comboID
                << " direction=" << static_cast<int>(direction)
                << " originalDamageTotal=" << originalTotal
                << " originalCut=" << originalCut
                << " originalBlunt=" << originalBlunt
                << " originalPierce=" << originalPierce
                << " originalExtraStun=" << originalExtraStun
                << " originalBleedMult=" << originalBleedMult
                << " originalArmourPenetration="
                << originalArmourPenetration
                << " recentOtherBlock="
                << (recentBlockFound ? "true" : "false")
                << " configuredProtectionMode="
                << ProtectionModeName(g_config.protectionMode)
                << " protectionAllowedByMode="
                << (protectionAllowedByMode ? "true" : "false")
                << " modeIsAlly="
                << (modeIsAlly ? "true" : "false")
                << " modeIsEnemy="
                << (modeIsEnemy ? "true" : "false")
                << " suppressed="
                << (suppressDamage ? "true" : "false")
                << " suppressionIndex=" << suppressionIndex
                << " configuredProtectedReactionMode="
                << ProtectedReactionModeName(
                    g_config.protectedReactionMode)
                << " nativeCallSkipped="
                << (nativeCallSkipped ? "true" : "false")
                << " protectedHitReactionUsed="
                << (protectedHitReactionUsed ? "true" : "false")
                << " nativeInputTotal=" << nativeInputTotal
                << " nativeResult="
                << static_cast<int>(result);

            if (recentBlockFound)
            {
                out << " deltaMs=" << deltaMs
                    << " blocker=0x" << std::hex << std::uppercase
                    << reinterpret_cast<uintptr_t>(recent.blocker)
                    << " blockTechnique=0x"
                    << reinterpret_cast<uintptr_t>(
                        recent.attackerTechnique)
                    << " sameAttacker=" << std::dec
                    << (sameAttacker ? "true" : "false")
                    << " sameTechnique="
                    << (sameTechnique ? "true" : "false")
                    << " sameDirection="
                    << (sameDirection ? "true" : "false")
                    << " blockDamageTotal="
                    << recent.damageTotal
                    << " configuredProtectionMode="
                    << ProtectionModeName(g_config.protectionMode)
                    << " relationObservationOnly=false"
                    << " blockerCombatAvailable="
                    << (relation.blockerCombatAvailable ? "true" : "false")
                    << " victimCombatAvailable="
                    << (relation.victimCombatAvailable ? "true" : "false")
                    << " attackerCombatAvailable="
                    << (relation.attackerCombatAvailable ? "true" : "false")
                    << " blockerSaysVictimFightsAlly="
                    << (relation.blockerSaysVictimFightsAlly
                        ? "true" : "false")
                    << " victimSaysBlockerFightsAlly="
                    << (relation.victimSaysBlockerFightsAlly
                        ? "true" : "false")
                    << " attackerSaysVictimFightsAlly="
                    << (relation.attackerSaysVictimFightsAlly
                        ? "true" : "false")
                    << " attackerSaysBlockerFightsAlly="
                    << (relation.attackerSaysBlockerFightsAlly
                        ? "true" : "false")
                    << " factorInDisguises="
                    << (g_config.factorInDisguises
                        ? "true" : "false")
                    << " blockerIsAllyWithVictim="
                    << (relation.blockerIsAllyWithVictim
                        ? "true" : "false")
                    << " blockerIsEnemyWithVictim="
                    << (relation.blockerIsEnemyWithVictim
                        ? "true" : "false")
                    << " victimIsAllyWithBlocker="
                    << (relation.victimIsAllyWithBlocker
                        ? "true" : "false")
                    << " victimIsEnemyWithBlocker="
                    << (relation.victimIsEnemyWithBlocker
                        ? "true" : "false");
            }

            out << " restoredDamageTotal=" << damage.total();

            DebugLog(
                std::string("[AreaGuard] ") + out.str());
        }

        return result;
    }
}

__declspec(dllexport) void startPlugin()
{
    LoadConfiguration();

    LogInfo(
        "Area Guard 1.0 RC1 starting.");
    LogInfo(
        "RE_Kenshi and KenshiLib integration initialized.");
    LogInfo(
        "ProtectionMode is active: 1=All, 2=Friendly, 3=NonEnemy; legacy text values remain supported.");

    const intptr_t blockHitAddress =
        CombatClassCorrelationAccess::ResolveBlockHit();

    const intptr_t meleeHitAddress =
        KenshiLib::GetRealAddress(
            &Character::_NV_hitByMeleeAttack);

    {
        std::ostringstream out;
        out << "Required address resolution:"
            << " blockHit=0x" << std::hex << std::uppercase
            << static_cast<uintptr_t>(blockHitAddress)
            << " meleeHit=0x"
            << static_cast<uintptr_t>(meleeHitAddress)
            << " resolved="
            << (blockHitAddress != 0 &&
                meleeHitAddress != 0
                ? "true" : "false");

        DebugLog(
            std::string("[AreaGuard] ") + out.str());
    }

    if (blockHitAddress == 0 || meleeHitAddress == 0)
    {
        LogInfo(
            "Experiment001 aborted: required address resolution failed.");
        return;
    }

    const KenshiLib::HookStatus blockHookResult =
        KenshiLib::AddHook(
            blockHitAddress,
            reinterpret_cast<void*>(&BlockHitHook),
            &g_originalBlockHit);

    const KenshiLib::HookStatus hitHookResult =
        KenshiLib::AddHook(
            meleeHitAddress,
            reinterpret_cast<void*>(&HitByMeleeAttackHook),
            &g_originalHitByMeleeAttack);

    {
        std::ostringstream out;
        out << "Hook installation:"
            << " blockResult=" << std::dec
            << static_cast<int>(blockHookResult)
            << " blockOriginal=0x" << std::hex << std::uppercase
            << reinterpret_cast<uintptr_t>(g_originalBlockHit)
            << " hitResult=" << std::dec
            << static_cast<int>(hitHookResult)
            << " hitOriginal=0x" << std::hex << std::uppercase
            << reinterpret_cast<uintptr_t>(
                g_originalHitByMeleeAttack);

        DebugLog(
            std::string("[AreaGuard] ") + out.str());
    }

    if (blockHookResult != KenshiLib::SUCCESS ||
        hitHookResult != KenshiLib::SUCCESS ||
        g_originalBlockHit == NULL ||
        g_originalHitByMeleeAttack == NULL)
    {
        LogInfo(
            "Experiment001 aborted: hook installation failed.");
        return;
    }

    LogInfo(
        "Protection modes initialized.");
    LogInfo(
        "Protected reaction modes initialized.");
    LogInfo(
        "Area Guard 1.0 RC1 loaded.");
}
