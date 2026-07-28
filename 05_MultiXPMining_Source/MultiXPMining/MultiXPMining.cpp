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
    const char* const kPluginName = "MultiXPMining";
    const char* const kPluginVersion = "1.0.0-rc3";
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
              defaultStrengthMultiplier(0.25f), defaultStrengthCap(15.0f),
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
              lastSummaryToughnessTicks(0), lastSummaryDexterityTicks(0) {}
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

        SourceSettings()
            : enabled(false), strengthMultiplier(0.0f), strengthCap(0.0f),
              toughnessMultiplier(0.0f), toughnessCap(0.0f),
              dexterityMultiplier(0.0f), dexterityCap(0.0f) {}
    };

    struct UseableIdentity
    {
        bool valid;
        std::string displayName;
        std::string stringId;
        int dataId;
        int useableStat;

        UseableIdentity()
            : valid(false), dataId(0), useableStat(static_cast<int>(STAT_NONE)) {}
    };

    typedef void (*XpStatTimeBasedFn)(CharStats* stats, StatsEnumerated stat);
    typedef void (*XpStatEventBasedFn)(CharStats* stats, StatsEnumerated stat, float amount);

    HMODULE g_module = NULL;
    Config g_config;
    XpStatTimeBasedFn g_originalXpStatTimeBased = NULL;
    XpStatEventBasedFn g_originalXpStatEventBased = NULL;

    CRITICAL_SECTION g_stateLock;
    bool g_stateLockInitialised = false;
    std::map<SessionKey, SessionState> g_sessions;
    std::map<CharStats*, ProportionalState> g_proportionalStates;
    std::map<std::string, SourceSettings> g_sourceSettings;
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

        identity.valid = true;
        identity.displayName = building->getName();
        identity.stringId = data->stringID;
        identity.dataId = data->id;
        identity.useableStat = static_cast<int>(useable->getStatUsed());
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

    void WriteUnknownSource(const UseableIdentity& identity)
    {
        if (!g_config.generateUnknownSources || identity.stringId.empty()) return;

        EnterCriticalSection(&g_stateLock);
        const bool alreadyKnown = g_unknownSourcesWritten.find(identity.stringId) != g_unknownSourcesWritten.end();
        if (!alreadyKnown) g_unknownSourcesWritten.insert(identity.stringId);
        LeaveCriticalSection(&g_stateLock);
        if (alreadyKnown) return;

        const std::string path = GetModuleDirectory() + "\\UnknownSources.ini";
        std::ofstream out(path.c_str(), std::ios::out | std::ios::app | std::ios::binary);
        if (!out) return;
        out << "\r\n; ================================================================\r\n";
        out << "; Multi XP - Mining: unknown source detected\r\n";
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

    bool GetMiningSourceSettings(const UseableIdentity& identity, SourceSettings& settings)
    {
        settings = SourceSettings();
        if (!identity.valid || identity.useableStat != static_cast<int>(STAT_LABOURING)) return false;
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
        const std::string iniPath = GetModuleDirectory() + "\\MultiXPMining.ini";
        g_config.configVersion = ReadUInt("General", "ConfigVersion", 1, 1, 1000, iniPath);
        g_config.enabled = ReadBool("General", "Enabled", true, iniPath);
        g_config.globalXpMultiplier = ReadFloat("General", "GlobalXpMultiplier", 1.0f, 0.0f, 100.0f, iniPath);
        g_config.globalCapMultiplier = ReadFloat("General", "GlobalCapMultiplier", 1.0f, 0.0f, 10.0f, iniPath);
        g_config.enableCaps = ReadBool("General", "EnableCaps", true, iniPath);
        g_config.loggingEnabled = ReadBool("Logging", "Enabled", true, iniPath);
        g_config.detailedLogging = ReadBool("Logging", "DetailedLogging", false, iniPath);
        g_config.logLabouringXp = ReadBool("Logging", "LogLabouringXp", true, iniPath);
        g_config.summaryIntervalMs = ReadUInt("Logging", "SummaryIntervalMs", 5000, 250, 600000, iniPath);
        g_config.sessionGapMs = ReadUInt("Logging", "SessionGapMs", 1500, 100, 600000, iniPath);
        g_config.maximumLogEntries = ReadUInt("Logging", "MaximumLogEntries", 500, 1, 100000, iniPath);
        g_config.defaultStrengthMultiplier = ReadFloat("Defaults", "StrengthMultiplier", 0.25f, 0.0f, 100.0f, iniPath);
        g_config.defaultStrengthCap = ReadFloat("Defaults", "StrengthCap", 15.0f, 0.0f, 1000.0f, iniPath);
        g_config.defaultToughnessMultiplier = ReadFloat("Defaults", "ToughnessMultiplier", 0.0f, 0.0f, 100.0f, iniPath);
        g_config.defaultToughnessCap = ReadFloat("Defaults", "ToughnessCap", 0.0f, 0.0f, 1000.0f, iniPath);
        g_config.defaultDexterityMultiplier = ReadFloat("Defaults", "DexterityMultiplier", 0.0f, 0.0f, 100.0f, iniPath);
        g_config.defaultDexterityCap = ReadFloat("Defaults", "DexterityCap", 0.0f, 0.0f, 1000.0f, iniPath);
        g_config.generateUnknownSources = ReadBool("General", "GenerateUnknownSourcesIni", true, iniPath);
        g_config.xpSummaryIntervalMs = ReadUInt("Logging", "XpSummaryIntervalMs", 5000, 500, 600000, iniPath);

        std::ostringstream message;
        message << "[" << kPluginName << "] [INFO] Config loaded: ConfigVersion="
                << g_config.configVersion
                << ", Enabled="
                << (g_config.enabled ? "true" : "false")
                << ", GlobalXpMultiplier=" << g_config.globalXpMultiplier
                << ", GlobalCapMultiplier=" << g_config.globalCapMultiplier
                << ", EnableCaps=" << (g_config.enableCaps ? "true" : "false")
                << ", DefaultStrengthMultiplier=" << g_config.defaultStrengthMultiplier
                << ", DefaultStrengthCap=" << g_config.defaultStrengthCap
                << ", DefaultToughnessMultiplier=" << g_config.defaultToughnessMultiplier
                << ", DefaultToughnessCap=" << g_config.defaultToughnessCap
                << ", DefaultDexterityMultiplier=" << g_config.defaultDexterityMultiplier
                << ", DefaultDexterityCap=" << g_config.defaultDexterityCap
                << ", DetailedLogging=" << (g_config.detailedLogging ? "true" : "false")
                << ", LogLabouringXp=" << (g_config.logLabouringXp ? "true" : "false")
                << ", SummaryIntervalMs=" << g_config.summaryIntervalMs
                << ", SessionGapMs=" << g_config.sessionGapMs
                << ", GenerateUnknownSourcesIni=" << (g_config.generateUnknownSources ? "true" : "false")
                << ", XpSummaryIntervalMs=" << g_config.xpSummaryIntervalMs;
        DebugLog(message.str());
        if (g_config.configVersion != kSupportedConfigVersion && ReserveLogEntry())
        {
            std::ostringstream warning;
            warning << "[" << kPluginName << "] [WARNING] Config version mismatch"
                    << " Found=" << g_config.configVersion
                    << " Supported=" << kSupportedConfigVersion
                    << ". Unknown options will use safe defaults.";
            DebugLog(warning.str());
        }
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

    void ApplyProportionalMultiXp(CharStats* stats)
    {
        if (!g_config.enabled || !stats || !stats->me ||
            !g_originalXpStatTimeBased || !g_stateLockInitialised) return;

        UseableIdentity identity;
        SourceSettings sourceSettings;
        if (!TryGetCurrentUseableIdentity(stats->me, identity) ||
            !GetMiningSourceSettings(identity, sourceSettings)) return;

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

        const DWORD now = GetTickCount();
        bool logSummary = false;
        unsigned __int64 intervalLabour = 0, intervalStrength = 0, intervalToughness = 0, intervalDexterity = 0;
        if (now - state.lastXpSummaryTick >= g_config.xpSummaryIntervalMs)
        {
            state.lastXpSummaryTick = now;
            intervalLabour = state.labourTicks - state.lastSummaryLabourTicks;
            intervalStrength = state.strengthTicks - state.lastSummaryStrengthTicks;
            intervalToughness = state.toughnessTicks - state.lastSummaryToughnessTicks;
            intervalDexterity = state.dexterityTicks - state.lastSummaryDexterityTicks;
            state.lastSummaryLabourTicks = state.labourTicks;
            state.lastSummaryStrengthTicks = state.strengthTicks;
            state.lastSummaryToughnessTicks = state.toughnessTicks;
            state.lastSummaryDexterityTicks = state.dexterityTicks;
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
                    << " Character=0x" << std::hex << reinterpret_cast<uintptr_t>(stats->me) << std::dec
                    << " StringID=\"" << identity.stringId << "\""
                    << " DisplayName=\"" << identity.displayName << "\""
                    << " Strength=" << sourceSettings.strengthMultiplier << "/" << sourceSettings.strengthCap
                    << " Toughness=" << sourceSettings.toughnessMultiplier << "/" << sourceSettings.toughnessCap
                    << " Dexterity=" << sourceSettings.dexterityMultiplier << "/" << sourceSettings.dexterityCap;
            DebugLog(message.str());
        }

        if (logSummary && ReserveLogEntry())
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
                    << " Values={Strength=" << GetBaseStatValue(stats, STAT_STRENGTH)
                    << ",Toughness=" << GetBaseStatValue(stats, STAT_TOUGHNESS)
                    << ",Dexterity=" << GetBaseStatValue(stats, STAT_DEXTERITY) << "}";
            DebugLog(message.str());
        }
    }

    void HookXpStatTimeBased(CharStats* stats, StatsEnumerated stat)
    {
        if (stat == STAT_LABOURING)
        {
            InterlockedIncrement(&g_timeBasedLabouringCalls);
            if (g_config.detailedLogging)
            {
                ObserveLabouringXp(stats, _ReturnAddress());
            }
        }
        if (g_originalXpStatTimeBased)
        {
            g_originalXpStatTimeBased(stats, stat);
        }
        if (stat == STAT_LABOURING)
        {
            ApplyProportionalMultiXp(stats);
        }
    }

    void HookXpStatEventBased(CharStats* stats, StatsEnumerated stat, float amount)
    {
        if (stat == STAT_LABOURING)
        {
            InterlockedIncrement(&g_eventBasedLabouringCalls);
            ObserveLabouringXp(stats, _ReturnAddress());
        }
        if (g_originalXpStatEventBased)
        {
            g_originalXpStatEventBased(stats, stat, amount);
        }
    }

    bool InstallObservationHooks()
    {
        const intptr_t timeBasedAddress = KenshiLib::GetRealAddress(&CharStats::xpStat_timeBased);
        const intptr_t eventBasedAddress = KenshiLib::GetRealAddress(&CharStats::xpStat_eventBased);

        const KenshiLib::HookStatus timeStatus = KenshiLib::AddHook(
            timeBasedAddress,
            reinterpret_cast<void*>(&HookXpStatTimeBased),
            &g_originalXpStatTimeBased);

        const KenshiLib::HookStatus eventStatus = KenshiLib::AddHook(
            eventBasedAddress,
            reinterpret_cast<void*>(&HookXpStatEventBased),
            &g_originalXpStatEventBased);

        std::ostringstream message;
        message << "[" << kPluginName << "] [INFO] XP hooks:"
                << " timeBased=" << (timeStatus == KenshiLib::SUCCESS ? "OK" : "FAILED")
                << " @0x" << std::hex << timeBasedAddress
                << ", eventBased=" << (eventStatus == KenshiLib::SUCCESS ? "OK" : "FAILED")
                << " @0x" << eventBasedAddress;
        DebugLog(message.str());

        return timeStatus == KenshiLib::SUCCESS || eventStatus == KenshiLib::SUCCESS;
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
    DebugLog(std::string("[") + kPluginName + "] [INFO] Starting Multi XP - Mining version " + kPluginVersion);
    LoadConfig();

    if (!g_config.enabled)
    {
        DebugLog(std::string("[") + kPluginName + "] [INFO] Disabled by INI. No hooks installed.");
        return;
    }

    if (InstallObservationHooks())
    {
        DebugLog(std::string("[") + kPluginName + "] [INFO] Framework initialized.");
        DebugLog(std::string("[") + kPluginName + "] [INFO] FrameworkVersion=1, MiningVersion=1.0.0-rc3, ConfigVersion=1, SourceDataVersion=1.");
    DebugLog(std::string("[") + kPluginName + "] [INFO] BuildInfo: BuildDate=2026-07-25, Toolset=Visual Studio 2010 v100, Target=x64, TestedRE_Kenshi=0.3.4, TestedKenshiLib=0.4.0.");
        DebugLog(std::string("[") + kPluginName + "] [INFO] SourceData controls supported equipment, XP multipliers, and skill caps.");
    }
    else
    {
        DebugLog(std::string("[") + kPluginName + "] [ERROR] No Labouring XP hook could be installed.");
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
