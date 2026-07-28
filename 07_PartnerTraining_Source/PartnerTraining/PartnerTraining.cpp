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

// Minimal declaration for the KenshiLib-documented AI method.
// AI.h is intentionally not included because the current KenshiLib header set
// duplicates CharacterMessage when combined with Character.h.
class AI
{
public:
    AITaskSytem* getTaskSystem() const;
};

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <intrin.h>
#include <map>
#include <set>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <stdint.h>


#pragma intrinsic(_ReturnAddress)

namespace
{
    const char* const kPluginName = "PartnerTraining";
    const char* const kPluginVersion = "1.0.0-rc6";
    const unsigned int kSupportedConfigVersion = 1;
    const unsigned int kSupportedSourceDataVersion = 1;

    struct Config
    {
        unsigned int configVersion;
        bool enabled;
        bool loggingEnabled;
        bool detailedLogging;
        bool logLabouringXp;
        unsigned int summaryIntervalMs;
        unsigned int sessionGapMs;
        unsigned int maximumLogEntries;
        float globalXpMultiplier;
        float globalCapMultiplier;
        bool enableCaps;
        float defaultStrengthMultiplier;
        float defaultStrengthCap;
        float defaultToughnessMultiplier;
        float defaultToughnessCap;
        float defaultDexterityMultiplier;
        float defaultDexterityCap;
        bool generateUnknownSources;
        unsigned int xpSummaryIntervalMs;

        Config()
            : configVersion(1), enabled(true), loggingEnabled(true), detailedLogging(false),
              logLabouringXp(true), summaryIntervalMs(5000), sessionGapMs(1500),
              maximumLogEntries(500), globalXpMultiplier(1.0f),
              globalCapMultiplier(1.0f), enableCaps(true),
              defaultStrengthMultiplier(0.0f), defaultStrengthCap(0.0f),
              defaultToughnessMultiplier(0.0f), defaultToughnessCap(0.0f),
              defaultDexterityMultiplier(0.0f), defaultDexterityCap(0.0f),
              generateUnknownSources(true), xpSummaryIntervalMs(5000) {}
    };

    struct SessionKey
    {
        CharStats* stats;
        uintptr_t callerRva;

        SessionKey() : stats(NULL), callerRva(0) {}
        SessionKey(CharStats* s, uintptr_t rva) : stats(s), callerRva(rva) {}

        bool operator<(const SessionKey& other) const
        {
            if (stats != other.stats)
            {
                return stats < other.stats;
            }
            return callerRva < other.callerRva;
        }
    };

    struct SessionState
    {
        DWORD startTick;
        DWORD lastCallTick;
        DWORD lastSummaryTick;
        unsigned __int64 calls;

        SessionState()
            : startTick(0),
              lastCallTick(0),
              lastSummaryTick(0),
              calls(0)
        {
        }
    };

    enum PartnerSkillIndex
    {
        PARTNER_ATTACK = 0,
        PARTNER_DEFENCE,
        PARTNER_DODGE,
        PARTNER_MARTIAL_ARTS,
        PARTNER_KATANAS,
        PARTNER_SABRES,
        PARTNER_HACKERS,
        PARTNER_HEAVY_WEAPONS,
        PARTNER_BLUNT,
        PARTNER_POLEARMS,
        PARTNER_CROSSBOWS,
        PARTNER_PRECISION_SHOOTING,
        PARTNER_PERCEPTION,
        PARTNER_TURRETS,
        PARTNER_SKILL_COUNT
    };

    struct ProportionalState
    {
        double strengthAccumulator;
        double toughnessAccumulator;
        double dexterityAccumulator;
        bool strengthCapLogged;
        bool toughnessCapLogged;
        bool dexterityCapLogged;
        bool matchedLogged;
        DWORD lastXpSummaryTick;
        unsigned __int64 labourTicks;
        unsigned __int64 strengthTicks;
        unsigned __int64 toughnessTicks;
        unsigned __int64 dexterityTicks;
        double partnerAccumulators[PARTNER_SKILL_COUNT];
        unsigned __int64 partnerTicks[PARTNER_SKILL_COUNT];
        unsigned __int64 lastSummaryPartnerTicks[PARTNER_SKILL_COUNT];
        bool partnerStartedLogged[PARTNER_SKILL_COUNT];
        bool partnerBlockedLogged[PARTNER_SKILL_COUNT];
        unsigned __int64 lastSummaryLabourTicks;
        unsigned __int64 lastSummaryStrengthTicks;
        unsigned __int64 lastSummaryToughnessTicks;
        unsigned __int64 lastSummaryDexterityTicks;
        std::string sourceId;

        ProportionalState()
            : strengthAccumulator(0.0), toughnessAccumulator(0.0), dexterityAccumulator(0.0),
              strengthCapLogged(false), toughnessCapLogged(false), dexterityCapLogged(false),
              matchedLogged(false), lastXpSummaryTick(0), labourTicks(0),
              strengthTicks(0), toughnessTicks(0), dexterityTicks(0),
              lastSummaryLabourTicks(0), lastSummaryStrengthTicks(0),
              lastSummaryToughnessTicks(0), lastSummaryDexterityTicks(0)
        {
            for (int i = 0; i < PARTNER_SKILL_COUNT; ++i)
            {
                partnerAccumulators[i] = 0.0;
                partnerTicks[i] = 0;
                lastSummaryPartnerTicks[i] = 0;
                partnerStartedLogged[i] = false;
                partnerBlockedLogged[i] = false;
            }
        }
    };

    struct SourceSettings
    {
        bool enabled;
        std::string displayName;
        float strengthMultiplier;
        float strengthCap;
        float toughnessMultiplier;
        float toughnessCap;
        float dexterityMultiplier;
        float dexterityCap;
        std::string sourceFile;
        std::string trainingProfile;
        float partnerXpMultiplier[PARTNER_SKILL_COUNT];
        float partnerTeacherRatio[PARTNER_SKILL_COUNT];

        SourceSettings()
            : enabled(false), strengthMultiplier(0.0f), strengthCap(0.0f),
              toughnessMultiplier(0.0f), toughnessCap(0.0f),
              dexterityMultiplier(0.0f), dexterityCap(0.0f)
        {
            for (int i = 0; i < PARTNER_SKILL_COUNT; ++i)
            {
                partnerXpMultiplier[i] = 0.0f;
                partnerTeacherRatio[i] = 0.50f;
            }
        }
    };


    struct CombatSkillSnapshot
    {
        float meleeAttack;
        float meleeDefence;
        float dodge;
        float martialArts;
        float katanas;
        float sabres;
        float hackers;
        float heavyWeapons;
        float blunt;
        float polearms;
        float crossbows;
        float precisionShooting;
        float perception;
        float turrets;

        CombatSkillSnapshot()
            : meleeAttack(-1.0f), meleeDefence(-1.0f), dodge(-1.0f),
              martialArts(-1.0f), katanas(-1.0f), sabres(-1.0f),
              hackers(-1.0f), heavyWeapons(-1.0f), blunt(-1.0f),
              polearms(-1.0f), crossbows(-1.0f),
              precisionShooting(-1.0f), perception(-1.0f), turrets(-1.0f)
        {
        }
    };

    struct PartnerSkillDefinition
    {
        StatsEnumerated stat;
        const char* name;
        const char* keyPrefix;
    };

    const PartnerSkillDefinition kPartnerSkills[PARTNER_SKILL_COUNT] =
    {
        { STAT_MELEE_ATTACK, "Attack", "Attack" },
        { STAT_MELEE_DEFENCE, "Defence", "Defence" },
        { STAT_DODGE, "Dodge", "Dodge" },
        { STAT_MARTIALARTS, "MartialArts", "MartialArts" },
        { STAT_KATANAS, "Katanas", "Katanas" },
        { STAT_SABRES, "Sabres", "Sabres" },
        { STAT_HACKERS, "Hackers", "Hackers" },
        { STAT_HEAVYWEAPONS, "HeavyWeapons", "HeavyWeapons" },
        { STAT_BLUNT, "Blunt", "Blunt" },
        { STAT_POLEARMS, "Polearms", "Polearms" },
        { STAT_CROSSBOWS, "Crossbows", "Crossbows" },
        { STAT_FRIENDLY_FIRE, "PrecisionShooting", "PrecisionShooting" },
        { STAT_PERCEPTION, "Perception", "Perception" },
        { STAT_TURRETS, "Turrets", "Turrets" }
    };

    CombatSkillSnapshot ReadCombatSkills(CharStats* stats)
    {
        CombatSkillSnapshot result;
        if (!stats)
        {
            return result;
        }

        result.meleeAttack = stats->getStat(STAT_MELEE_ATTACK, true);
        result.meleeDefence = stats->getStat(STAT_MELEE_DEFENCE, true);
        result.dodge = stats->getStat(STAT_DODGE, true);
        result.martialArts = stats->getStat(STAT_MARTIALARTS, true);
        result.katanas = stats->getStat(STAT_KATANAS, true);
        result.sabres = stats->getStat(STAT_SABRES, true);
        result.hackers = stats->getStat(STAT_HACKERS, true);
        result.heavyWeapons = stats->getStat(STAT_HEAVYWEAPONS, true);
        result.blunt = stats->getStat(STAT_BLUNT, true);
        result.polearms = stats->getStat(STAT_POLEARMS, true);
        result.crossbows = stats->getStat(STAT_CROSSBOWS, true);
        result.precisionShooting = stats->getStat(STAT_FRIENDLY_FIRE, true);
        result.perception = stats->getStat(STAT_PERCEPTION, true);
        result.turrets = stats->getStat(STAT_TURRETS, true);
        return result;
    }

    float GetSnapshotPartnerSkill(const CombatSkillSnapshot& value, int index)
    {
        switch (index)
        {
        case PARTNER_ATTACK: return value.meleeAttack;
        case PARTNER_DEFENCE: return value.meleeDefence;
        case PARTNER_DODGE: return value.dodge;
        case PARTNER_MARTIAL_ARTS: return value.martialArts;
        case PARTNER_KATANAS: return value.katanas;
        case PARTNER_SABRES: return value.sabres;
        case PARTNER_HACKERS: return value.hackers;
        case PARTNER_HEAVY_WEAPONS: return value.heavyWeapons;
        case PARTNER_BLUNT: return value.blunt;
        case PARTNER_POLEARMS: return value.polearms;
        case PARTNER_CROSSBOWS: return value.crossbows;
        case PARTNER_PRECISION_SHOOTING: return value.precisionShooting;
        case PARTNER_PERCEPTION: return value.perception;
        case PARTNER_TURRETS: return value.turrets;
        default: return -1.0f;
        }
    }

    void UpdateHighestCombatSkills(
        CombatSkillSnapshot& highest,
        const CombatSkillSnapshot& candidate)
    {
        if (candidate.meleeAttack > highest.meleeAttack) highest.meleeAttack = candidate.meleeAttack;
        if (candidate.meleeDefence > highest.meleeDefence) highest.meleeDefence = candidate.meleeDefence;
        if (candidate.dodge > highest.dodge) highest.dodge = candidate.dodge;
        if (candidate.martialArts > highest.martialArts) highest.martialArts = candidate.martialArts;
        if (candidate.katanas > highest.katanas) highest.katanas = candidate.katanas;
        if (candidate.sabres > highest.sabres) highest.sabres = candidate.sabres;
        if (candidate.hackers > highest.hackers) highest.hackers = candidate.hackers;
        if (candidate.heavyWeapons > highest.heavyWeapons) highest.heavyWeapons = candidate.heavyWeapons;
        if (candidate.blunt > highest.blunt) highest.blunt = candidate.blunt;
        if (candidate.polearms > highest.polearms) highest.polearms = candidate.polearms;
        if (candidate.crossbows > highest.crossbows) highest.crossbows = candidate.crossbows;
        if (candidate.precisionShooting > highest.precisionShooting)
            highest.precisionShooting = candidate.precisionShooting;
        if (candidate.perception > highest.perception) highest.perception = candidate.perception;
        if (candidate.turrets > highest.turrets) highest.turrets = candidate.turrets;
    }

    void AppendCombatSkills(
        std::ostringstream& out,
        const char* label,
        const CombatSkillSnapshot& value)
    {
        out << " " << label << "={"
            << "Attack=" << value.meleeAttack
            << ",Defence=" << value.meleeDefence
            << ",Dodge=" << value.dodge
            << ",MartialArts=" << value.martialArts
            << ",Katanas=" << value.katanas
            << ",Sabres=" << value.sabres
            << ",Hackers=" << value.hackers
            << ",HeavyWeapons=" << value.heavyWeapons
            << ",Blunt=" << value.blunt
            << ",Polearms=" << value.polearms
            << ",Crossbows=" << value.crossbows
            << ",PrecisionShooting=" << value.precisionShooting
            << ",Perception=" << value.perception
            << ",Turrets=" << value.turrets
            << "}";
    }


    void AppendFixedDynamicCapPreview(
        std::ostringstream& out,
        const char* skillName,
        float currentValue,
        float teacherValue)
    {
        const float teacherRatio = 0.50f;
        const float dynamicCap =
            teacherValue >= 0.0f ? teacherValue * teacherRatio : -1.0f;

        const bool hasTeacher = teacherValue >= 0.0f;
        const bool trainingAllowed =
            hasTeacher && dynamicCap >= 0.0f && currentValue < dynamicCap;

        out << " " << skillName << "CapPreview={"
            << "Current=" << currentValue
            << ",Teacher=" << teacherValue
            << ",Ratio=" << teacherRatio
            << ",DynamicCap=" << dynamicCap
            << ",HasTeacher=" << (hasTeacher ? "true" : "false")
            << ",TrainingAllowed=" << (trainingAllowed ? "true" : "false")
            << ",TrainingBlocked=" << (trainingAllowed ? "false" : "true")
            << "}";
    }

    void AppendAllFixedDynamicCapPreviews(
        std::ostringstream& out,
        const CombatSkillSnapshot& current,
        const CombatSkillSnapshot& teacher)
    {
        AppendFixedDynamicCapPreview(out, "Attack", current.meleeAttack, teacher.meleeAttack);
        AppendFixedDynamicCapPreview(out, "Defence", current.meleeDefence, teacher.meleeDefence);
        AppendFixedDynamicCapPreview(out, "Dodge", current.dodge, teacher.dodge);
        AppendFixedDynamicCapPreview(out, "MartialArts", current.martialArts, teacher.martialArts);
        AppendFixedDynamicCapPreview(out, "Katanas", current.katanas, teacher.katanas);
        AppendFixedDynamicCapPreview(out, "Sabres", current.sabres, teacher.sabres);
        AppendFixedDynamicCapPreview(out, "Hackers", current.hackers, teacher.hackers);
        AppendFixedDynamicCapPreview(out, "HeavyWeapons", current.heavyWeapons, teacher.heavyWeapons);
        AppendFixedDynamicCapPreview(out, "Blunt", current.blunt, teacher.blunt);
        AppendFixedDynamicCapPreview(out, "Polearms", current.polearms, teacher.polearms);
        AppendFixedDynamicCapPreview(out, "Crossbows", current.crossbows, teacher.crossbows);
        AppendFixedDynamicCapPreview(
            out, "PrecisionShooting", current.precisionShooting, teacher.precisionShooting);
        AppendFixedDynamicCapPreview(out, "Perception", current.perception, teacher.perception);
        AppendFixedDynamicCapPreview(out, "Turrets", current.turrets, teacher.turrets);
    }

    struct UseableIdentity
    {
        bool valid;
        std::string displayName;
        std::string stringId;
        int dataId;
        int useableStat;
        unsigned int operatorCount;
        bool operatorIteratorHasEntry;
        int firstOperatorType;
        unsigned int firstOperatorContainer;
        unsigned int firstOperatorContainerSerial;
        unsigned int firstOperatorIndex;
        unsigned int firstOperatorSerial;
        bool firstOperatorCharacterResolved;
        unsigned int resolvedOperatorCharacters;
        bool currentCharacterFoundInOperators;
        float highestOperatorLabouring;
        CombatSkillSnapshot currentCombatSkills;
        CombatSkillSnapshot highestOperatorCombatSkills;
        CombatSkillSnapshot highestPartnerCombatSkills;
        unsigned int resolvedPartnerCharacters;
        std::string operatorLabouringSummary;
        std::string operatorCombatSummary;

        UseableIdentity()
            : valid(false), dataId(0), useableStat(static_cast<int>(STAT_NONE)),
              operatorCount(0), operatorIteratorHasEntry(false),
              firstOperatorType(-1), firstOperatorContainer(0),
              firstOperatorContainerSerial(0), firstOperatorIndex(0),
              firstOperatorSerial(0), firstOperatorCharacterResolved(false),
              resolvedOperatorCharacters(0), currentCharacterFoundInOperators(false),
              highestOperatorLabouring(-1.0f), resolvedPartnerCharacters(0) {}
    };

    typedef void (*XpStatTimeBasedFn)(CharStats* stats, StatsEnumerated stat);
    typedef void (*XpStatEventBasedFn)(CharStats* stats, StatsEnumerated stat, float amount);
    typedef void (*XpTrainingFn)(CharStats* stats, float time, float mult, float& statValue, float upperLimit, StatsEnumerated stat);

    HMODULE g_module = NULL;
    Config g_config;
    XpStatTimeBasedFn g_originalXpStatTimeBased = NULL;
    XpStatEventBasedFn g_originalXpStatEventBased = NULL;
    XpTrainingFn g_originalXpTraining = NULL;

    CRITICAL_SECTION g_stateLock;
    bool g_stateLockInitialised = false;
    std::map<SessionKey, SessionState> g_sessions;
    std::map<CharStats*, ProportionalState> g_proportionalStates;
    std::map<std::string, SourceSettings> g_sourceSettings;
    std::map<std::string, SourceSettings> g_trainingProfiles;
    std::set<std::string> g_unknownSourcesWritten;
    unsigned int g_writtenEntries = 0;
    unsigned int g_suppressedEntries = 0;
    volatile LONG g_timeBasedLabouringCalls = 0;
    volatile LONG g_eventBasedLabouringCalls = 0;
    volatile LONG g_strengthGrantCalls = 0;

    std::string GetModuleDirectory();
    bool ReadBool(const char* section, const char* key, bool defaultValue, const std::string& iniPath);
    unsigned int ReadUInt(const char* section, const char* key, unsigned int defaultValue, unsigned int minimumValue, unsigned int maximumValue, const std::string& iniPath);
    float ReadFloat(const char* section, const char* key, float defaultValue, float minimumValue, float maximumValue, const std::string& iniPath);
    bool ReserveLogEntry();

    std::string DescribeHandle(const char* label, const hand& value)
    {
        std::ostringstream out;
        out << label << "={Valid=" << (value.isValid() ? "true" : "false")
            << ",Null=" << (value.isNull() ? "true" : "false")
            << ",Type=" << static_cast<int>(value.type)
            << ",Index=" << value.index
            << ",Serial=" << value.serial;

        if (value.isValid())
        {
            Building* building = value.getBuilding();
            if (building)
            {
                out << ",Building=0x" << std::hex
                    << reinterpret_cast<uintptr_t>(building) << std::dec;
                out << ",DisplayName=\"" << building->getName() << "\"";

                GameData* data = building->getGameData();
                out << ",GameData=0x" << std::hex
                    << reinterpret_cast<uintptr_t>(data) << std::dec;
                if (data)
                {
                    out << ",DataValid=" << (data->isValid() ? "true" : "false")
                        << ",DataId=" << data->id
                        << ",DataType=" << static_cast<int>(data->type)
                        << ",ObjectDataType=" << static_cast<int>(building->getDataType())
                        << ",InternalName=\"" << data->name << "\""
                        << ",StringID=\"" << data->stringID << "\"";
                }

                UseableStuff* useable = building->getUseableStuff();
                out << ",Useable=0x" << std::hex
                    << reinterpret_cast<uintptr_t>(useable) << std::dec;
                if (useable)
                {
                    out << ",UseableStat=" << static_cast<int>(useable->getStatUsed())
                        << ",OperatorCount=" << useable->currentOperators.size();
                }
            }
        }

        out << "}";
        return out.str();
    }

    bool TryGetCurrentUseableIdentity(Character* character, UseableIdentity& identity)
    {
        identity = UseableIdentity();
        if (!character)
        {
            return false;
        }

        AI* ai = character->getAI();
        if (!ai)
        {
            return false;
        }

        AITaskSytem* taskSystem = ai->getTaskSystem();
        if (!taskSystem)
        {
            return false;
        }

        const TaskMatch& goal = taskSystem->getCurrentGoal();
        if (!goal.subject.isValid() || goal.subject.isNull())
        {
            return false;
        }

        Building* building = goal.subject.getBuilding();
        if (!building)
        {
            return false;
        }

        UseableStuff* useable = building->getUseableStuff();
        GameData* data = building->getGameData();
        if (!useable || !data || !data->isValid())
        {
            return false;
        }

        // Partner Training Prototype v0.01:
        // Enumerate every current operator, resolve Character and CharStats,
        // read Labouring, and calculate the highest observed Labouring value.
        // No Partner Training XP or dynamic cap is applied in this version.
        const std::set<hand, std::less<hand>, Ogre::STLAllocator<hand, Ogre::GeneralAllocPolicy > >::const_iterator
            operatorBegin = useable->currentOperators.begin();
        const std::set<hand, std::less<hand>, Ogre::STLAllocator<hand, Ogre::GeneralAllocPolicy > >::const_iterator
            operatorEnd = useable->currentOperators.end();

        identity.currentCombatSkills = ReadCombatSkills(character->getStats());

        std::ostringstream operatorSummary;
        std::ostringstream operatorCombatSummary;
        unsigned int operatorOrdinal = 0;

        for (std::set<hand, std::less<hand>, Ogre::STLAllocator<hand, Ogre::GeneralAllocPolicy > >::const_iterator
                 operatorIt = operatorBegin;
             operatorIt != operatorEnd;
             ++operatorIt, ++operatorOrdinal)
        {
            const hand operatorHandle = *operatorIt;

            if (operatorOrdinal == 0)
            {
                identity.firstOperatorType = static_cast<int>(operatorHandle.type);
                identity.firstOperatorContainer = operatorHandle.container;
                identity.firstOperatorContainerSerial = operatorHandle.containerSerial;
                identity.firstOperatorIndex = operatorHandle.index;
                identity.firstOperatorSerial = operatorHandle.serial;
            }

            Character* operatorCharacter = operatorHandle.getCharacter();
            if (operatorOrdinal == 0)
            {
                identity.firstOperatorCharacterResolved = (operatorCharacter != NULL);
            }

            if (!operatorCharacter)
            {
                operatorSummary << " Op" << operatorOrdinal << "={Character=false}";
                continue;
            }

            if (operatorCharacter == character)
            {
                identity.currentCharacterFoundInOperators = true;
            }

            CharStats* operatorStats = operatorCharacter->getStats();
            if (!operatorStats)
            {
                operatorSummary << " Op" << operatorOrdinal
                                << "={Character=true,Stats=false}";
                continue;
            }

            const float labouring = operatorStats->getStat(STAT_LABOURING, true);
            const CombatSkillSnapshot combatSkills = ReadCombatSkills(operatorStats);
            ++identity.resolvedOperatorCharacters;

            if (labouring > identity.highestOperatorLabouring)
            {
                identity.highestOperatorLabouring = labouring;
            }
            UpdateHighestCombatSkills(identity.highestOperatorCombatSkills, combatSkills);

            if (operatorCharacter != character)
            {
                UpdateHighestCombatSkills(identity.highestPartnerCombatSkills, combatSkills);
                ++identity.resolvedPartnerCharacters;
            }

            operatorSummary << " Op" << operatorOrdinal
                            << "={Character=0x" << std::hex
                            << reinterpret_cast<uintptr_t>(operatorCharacter)
                            << std::dec
                            << ",Labouring=" << labouring
                            << ",IsCurrent="
                            << (operatorCharacter == character ? "true" : "false")
                            << "}";

            operatorCombatSummary << " Op" << operatorOrdinal
                                  << "Combat={Character=0x" << std::hex
                                  << reinterpret_cast<uintptr_t>(operatorCharacter)
                                  << std::dec
                                  << ",IsCurrent="
                                  << (operatorCharacter == character ? "true" : "false");
            AppendCombatSkills(operatorCombatSummary, "Skills", combatSkills);
            operatorCombatSummary << "}";
        }

        identity.operatorLabouringSummary = operatorSummary.str();
        identity.operatorCombatSummary = operatorCombatSummary.str();
        identity.valid = true;
        identity.displayName = building->getName();
        identity.stringId = data->stringID;
        identity.dataId = data->id;
        identity.useableStat = static_cast<int>(useable->getStatUsed());
        identity.operatorCount = static_cast<unsigned int>(useable->currentOperators.size());
        identity.operatorIteratorHasEntry = (operatorBegin != operatorEnd);
        return true;
    }

    std::string Trim(const std::string& input)
    {
        std::string::size_type first = 0;
        while (first < input.size() && isspace(static_cast<unsigned char>(input[first]))) ++first;
        std::string::size_type last = input.size();
        while (last > first && isspace(static_cast<unsigned char>(input[last - 1]))) --last;
        return input.substr(first, last - first);
    }

    std::string ReadString(const char* section, const char* key, const char* defaultValue,
        const std::string& iniPath)
    {
        char buffer[1024] = { 0 };
        GetPrivateProfileStringA(section, key, defaultValue, buffer, sizeof(buffer), iniPath.c_str());
        return Trim(buffer);
    }

    bool HasIniExtension(const std::string& name)
    {
        if (name.size() < 4) return false;
        std::string ext = name.substr(name.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".ini";
    }

    void LoadTrainingProfileFile(const std::string& iniPath)
    {
        std::vector<char> sectionBuffer(65536, 0);
        const DWORD copied = GetPrivateProfileSectionNamesA(&sectionBuffer[0],
            static_cast<DWORD>(sectionBuffer.size()), iniPath.c_str());
        if (copied == 0) return;

        const char* cursor = &sectionBuffer[0];
        while (*cursor)
        {
            const std::string section(cursor);
            cursor += section.size() + 1;
            if (section.empty() || section == "Metadata") continue;

            SourceSettings profile;
            std::map<std::string, SourceSettings>::const_iterator existing = g_trainingProfiles.find(section);
            if (existing != g_trainingProfiles.end()) profile = existing->second;

            const std::string missing = "__MULTIXP_MISSING_KEY__";
            for (int skillIndex = 0; skillIndex < PARTNER_SKILL_COUNT; ++skillIndex)
            {
                const std::string multiplierKey = std::string(kPartnerSkills[skillIndex].keyPrefix) + "XpMultiplier";
                const std::string ratioKey = std::string(kPartnerSkills[skillIndex].keyPrefix) + "TeacherRatio";
                const std::string skillMultiplierValue = ReadString(section.c_str(), multiplierKey.c_str(), missing.c_str(), iniPath);
                const std::string skillRatioValue = ReadString(section.c_str(), ratioKey.c_str(), missing.c_str(), iniPath);
                if (skillMultiplierValue != missing)
                    profile.partnerXpMultiplier[skillIndex] = ReadFloat(section.c_str(), multiplierKey.c_str(), profile.partnerXpMultiplier[skillIndex], 0.0f, 100.0f, iniPath);
                if (skillRatioValue != missing)
                    profile.partnerTeacherRatio[skillIndex] = ReadFloat(section.c_str(), ratioKey.c_str(), profile.partnerTeacherRatio[skillIndex], 0.0f, 10.0f, iniPath);
            }
            profile.sourceFile = iniPath;
            g_trainingProfiles[section] = profile;
        }
    }

    void LoadTrainingProfiles()
    {
        g_trainingProfiles.clear();
        const std::string directory = GetModuleDirectory() + "\\TrainingProfiles";
        const std::string pattern = directory + "\\*.ini";
        WIN32_FIND_DATAA findData;
        HANDLE findHandle = FindFirstFileA(pattern.c_str(), &findData);
        std::vector<std::string> files;
        if (findHandle != INVALID_HANDLE_VALUE)
        {
            do
            {
                if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && HasIniExtension(findData.cFileName))
                    files.push_back(findData.cFileName);
            } while (FindNextFileA(findHandle, &findData));
            FindClose(findHandle);
        }
        std::sort(files.begin(), files.end());
        for (std::vector<std::string>::const_iterator it = files.begin(); it != files.end(); ++it)
            LoadTrainingProfileFile(directory + "\\" + *it);

        std::ostringstream message;
        message << "[" << kPluginName << "] [INFO] TrainingProfiles loaded: Files=" << files.size()
                << ", Profiles=" << g_trainingProfiles.size();
        DebugLog(message.str());
    }

    void LoadSourceDataFile(const std::string& iniPath)
    {
        const unsigned int sourceDataVersion = ReadUInt("Metadata", "SourceDataVersion",
            kSupportedSourceDataVersion, 1, 1000, iniPath);
        if (sourceDataVersion != kSupportedSourceDataVersion && ReserveLogEntry())
        {
            std::ostringstream warning;
            warning << "[" << kPluginName << "] [WARNING] SourceData version mismatch"
                    << " File=\"" << iniPath << "\""
                    << " Found=" << sourceDataVersion
                    << " Supported=" << kSupportedSourceDataVersion;
            DebugLog(warning.str());
        }

        std::vector<char> sectionBuffer(65536, 0);
        const DWORD copied = GetPrivateProfileSectionNamesA(&sectionBuffer[0],
            static_cast<DWORD>(sectionBuffer.size()), iniPath.c_str());
        if (copied == 0) return;

        const char* cursor = &sectionBuffer[0];
        while (*cursor)
        {
            const std::string section(cursor);
            cursor += section.size() + 1;
            if (section.empty() || section == "General" || section == "Logging" || section == "Metadata") continue;

            SourceSettings settings;
            std::map<std::string, SourceSettings>::const_iterator existing = g_sourceSettings.find(section);
            if (existing != g_sourceSettings.end()) settings = existing->second;

            const std::string missing = "__MULTIXP_MISSING_KEY__";
            const std::string profileValue = ReadString(section.c_str(), "TrainingProfile", missing.c_str(), iniPath);
            if (profileValue != missing)
            {
                settings.trainingProfile = profileValue;
                std::map<std::string, SourceSettings>::const_iterator profileIt = g_trainingProfiles.find(profileValue);
                if (profileIt != g_trainingProfiles.end())
                {
                    for (int i = 0; i < PARTNER_SKILL_COUNT; ++i)
                    {
                        settings.partnerXpMultiplier[i] = profileIt->second.partnerXpMultiplier[i];
                        settings.partnerTeacherRatio[i] = profileIt->second.partnerTeacherRatio[i];
                    }
                }
                else if (ReserveLogEntry())
                {
                    std::ostringstream warning;
                    warning << "[" << kPluginName << "] [WARNING] Unknown TrainingProfile"
                            << " Source=\"" << section << "\""
                            << " Profile=\"" << profileValue << "\"";
                    DebugLog(warning.str());
                }
            }

            const std::string enabledValue = ReadString(section.c_str(), "Enabled", missing.c_str(), iniPath);
            const std::string displayValue = ReadString(section.c_str(), "DisplayName", missing.c_str(), iniPath);
            const std::string multiplierValue = ReadString(section.c_str(), "StrengthMultiplier", missing.c_str(), iniPath);
            const std::string capValue = ReadString(section.c_str(), "StrengthCap", missing.c_str(), iniPath);
            const std::string toughnessMultiplierValue = ReadString(section.c_str(), "ToughnessMultiplier", missing.c_str(), iniPath);
            const std::string toughnessCapValue = ReadString(section.c_str(), "ToughnessCap", missing.c_str(), iniPath);
            const std::string dexterityMultiplierValue = ReadString(section.c_str(), "DexterityMultiplier", missing.c_str(), iniPath);
            const std::string dexterityCapValue = ReadString(section.c_str(), "DexterityCap", missing.c_str(), iniPath);

            if (enabledValue != missing) settings.enabled = ReadBool(section.c_str(), "Enabled", settings.enabled, iniPath);
            if (displayValue != missing) settings.displayName = displayValue;
            if (multiplierValue != missing) settings.strengthMultiplier = ReadFloat(section.c_str(), "StrengthMultiplier", settings.strengthMultiplier, 0.0f, 100.0f, iniPath);
            if (capValue != missing) settings.strengthCap = ReadFloat(section.c_str(), "StrengthCap", settings.strengthCap, 0.0f, 1000.0f, iniPath);
            if (toughnessMultiplierValue != missing) settings.toughnessMultiplier = ReadFloat(section.c_str(), "ToughnessMultiplier", settings.toughnessMultiplier, 0.0f, 100.0f, iniPath);
            if (toughnessCapValue != missing) settings.toughnessCap = ReadFloat(section.c_str(), "ToughnessCap", settings.toughnessCap, 0.0f, 1000.0f, iniPath);
            if (dexterityMultiplierValue != missing) settings.dexterityMultiplier = ReadFloat(section.c_str(), "DexterityMultiplier", settings.dexterityMultiplier, 0.0f, 100.0f, iniPath);
            if (dexterityCapValue != missing) settings.dexterityCap = ReadFloat(section.c_str(), "DexterityCap", settings.dexterityCap, 0.0f, 1000.0f, iniPath);
            for (int skillIndex = 0; skillIndex < PARTNER_SKILL_COUNT; ++skillIndex)
            {
                const std::string multiplierKey = std::string(kPartnerSkills[skillIndex].keyPrefix) + "XpMultiplier";
                const std::string ratioKey = std::string(kPartnerSkills[skillIndex].keyPrefix) + "TeacherRatio";
                const std::string skillMultiplierValue = ReadString(section.c_str(), multiplierKey.c_str(), missing.c_str(), iniPath);
                const std::string skillRatioValue = ReadString(section.c_str(), ratioKey.c_str(), missing.c_str(), iniPath);
                if (skillMultiplierValue != missing)
                    settings.partnerXpMultiplier[skillIndex] = ReadFloat(section.c_str(), multiplierKey.c_str(), settings.partnerXpMultiplier[skillIndex], 0.0f, 100.0f, iniPath);
                if (skillRatioValue != missing)
                    settings.partnerTeacherRatio[skillIndex] = ReadFloat(section.c_str(), ratioKey.c_str(), settings.partnerTeacherRatio[skillIndex], 0.0f, 10.0f, iniPath);
            }
            settings.sourceFile = iniPath;
            g_sourceSettings[section] = settings;
        }
    }

    void LoadSourceData()
    {
        g_sourceSettings.clear();
        const std::string directory = GetModuleDirectory() + "\\SourceData";
        const std::string pattern = directory + "\\*.ini";
        WIN32_FIND_DATAA findData;
        HANDLE findHandle = FindFirstFileA(pattern.c_str(), &findData);
        std::vector<std::string> files;
        if (findHandle != INVALID_HANDLE_VALUE)
        {
            do
            {
                if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && HasIniExtension(findData.cFileName))
                    files.push_back(findData.cFileName);
            } while (FindNextFileA(findHandle, &findData));
            FindClose(findHandle);
        }
        std::sort(files.begin(), files.end());
        for (std::vector<std::string>::const_iterator it = files.begin(); it != files.end(); ++it)
            LoadSourceDataFile(directory + "\\" + *it);

        std::ostringstream message;
        message << "[" << kPluginName << "] [INFO] SourceData loaded: Files=" << files.size()
                << ", Sources=" << g_sourceSettings.size();
        DebugLog(message.str());
    }

    std::string GetSourceFileFromStringId(const std::string& stringId)
    {
        const std::string::size_type separator = stringId.find('-');
        if (separator == std::string::npos || separator + 1 >= stringId.size())
            return stringId;
        return stringId.substr(separator + 1);
    }

    bool IsOfficialVanillaSourceFile(const std::string& sourceFile)
    {
        // Kenshi's vanilla data includes several official files that retain
        // the .mod extension because of the game's long development history.
        // Compare source-file names only; never infer from translated display names.
        static const char* const kOfficialSources[] =
        {
            "gamedatabase",
            "gamedata.base",
            "Newwworld.mod",
            "Dialogue.mod",
            "rebirth.mod"
        };

        for (size_t i = 0; i < sizeof(kOfficialSources) / sizeof(kOfficialSources[0]); ++i)
        {
            if (_stricmp(sourceFile.c_str(), kOfficialSources[i]) == 0)
                return true;
        }
        return false;
    }

    bool IsThirdPartyModSourceStringId(const std::string& stringId)
    {
        const std::string sourceFile = GetSourceFileFromStringId(stringId);
        if (IsOfficialVanillaSourceFile(sourceFile)) return false;

        const std::string suffix = ".mod";
        if (sourceFile.size() < suffix.size()) return false;
        const std::string tail = sourceFile.substr(sourceFile.size() - suffix.size());
        return _stricmp(tail.c_str(), suffix.c_str()) == 0;
    }

    void WriteUnknownSource(const UseableIdentity& identity)
    {
        if (!g_config.generateUnknownSources || identity.stringId.empty()) return;

        // UnknownSources.ini is intended for third-party FCS content.
        // Kenshi also ships official vanilla data in .mod files, so exclude
        // the known official source files before accepting third-party .mod data.
        // Do not infer equipment type from translated display names.
        if (!IsThirdPartyModSourceStringId(identity.stringId)) return;

        EnterCriticalSection(&g_stateLock);
        const bool alreadyKnown = g_unknownSourcesWritten.find(identity.stringId) != g_unknownSourcesWritten.end();
        if (!alreadyKnown) g_unknownSourcesWritten.insert(identity.stringId);
        LeaveCriticalSection(&g_stateLock);
        if (alreadyKnown) return;

        const std::string path = GetModuleDirectory() + "\\UnknownSources.ini";
        std::ofstream out(path.c_str(), std::ios::out | std::ios::app | std::ios::binary);
        if (!out) return;
        out << "\r\n; ================================================================\r\n";
        out << "; Partner Training: unknown source detected\r\n";
        out << "; Copy this entire block into SourceData\\99_UserOverride.ini,\r\n";
        out << "; then set Enabled=true and configure the multipliers and caps.\r\n";
        out << "; This block is disabled by default for compatibility and safety.\r\n";
        out << "; DisplayName=" << identity.displayName << "\r\n";
        out << "; ================================================================\r\n";
        out << "[" << identity.stringId << "]\r\n";
        out << "Enabled=false\r\n";
        out << "DisplayName=" << identity.displayName << "\r\n";
        out << "StrengthMultiplier=0.0\r\n";
        out << "StrengthCap=0.0\r\n";
        out << "ToughnessMultiplier=0.0\r\n";
        out << "ToughnessCap=0.0\r\n";
        out << "DexterityMultiplier=0.0\r\n";
        out << "DexterityCap=0.0\r\n";
        out << "TrainingProfile=Template_AllCombatSkills\r\n";
        out << "; Replace the profile above with a bundled or custom profile.\r\n";
        out.close();

        if (ReserveLogEntry())
        {
            std::ostringstream message;
            message << "[" << kPluginName << "] [WARNING] Unknown Source recorded"
                    << " StringID=\"" << identity.stringId << "\""
                    << " DisplayName=\"" << identity.displayName << "\"";
            DebugLog(message.str());
        }
    }

    bool GetRegisteredSourceSettings(const UseableIdentity& identity, SourceSettings& settings)
    {
        settings = SourceSettings();
        if (!identity.valid) return false;
        std::map<std::string, SourceSettings>::const_iterator it = g_sourceSettings.find(identity.stringId);
        if (it == g_sourceSettings.end())
        {
            WriteUnknownSource(identity);
            return false;
        }
        settings = it->second;
        return settings.enabled;
    }

    std::string CaptureKenshiLibTaskContext(Character* character)
    {
        if (!character)
        {
            return "Character=null";
        }

        std::ostringstream out;
        out << "CurrentSkillUsing=" << static_cast<int>(character->currentSkillUsing);

        AI* ai = character->getAI();
        out << " AI=0x" << std::hex << reinterpret_cast<uintptr_t>(ai) << std::dec;
        if (!ai)
        {
            return out.str();
        }

        AITaskSytem* taskSystem = ai->getTaskSystem();
        out << " TaskSystem=0x" << std::hex
            << reinterpret_cast<uintptr_t>(taskSystem) << std::dec;
        if (!taskSystem)
        {
            return out.str();
        }

        const TaskMatch& goal = taskSystem->getCurrentGoal();
        out << " GoalKey=" << static_cast<int>(goal.key())
            << " " << DescribeHandle("GoalSubject", goal.subject)
            << " " << DescribeHandle("GoalSubtarget", goal.subtarget);

        // First-order inspection is intentionally omitted in this build.
        // AITaskSystem::getCurrentGoal() is sufficient for the first safe test,
        // and avoiding Tasker.h prevents duplicate taskPriority definitions.


        return out.str();
    }

    std::string GetModuleDirectory()
    {
        char path[MAX_PATH] = { 0 };
        const DWORD length = GetModuleFileNameA(g_module, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            return ".";
        }

        std::string result(path, length);
        const std::string::size_type separator = result.find_last_of("\\/");
        if (separator == std::string::npos)
        {
            return ".";
        }
        return result.substr(0, separator);
    }

    bool ReadBool(const char* section, const char* key, bool defaultValue, const std::string& iniPath)
    {
        char buffer[32] = { 0 };
        GetPrivateProfileStringA(section, key, defaultValue ? "true" : "false",
            buffer, sizeof(buffer), iniPath.c_str());

        std::string value(buffer);
        value.erase(std::remove_if(value.begin(), value.end(), ::isspace), value.end());
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);

        if (value == "true" || value == "yes" || value == "on" || value == "1") return true;
        if (value == "false" || value == "no" || value == "off" || value == "0") return false;
        return defaultValue;
    }

    unsigned int ReadUInt(const char* section, const char* key,
        unsigned int defaultValue, unsigned int minimumValue,
        unsigned int maximumValue, const std::string& iniPath)
    {
        const int raw = GetPrivateProfileIntA(section, key,
            static_cast<int>(defaultValue), iniPath.c_str());
        return static_cast<unsigned int>((std::max)(
            static_cast<int>(minimumValue),
            (std::min)(raw, static_cast<int>(maximumValue))));
    }

    float ReadFloat(const char* section, const char* key, float defaultValue,
        float minimumValue, float maximumValue, const std::string& iniPath)
    {
        char defaultBuffer[64] = { 0 };
        char buffer[64] = { 0 };
        sprintf_s(defaultBuffer, sizeof(defaultBuffer), "%.6f", defaultValue);
        GetPrivateProfileStringA(section, key, defaultBuffer, buffer, sizeof(buffer), iniPath.c_str());

        char* end = NULL;
        const double parsed = strtod(buffer, &end);
        if (end == buffer)
        {
            return defaultValue;
        }

        const float value = static_cast<float>(parsed);
        return (std::max)(minimumValue, (std::min)(value, maximumValue));
    }

    void LoadConfig()
    {
        const std::string iniPath = GetModuleDirectory() + "\\PartnerTraining.ini";
        g_config.configVersion = ReadUInt("General", "ConfigVersion", 1, 1, 1000, iniPath);
        g_config.enabled = ReadBool("General", "Enabled", true, iniPath);
        g_config.globalXpMultiplier = ReadFloat("General", "GlobalXpMultiplier", 1.0f, 0.0f, 100.0f, iniPath);
        g_config.globalCapMultiplier = ReadFloat("General", "GlobalCapMultiplier", 1.0f, 0.0f, 10.0f, iniPath);
        g_config.enableCaps = ReadBool("General", "EnableCaps", true, iniPath);
        g_config.loggingEnabled = ReadBool("Logging", "Enabled", true, iniPath);
        g_config.detailedLogging = ReadBool("Logging", "DetailedLogging", false, iniPath);
        g_config.logLabouringXp = ReadBool("Logging", "LogLabouringXp", false, iniPath);
        g_config.summaryIntervalMs = ReadUInt("Logging", "SummaryIntervalMs", 5000, 250, 600000, iniPath);
        g_config.sessionGapMs = ReadUInt("Logging", "SessionGapMs", 1500, 100, 600000, iniPath);
        g_config.maximumLogEntries = ReadUInt("Logging", "MaximumLogEntries", 500, 1, 100000, iniPath);
        g_config.defaultStrengthMultiplier = ReadFloat("Defaults", "StrengthMultiplier", 0.0f, 0.0f, 100.0f, iniPath);
        g_config.defaultStrengthCap = ReadFloat("Defaults", "StrengthCap", 0.0f, 0.0f, 1000.0f, iniPath);
        g_config.defaultToughnessMultiplier = ReadFloat("Defaults", "ToughnessMultiplier", 0.0f, 0.0f, 100.0f, iniPath);
        g_config.defaultToughnessCap = ReadFloat("Defaults", "ToughnessCap", 0.0f, 0.0f, 1000.0f, iniPath);
        g_config.defaultDexterityMultiplier = ReadFloat("Defaults", "DexterityMultiplier", 0.0f, 0.0f, 100.0f, iniPath);
        g_config.defaultDexterityCap = ReadFloat("Defaults", "DexterityCap", 0.0f, 0.0f, 1000.0f, iniPath);
        g_config.generateUnknownSources = ReadBool("General", "GenerateUnknownSourcesIni", true, iniPath);
        g_config.xpSummaryIntervalMs = ReadUInt("Logging", "XpSummaryIntervalMs", 5000, 500, 600000, iniPath);

        std::ostringstream message;
        message << "[" << kPluginName << "] [INFO] Config loaded"
                << " ConfigVersion=" << g_config.configVersion
                << " Enabled=" << (g_config.enabled ? "true" : "false")
                << " GlobalXpMultiplier=" << g_config.globalXpMultiplier
                << " GlobalCapMultiplier=" << g_config.globalCapMultiplier
                << " EnableCaps=" << (g_config.enableCaps ? "true" : "false")
                << " DetailedLogging=" << (g_config.detailedLogging ? "true" : "false");
        DebugLog(message.str());

        if (g_config.detailedLogging)
        {
            std::ostringstream details;
            details << "[" << kPluginName << "] [DEBUG] Logging configuration"
                    << " LogLabouringXp=" << (g_config.logLabouringXp ? "true" : "false")
                    << " SummaryIntervalMs=" << g_config.summaryIntervalMs
                    << " SessionGapMs=" << g_config.sessionGapMs
                    << " MaximumLogEntries=" << g_config.maximumLogEntries
                    << " GenerateUnknownSourcesIni=" << (g_config.generateUnknownSources ? "true" : "false")
                    << " XpSummaryIntervalMs=" << g_config.xpSummaryIntervalMs
                    << " AttributeDefaults={Strength=" << g_config.defaultStrengthMultiplier << "/" << g_config.defaultStrengthCap
                    << ",Toughness=" << g_config.defaultToughnessMultiplier << "/" << g_config.defaultToughnessCap
                    << ",Dexterity=" << g_config.defaultDexterityMultiplier << "/" << g_config.defaultDexterityCap << "}";
            DebugLog(details.str());
        }
        if (g_config.configVersion != kSupportedConfigVersion && ReserveLogEntry())
        {
            std::ostringstream warning;
            warning << "[" << kPluginName << "] [WARNING] Config version mismatch"
                    << " Found=" << g_config.configVersion
                    << " Supported=" << kSupportedConfigVersion
                    << ". Unknown options will use safe defaults.";
            DebugLog(warning.str());
        }
        LoadTrainingProfiles();
        LoadSourceData();
    }

    uintptr_t GetMainModuleBase()
    {
        return reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
    }

    bool ReserveLogEntry()
    {
        if (!g_config.loggingEnabled)
        {
            return false;
        }
        if (g_writtenEntries >= g_config.maximumLogEntries)
        {
            ++g_suppressedEntries;
            return false;
        }
        ++g_writtenEntries;
        return true;
    }

    void LogSessionLine(const char* type, const SessionKey& key,
        const SessionState& state, DWORD timestamp)
    {
        if (!ReserveLogEntry())
        {
            return;
        }

        const DWORD durationMs = timestamp >= state.startTick ? timestamp - state.startTick : 0;
        const double seconds = durationMs > 0 ? static_cast<double>(durationMs) / 1000.0 : 0.0;
        const double callsPerSecond = seconds > 0.0
            ? static_cast<double>(state.calls) / seconds : 0.0;

        std::ostringstream message;
        message << "[" << kPluginName << "] LabourSession " << type
                << " Stats=0x" << std::hex << reinterpret_cast<uintptr_t>(key.stats)
                << " Character=0x" << reinterpret_cast<uintptr_t>(key.stats ? key.stats->me : NULL)
                << " CallerRVA=0x" << key.callerRva << std::dec
                << " Calls=" << state.calls
                << " DurationMs=" << durationMs
                << " CallsPerSecond=" << std::fixed << std::setprecision(2) << callsPerSecond;
        if (std::string(type) == "BEGIN")
        {
            Character* character = key.stats ? key.stats->me : NULL;
            message << " KenshiLibContext={"
                    << CaptureKenshiLibTaskContext(character)
                    << "}";
        }
        DebugLog(message.str());
    }

    void ObserveLabouringXp(CharStats* stats, void* caller)
    {
        if (!g_config.loggingEnabled || !g_config.logLabouringXp || !g_stateLockInitialised)
        {
            return;
        }

        const DWORD now = GetTickCount();
        const uintptr_t base = GetMainModuleBase();
        const uintptr_t callerAddress = reinterpret_cast<uintptr_t>(caller);
        const uintptr_t callerRva = callerAddress >= base ? callerAddress - base : 0;
        const SessionKey key(stats, callerRva);

        EnterCriticalSection(&g_stateLock);

        SessionState& state = g_sessions[key];
        const bool hadExistingSession = state.startTick != 0;
        const bool hadGap = hadExistingSession &&
            (now - state.lastCallTick >= g_config.sessionGapMs);

        if (hadGap)
        {
            LogSessionLine("END", key, state, state.lastCallTick);
            state = SessionState();
        }

        if (state.startTick == 0)
        {
            state.startTick = now;
            state.lastCallTick = now;
            state.lastSummaryTick = now;
            state.calls = 1;
            LogSessionLine("BEGIN", key, state, now);
        }
        else
        {
            state.lastCallTick = now;
            ++state.calls;
            if (now - state.lastSummaryTick >= g_config.summaryIntervalMs)
            {
                state.lastSummaryTick = now;
                LogSessionLine("SUMMARY", key, state, now);
            }
        }

        LeaveCriticalSection(&g_stateLock);
    }

    float GetBaseStatValue(CharStats* stats, StatsEnumerated stat)
    {
        if (!stats) return 0.0f;
        return stats->getStat(stat, true);
    }

    void GrantConfiguredStat(CharStats* stats, StatsEnumerated stat, const char* statName,
        float sourceMultiplier, float sourceCap, double& accumulator,
        unsigned __int64& tickCounter, bool& capLogged)
    {
        const float effectiveMultiplier = sourceMultiplier * g_config.globalXpMultiplier;
        const float effectiveCap = sourceCap * g_config.globalCapMultiplier;
        if (effectiveMultiplier <= 0.0f) return;

        const float currentValue = GetBaseStatValue(stats, stat);
        if (g_config.enableCaps && (effectiveCap <= 0.0f || currentValue >= effectiveCap))
        {
            if (!capLogged && ReserveLogEntry())
            {
                capLogged = true;
                std::ostringstream message;
                message << "[" << kPluginName << "] " << statName << " CAP REACHED"
                        << " Character=0x" << std::hex << reinterpret_cast<uintptr_t>(stats->me) << std::dec
                        << " Current=" << std::fixed << std::setprecision(4) << currentValue
                        << " EffectiveCap=" << effectiveCap;
                DebugLog(message.str());
            }
            return;
        }

        capLogged = false;
        accumulator += effectiveMultiplier;
        unsigned int grants = 0;
        while (accumulator >= 1.0 && grants < 100)
        {
            accumulator -= 1.0;
            ++grants;
        }
        for (unsigned int i = 0; i < grants; ++i)
        {
            if (g_config.enableCaps && GetBaseStatValue(stats, stat) >= effectiveCap) break;
            g_originalXpStatTimeBased(stats, stat);
            ++tickCounter;
        }
    }


    void GrantPartnerTrainingXp(
        CharStats* stats,
        StatsEnumerated stat,
        const char* skillName,
        float teacherSkill,
        float teacherRatio,
        float xpMultiplier,
        double& accumulator,
        unsigned __int64& tickCounter,
        bool& startedLogged,
        bool& blockedLogged,
        bool useTrainingPath,
        float trainingTime,
        float trainingMult)
    {
        if (xpMultiplier <= 0.0f || teacherRatio <= 0.0f || teacherSkill < 0.0f)
            return;

        const float dynamicCap = teacherSkill * teacherRatio;
        const float currentSkill = GetBaseStatValue(stats, stat);
        if (currentSkill >= dynamicCap)
        {
            if (g_config.detailedLogging && !blockedLogged && ReserveLogEntry())
            {
                blockedLogged = true;
                std::ostringstream message;
                message << "[" << kPluginName << "] [PARTNER_TRAINING_BLOCKED]"
                        << " Skill=" << skillName
                        << " Character=0x" << std::hex << reinterpret_cast<uintptr_t>(stats->me) << std::dec
                        << " Current=" << currentSkill << " Teacher=" << teacherSkill
                        << " Ratio=" << teacherRatio << " XpMultiplier=" << xpMultiplier
                        << " DynamicCap=" << dynamicCap;
                DebugLog(message.str());
            }
            return;
        }

        blockedLogged = false;
        if (g_config.detailedLogging && !startedLogged && ReserveLogEntry())
        {
            startedLogged = true;
            std::ostringstream message;
            message << "[" << kPluginName << "] [PARTNER_TRAINING_STARTED]"
                    << " Skill=" << skillName
                    << " Character=0x" << std::hex << reinterpret_cast<uintptr_t>(stats->me) << std::dec
                    << " Current=" << currentSkill << " Teacher=" << teacherSkill
                    << " Ratio=" << teacherRatio << " XpMultiplier=" << xpMultiplier
                    << " DynamicCap=" << dynamicCap;
            DebugLog(message.str());
        }

        accumulator += xpMultiplier;
        unsigned int grants = 0;
        while (accumulator >= 1.0 && grants < 100)
        {
            accumulator -= 1.0;
            ++grants;
        }
        for (unsigned int i = 0; i < grants; ++i)
        {
            if (GetBaseStatValue(stats, stat) >= dynamicCap) break;
            if (useTrainingPath)
            {
                if (!g_originalXpTraining) break;
                float& statRef = stats->getStatRef(stat);
                g_originalXpTraining(
                    stats,
                    trainingTime,
                    trainingMult * xpMultiplier * g_config.globalXpMultiplier,
                    statRef,
                    dynamicCap,
                    stat);
            }
            else
            {
                if (!g_originalXpStatTimeBased) break;
                g_originalXpStatTimeBased(stats, stat);
            }
            ++tickCounter;
        }
    }

    void ApplyProportionalMultiXp(
        CharStats* stats,
        bool useTrainingPath,
        float trainingTime,
        float trainingMult)
    {
        if (!g_config.enabled || !stats || !stats->me || !g_stateLockInitialised) return;
        if (useTrainingPath)
        {
            if (!g_originalXpTraining) return;
        }
        else if (!g_originalXpStatTimeBased)
        {
            return;
        }

        UseableIdentity identity;
        SourceSettings sourceSettings;
        if (!TryGetCurrentUseableIdentity(stats->me, identity) ||
            !GetRegisteredSourceSettings(identity, sourceSettings)) return;

        bool logMatched = false;
        EnterCriticalSection(&g_stateLock);
        ProportionalState& state = g_proportionalStates[stats];
        if (state.sourceId != identity.stringId)
        {
            state = ProportionalState();
            state.sourceId = identity.stringId;
        }
        ++state.labourTicks;
        if (!state.matchedLogged)
        {
            state.matchedLogged = true;
            state.lastXpSummaryTick = GetTickCount();
            logMatched = true;
        }

        GrantConfiguredStat(stats, STAT_STRENGTH, "Strength",
            sourceSettings.strengthMultiplier, sourceSettings.strengthCap,
            state.strengthAccumulator, state.strengthTicks, state.strengthCapLogged);
        GrantConfiguredStat(stats, STAT_TOUGHNESS, "Toughness",
            sourceSettings.toughnessMultiplier, sourceSettings.toughnessCap,
            state.toughnessAccumulator, state.toughnessTicks, state.toughnessCapLogged);
        GrantConfiguredStat(stats, STAT_DEXTERITY, "Dexterity",
            sourceSettings.dexterityMultiplier, sourceSettings.dexterityCap,
            state.dexterityAccumulator, state.dexterityTicks, state.dexterityCapLogged);

        // RC4 validation: all known combat skills use the same data-driven engine.
        // Each source controls XP multiplier and teacher-ratio cap independently.
        for (int skillIndex = 0; skillIndex < PARTNER_SKILL_COUNT; ++skillIndex)
        {
            GrantPartnerTrainingXp(
                stats,
                kPartnerSkills[skillIndex].stat,
                kPartnerSkills[skillIndex].name,
                GetSnapshotPartnerSkill(identity.highestPartnerCombatSkills, skillIndex),
                sourceSettings.partnerTeacherRatio[skillIndex],
                sourceSettings.partnerXpMultiplier[skillIndex],
                state.partnerAccumulators[skillIndex],
                state.partnerTicks[skillIndex],
                state.partnerStartedLogged[skillIndex],
                state.partnerBlockedLogged[skillIndex],
                useTrainingPath,
                trainingTime,
                trainingMult);
        }

        const DWORD now = GetTickCount();
        bool logSummary = false;
        unsigned __int64 intervalLabour = 0, intervalStrength = 0, intervalToughness = 0, intervalDexterity = 0;
        unsigned __int64 intervalPartnerTicks[PARTNER_SKILL_COUNT] = {0};
        if (now - state.lastXpSummaryTick >= g_config.xpSummaryIntervalMs)
        {
            state.lastXpSummaryTick = now;
            intervalLabour = state.labourTicks - state.lastSummaryLabourTicks;
            intervalStrength = state.strengthTicks - state.lastSummaryStrengthTicks;
            intervalToughness = state.toughnessTicks - state.lastSummaryToughnessTicks;
            intervalDexterity = state.dexterityTicks - state.lastSummaryDexterityTicks;
            for (int skillIndex = 0; skillIndex < PARTNER_SKILL_COUNT; ++skillIndex)
            {
                intervalPartnerTicks[skillIndex] = state.partnerTicks[skillIndex] - state.lastSummaryPartnerTicks[skillIndex];
                state.lastSummaryPartnerTicks[skillIndex] = state.partnerTicks[skillIndex];
            }
            logSummary = true;
        }
        const unsigned __int64 totalLabour = state.labourTicks;
        const unsigned __int64 totalStrength = state.strengthTicks;
        const unsigned __int64 totalToughness = state.toughnessTicks;
        const unsigned __int64 totalDexterity = state.dexterityTicks;
        LeaveCriticalSection(&g_stateLock);

        if (logMatched && ReserveLogEntry())
        {
            std::ostringstream message;
            message << "[" << kPluginName << "] [INFO] Source matched"
                    << " StringID=\"" << identity.stringId << "\""
                    << " DisplayName=\"" << identity.displayName << "\""
                    << " TrainingProfile=\"" << sourceSettings.trainingProfile << "\""
                    << " OperatorCount=" << identity.operatorCount;
            DebugLog(message.str());
        }

        if (g_config.detailedLogging && logMatched && ReserveLogEntry())
        {
            std::ostringstream message;
            message << "[" << kPluginName << "] [DEBUG] Source details"
                    << " Character=0x" << std::hex << reinterpret_cast<uintptr_t>(stats->me) << std::dec
                    << " StringID=\"" << identity.stringId << "\""
                    << " DisplayName=\"" << identity.displayName << "\""
                    << " Strength=" << sourceSettings.strengthMultiplier << "/" << sourceSettings.strengthCap
                    << " Toughness=" << sourceSettings.toughnessMultiplier << "/" << sourceSettings.toughnessCap
                    << " Dexterity=" << sourceSettings.dexterityMultiplier << "/" << sourceSettings.dexterityCap;
            message << " TrainingProfile=\"" << sourceSettings.trainingProfile << "\"";
            message << " PartnerConfig={";
            for (int skillIndex = 0; skillIndex < PARTNER_SKILL_COUNT; ++skillIndex)
            {
                if (skillIndex != 0) message << ",";
                message << kPartnerSkills[skillIndex].name << "="
                        << sourceSettings.partnerXpMultiplier[skillIndex] << "/"
                        << sourceSettings.partnerTeacherRatio[skillIndex];
            }
            message << "}"
                    << " OperatorCount=" << identity.operatorCount
                    << " IteratorHasEntry=" << (identity.operatorIteratorHasEntry ? "true" : "false")
                    << " FirstHandType=" << identity.firstOperatorType
                    << " FirstHandContainer=" << identity.firstOperatorContainer
                    << " FirstHandContainerSerial=" << identity.firstOperatorContainerSerial
                    << " FirstHandIndex=" << identity.firstOperatorIndex
                    << " FirstHandSerial=" << identity.firstOperatorSerial
                    << " FirstHandCharacterResolved="
                    << (identity.firstOperatorCharacterResolved ? "true" : "false")
                    << " ResolvedOperators=" << identity.resolvedOperatorCharacters
                    << " CurrentCharacterFound="
                    << (identity.currentCharacterFoundInOperators ? "true" : "false")
                    << " HighestOperatorLabouring=" << identity.highestOperatorLabouring
                    << identity.operatorLabouringSummary;
            AppendCombatSkills(message, "CurrentCombat", identity.currentCombatSkills);
            AppendCombatSkills(message, "HighestOperatorCombat",
                               identity.highestOperatorCombatSkills);
            AppendCombatSkills(message, "HighestPartnerCombat",
                               identity.highestPartnerCombatSkills);
            message << " ResolvedPartners=" << identity.resolvedPartnerCharacters;
            AppendAllFixedDynamicCapPreviews(
                message,
                identity.currentCombatSkills,
                identity.highestPartnerCombatSkills);
            message << identity.operatorCombatSummary;
            DebugLog(message.str());
        }

        if (g_config.detailedLogging && logSummary && ReserveLogEntry())
        {
            std::ostringstream message;
            message << "[" << kPluginName << "] [INFO] XP summary"
                    << " Character=0x" << std::hex << reinterpret_cast<uintptr_t>(stats->me) << std::dec
                    << " StringID=\"" << identity.stringId << "\""
                    << " Interval={Labour=" << intervalLabour
                    << ",Strength=" << intervalStrength
                    << ",Toughness=" << intervalToughness
                    << ",Dexterity=" << intervalDexterity << "}"
                    << " Total={Labour=" << totalLabour
                    << ",Strength=" << totalStrength
                    << ",Toughness=" << totalToughness
                    << ",Dexterity=" << totalDexterity << "}"
                    << " PartnerTicks={";
            for (int skillIndex = 0; skillIndex < PARTNER_SKILL_COUNT; ++skillIndex)
            {
                if (skillIndex != 0) message << ",";
                message << kPartnerSkills[skillIndex].name << "=" << intervalPartnerTicks[skillIndex];
            }
            message << "}"
                    << " Values={Strength=" << GetBaseStatValue(stats, STAT_STRENGTH)
                    << ",Toughness=" << GetBaseStatValue(stats, STAT_TOUGHNESS)
                    << ",Dexterity=" << GetBaseStatValue(stats, STAT_DEXTERITY)
                    << ",Attack=" << GetBaseStatValue(stats, STAT_MELEE_ATTACK)
                    << ",Defence=" << GetBaseStatValue(stats, STAT_MELEE_DEFENCE)
                    << ",Dodge=" << GetBaseStatValue(stats, STAT_DODGE)
                    << ",MartialArts=" << GetBaseStatValue(stats, STAT_MARTIALARTS)
                    << ",Katanas=" << GetBaseStatValue(stats, STAT_KATANAS)
                    << ",Sabres=" << GetBaseStatValue(stats, STAT_SABRES)
                    << ",Hackers=" << GetBaseStatValue(stats, STAT_HACKERS)
                    << ",HeavyWeapons=" << GetBaseStatValue(stats, STAT_HEAVYWEAPONS)
                    << ",Blunt=" << GetBaseStatValue(stats, STAT_BLUNT)
                    << ",Polearms=" << GetBaseStatValue(stats, STAT_POLEARMS)
                    << ",Crossbows=" << GetBaseStatValue(stats, STAT_CROSSBOWS)
                    << ",PrecisionShooting=" << GetBaseStatValue(stats, STAT_FRIENDLY_FIRE)
                    << ",Perception=" << GetBaseStatValue(stats, STAT_PERCEPTION)
                    << ",Turrets=" << GetBaseStatValue(stats, STAT_TURRETS) << "}";
            DebugLog(message.str());
        }
    }

    void HookXpStatTimeBased(CharStats* stats, StatsEnumerated stat)
    {
        if (stat == STAT_LABOURING)
        {
            InterlockedIncrement(&g_timeBasedLabouringCalls);
            if (g_config.detailedLogging && g_config.logLabouringXp)
            {
                ObserveLabouringXp(stats, _ReturnAddress());
            }
        }
        if (g_originalXpStatTimeBased)
        {
            g_originalXpStatTimeBased(stats, stat);
        }
        // Mining sources continue to use Labouring as their trigger.
        // BF_TRAINING equipment uses Melee Attack as its reliable time-based trigger.
        // Registered SourceData decides whether the current equipment actually grants anything.
        if (stat == STAT_LABOURING || stat == STAT_MELEE_ATTACK)
        {
            ApplyProportionalMultiXp(stats, false, 0.0f, 0.0f);
        }
    }

    void HookXpStatEventBased(CharStats* stats, StatsEnumerated stat, float amount)
    {
        if (stat == STAT_LABOURING)
        {
            InterlockedIncrement(&g_eventBasedLabouringCalls);
            if (g_config.detailedLogging && g_config.logLabouringXp)
            {
                ObserveLabouringXp(stats, _ReturnAddress());
            }
        }
        if (g_originalXpStatEventBased)
        {
            g_originalXpStatEventBased(stats, stat, amount);
        }
    }

    void HookXpTraining(
        CharStats* stats,
        float time,
        float mult,
        float& statValue,
        float upperLimit,
        StatsEnumerated stat)
    {
        if (g_originalXpTraining)
        {
            g_originalXpTraining(stats, time, mult, statValue, upperLimit, stat);
        }

        // Partner Training uses Kenshi's dedicated training path.
        // SourceData decides whether the current equipment grants partner skills.
        ApplyProportionalMultiXp(stats, true, time, mult);
    }

    bool InstallObservationHooks()
    {
        const intptr_t timeBasedAddress = KenshiLib::GetRealAddress(&CharStats::xpStat_timeBased);
        const intptr_t eventBasedAddress = KenshiLib::GetRealAddress(&CharStats::xpStat_eventBased);
        const intptr_t trainingAddress = KenshiLib::GetRealAddress(&CharStats::xpTraining);

        const KenshiLib::HookStatus timeStatus = KenshiLib::AddHook(
            timeBasedAddress,
            reinterpret_cast<void*>(&HookXpStatTimeBased),
            &g_originalXpStatTimeBased);

        const KenshiLib::HookStatus eventStatus = KenshiLib::AddHook(
            eventBasedAddress,
            reinterpret_cast<void*>(&HookXpStatEventBased),
            &g_originalXpStatEventBased);

        const KenshiLib::HookStatus trainingStatus = KenshiLib::AddHook(
            trainingAddress,
            reinterpret_cast<void*>(&HookXpTraining),
            reinterpret_cast<void**>(&g_originalXpTraining));

        std::ostringstream message;
        message << "[" << kPluginName << "] [INFO] XP hooks:"
                << " timeBased=" << (timeStatus == KenshiLib::SUCCESS ? "OK" : "FAILED")
                << " @0x" << std::hex << timeBasedAddress
                << ", eventBased=" << (eventStatus == KenshiLib::SUCCESS ? "OK" : "FAILED")
                << " @0x" << eventBasedAddress
                << ", xpTraining=" << (trainingStatus == KenshiLib::SUCCESS ? "OK" : "FAILED")
                << " @0x" << trainingAddress;
        DebugLog(message.str());

        return timeStatus == KenshiLib::SUCCESS ||
               eventStatus == KenshiLib::SUCCESS ||
               trainingStatus == KenshiLib::SUCCESS;
    }

    void FlushSessions(const char* reason)
    {
        if (!g_stateLockInitialised)
        {
            return;
        }

        EnterCriticalSection(&g_stateLock);
        for (std::map<SessionKey, SessionState>::const_iterator it = g_sessions.begin();
             it != g_sessions.end(); ++it)
        {
            if (it->second.startTick != 0)
            {
                LogSessionLine(reason, it->first, it->second, it->second.lastCallTick);
            }
        }
        LeaveCriticalSection(&g_stateLock);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        InitializeCriticalSection(&g_stateLock);
        g_stateLockInitialised = true;
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (g_stateLockInitialised)
        {
            DeleteCriticalSection(&g_stateLock);
            g_stateLockInitialised = false;
        }
    }
    return TRUE;
}

__declspec(dllexport) void startPlugin()
{
    DebugLog(std::string("[") + kPluginName + "] [INFO] Starting Partner Training version " + kPluginVersion);
    LoadConfig();

    if (!g_config.enabled)
    {
        DebugLog(std::string("[") + kPluginName + "] [INFO] Disabled by INI. No hooks installed.");
        return;
    }

    if (InstallObservationHooks())
    {
        DebugLog(std::string("[") + kPluginName + "] [INFO] Framework initialized.");
        DebugLog(std::string("[") + kPluginName + "] [INFO] FrameworkVersion=1, PartnerTrainingVersion=1.0.0-rc6, ConfigVersion=1, SourceDataVersion=1.");
    DebugLog(std::string("[") + kPluginName + "] [INFO] BuildInfo: BuildDate=2026-07-26, Toolset=Visual Studio 2010 v100, Target=x64, TestedRE_Kenshi=0.3.4, TestedKenshiLib=0.4.0.");
        DebugLog(std::string("[") + kPluginName + "] [INFO] TrainingProfiles define reusable skill sets; SourceData maps equipment StringIDs and may override profile values.");
    }
    else
    {
        DebugLog(std::string("[") + kPluginName + "] [ERROR] No supported XP hook could be installed.");
    }
}

__declspec(dllexport) void stopPlugin()
{
    FlushSessions("STOP");

    std::ostringstream message;
    message << "[" << kPluginName << "] Stopping observation."
            << " TimeBasedLabouringCalls=" << g_timeBasedLabouringCalls
            << ", EventBasedLabouringCalls=" << g_eventBasedLabouringCalls
            << ", WrittenEntries=" << g_writtenEntries
            << ", StrengthGrantCalls=" << g_strengthGrantCalls
            << ", SuppressedEntries=" << g_suppressedEntries
            << ". Hooks remain installed until process exit.";
    DebugLog(message.str());
}
