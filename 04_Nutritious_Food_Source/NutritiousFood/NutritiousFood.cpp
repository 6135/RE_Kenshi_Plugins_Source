#include <Windows.h>

#include <Debug.h>
#include <core/Functions.h>

#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/Enums.h>
#include <kenshi/GameData.h>
#include <kenshi/GameDataManager.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/RootObject.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <float.h>
#include <vector>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace
{
    const char* const kPluginName = "NutritiousFood";
    const char* const kPluginVersion = "1.0.0-rc2";
    const char* const kIngredientsListName = "ingredients";
    const char* const kBasePrefix = "BaseIngredient.";

    typedef std::map<std::string, double> AttributeMap;
    typedef std::map<std::string, AttributeMap> BaseIngredientMap;
typedef std::map<std::string, std::map<std::string, std::string> > AttributeSourceMap;
typedef std::map<std::string, AttributeMap> NutritionCache;

    struct ExperienceEffect
    {
        bool enabled;
        StatsEnumerated stat;
        std::string statName;
        double multiplier;
        double maximumXpPerMeal;

        ExperienceEffect()
            : enabled(false), stat(STAT_STRENGTH), multiplier(0.0), maximumXpPerMeal(0.0) {}
    };

    typedef std::map<std::string, ExperienceEffect> ExperienceEffectMap;

    struct Config
    {
        bool enabled;
        bool logEatCalls;
        bool logTree;
        bool logUnknownTerminalIngredients;
        bool calculateOnlyOncePerSid;
        bool affectAnimals;
        bool enableNutritionCache;
        bool logDataLoading;
        bool logBalanceInfo;
        int maximumDepth;
        int maximumIngredientsPerItem;
        double ratioDivisor;
        double globalExperienceMultiplier;

        Config()
            : enabled(true), logEatCalls(true), logTree(true),
              logUnknownTerminalIngredients(true), calculateOnlyOncePerSid(true),
              affectAnimals(true), enableNutritionCache(true), logDataLoading(true), logBalanceInfo(false),
              maximumDepth(16), maximumIngredientsPerItem(64), ratioDivisor(100.0),
              globalExperienceMultiplier(0.10) {}
    };

    Config g_config;
    HMODULE g_module = NULL;
    std::string g_iniPath;
    BaseIngredientMap g_baseIngredients;
    AttributeSourceMap g_attributeSources;
    ExperienceEffectMap g_experienceEffects;
    NutritionCache g_nutritionCache;
    std::set<std::string> g_calculatedSids;

    typedef bool (*EatItemFn)(Character* self, Item* food, Inventory* from);
    EatItemFn g_originalEatItem = NULL;

    std::string Trim(const std::string& input)
    {
        std::string::size_type first = 0;
        while (first < input.size() && std::isspace(static_cast<unsigned char>(input[first]))) ++first;
        std::string::size_type last = input.size();
        while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1]))) --last;
        return input.substr(first, last - first);
    }

    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return value;
    }

    std::string GetModuleDirectory()
    {
        char path[MAX_PATH] = { 0 };
        const DWORD length = GetModuleFileNameA(g_module, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return ".";
        std::string result(path, length);
        const std::string::size_type separator = result.find_last_of("\\/");
        return separator == std::string::npos ? "." : result.substr(0, separator);
    }

    std::string ReadString(const char* section, const char* key, const char* fallback)
    {
        char buffer[512] = { 0 };
        GetPrivateProfileStringA(section, key, fallback, buffer, sizeof(buffer), g_iniPath.c_str());
        return std::string(buffer);
    }

    bool ReadBool(const char* section, const char* key, bool defaultValue)
    {
        std::string value = ToLower(Trim(ReadString(section, key, defaultValue ? "true" : "false")));
        if (value == "true" || value == "yes" || value == "on" || value == "1") return true;
        if (value == "false" || value == "no" || value == "off" || value == "0") return false;
        return defaultValue;
    }

    int ReadInt(const char* section, const char* key, int defaultValue)
    {
        std::ostringstream fallback;
        fallback << defaultValue;
        const std::string value = ReadString(section, key, fallback.str().c_str());
        char* end = NULL;
        const long parsed = std::strtol(value.c_str(), &end, 10);
        return end != value.c_str() ? static_cast<int>(parsed) : defaultValue;
    }

    double ReadDouble(const char* section, const char* key, double defaultValue)
    {
        std::ostringstream fallback;
        fallback << defaultValue;
        const std::string value = ReadString(section, key, fallback.str().c_str());
        char* end = NULL;
        const double parsed = std::strtod(value.c_str(), &end);
        return end != value.c_str() ? parsed : defaultValue;
    }

    bool ParseNumber(const std::string& text, double& output)
    {
        const std::string value = Trim(text);
        if (value.empty()) return false;
        char* end = NULL;
        const double parsed = std::strtod(value.c_str(), &end);
        if (end == value.c_str()) return false;
        while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
        if (*end != '\0') return false;
        output = parsed;
        return true;
    }

    bool TryParseStat(const std::string& text, StatsEnumerated& stat, std::string& canonicalName)
    {
        const std::string value = ToLower(Trim(text));
        if (value == "strength") { stat = STAT_STRENGTH; canonicalName = "Strength"; return true; }
        if (value == "dexterity") { stat = STAT_DEXTERITY; canonicalName = "Dexterity"; return true; }
        if (value == "toughness") { stat = STAT_TOUGHNESS; canonicalName = "Toughness"; return true; }
        if (value == "perception") { stat = STAT_PERCEPTION; canonicalName = "Perception"; return true; }
        return false;
    }

    void ParseExperienceEffectSections()
    {
        g_experienceEffects.clear();
        std::ifstream input(g_iniPath.c_str());
        if (!input)
        {
            ErrorLog(std::string("[") + kPluginName + "] Could not open INI for effect parsing: " + g_iniPath);
            return;
        }

        const std::string prefix = "Effect.";
        std::string currentAttribute;
        std::map<std::string, std::map<std::string, std::string> > raw;
        std::string line;
        while (std::getline(input, line))
        {
            if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
            const std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;
            if (trimmed[0] == '[' && trimmed[trimmed.size() - 1] == ']')
            {
                const std::string section = Trim(trimmed.substr(1, trimmed.size() - 2));
                if (section.compare(0, prefix.size(), prefix) == 0)
                    currentAttribute = Trim(section.substr(prefix.size()));
                else
                    currentAttribute.clear();
                continue;
            }
            if (currentAttribute.empty()) continue;
            const std::string::size_type equals = trimmed.find('=');
            if (equals == std::string::npos) continue;
            const std::string key = ToLower(Trim(trimmed.substr(0, equals)));
            std::string value = Trim(trimmed.substr(equals + 1));
            const std::string::size_type semicolon = value.find(';');
            if (semicolon != std::string::npos) value = Trim(value.substr(0, semicolon));
            raw[currentAttribute][key] = value;
        }

        for (std::map<std::string, std::map<std::string, std::string> >::const_iterator it = raw.begin(); it != raw.end(); ++it)
        {
            ExperienceEffect effect;
            const std::map<std::string, std::string>& values = it->second;
            std::map<std::string, std::string>::const_iterator typeIt = values.find("type");
            if (typeIt != values.end() && ToLower(Trim(typeIt->second)) != "experience") continue;
            std::map<std::string, std::string>::const_iterator enabledIt = values.find("enabled");
            effect.enabled = enabledIt == values.end() || ToLower(Trim(enabledIt->second)) != "false";
            std::map<std::string, std::string>::const_iterator statIt = values.find("stat");
            if (statIt == values.end() || !TryParseStat(statIt->second, effect.stat, effect.statName))
            {
                ErrorLog(std::string("[") + kPluginName + ":EffectConfig] Attribute=\"" + it->first + "\" ignored: unsupported or missing Stat.");
                continue;
            }
            std::map<std::string, std::string>::const_iterator multIt = values.find("multiplier");
            if (multIt != values.end()) ParseNumber(multIt->second, effect.multiplier);
            std::map<std::string, std::string>::const_iterator capIt = values.find("maximumxppermeal");
            if (capIt != values.end()) ParseNumber(capIt->second, effect.maximumXpPerMeal);
            g_experienceEffects[it->first] = effect;

            std::ostringstream entry;
            entry << "[" << kPluginName << ":EffectConfig] Attribute=\"" << it->first
                  << "\" Enabled=" << (effect.enabled ? "true" : "false")
                  << " Stat=\"" << effect.statName << "\" Multiplier=" << effect.multiplier
                  << " MaximumXPPerMeal=" << effect.maximumXpPerMeal;
            DebugLog(entry.str());
        }
    }

    std::string ParentDirectory(const std::string& path)
    {
        const std::string::size_type separator = path.find_last_of("\\/");
        return separator == std::string::npos ? "." : path.substr(0, separator);
    }

    std::string FileNameFromPath(const std::string& path)
    {
        const std::string::size_type separator = path.find_last_of("\\/");
        return separator == std::string::npos ? path : path.substr(separator + 1);
    }

    bool DirectoryExists(const std::string& path)
    {
        const DWORD attributes = GetFileAttributesA(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    std::string FindModsDirectory(const std::string& moduleDirectory)
    {
        std::string current = moduleDirectory;
        for (int depth = 0; depth < 8; ++depth)
        {
            if (ToLower(FileNameFromPath(current)) == "mods") return current;
            const std::string parent = ParentDirectory(current);
            if (parent == current || parent.empty()) break;
            current = parent;
        }

        const std::string fallback = ParentDirectory(moduleDirectory);
        return fallback;
    }

    bool DataPackPathLess(const std::string& left, const std::string& right)
    {
        const std::string leftName = ToLower(FileNameFromPath(left));
        const std::string rightName = ToLower(FileNameFromPath(right));
        if (leftName != rightName) return leftName < rightName;
        return ToLower(left) < ToLower(right);
    }

    bool FileExists(const std::string& path)
    {
        const DWORD attributes = GetFileAttributesA(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    void CollectIniFiles(const std::string& directory, std::vector<std::string>& files)
    {
        if (!DirectoryExists(directory)) return;

        WIN32_FIND_DATAA data;
        const std::string pattern = directory + "\\*.ini";
        HANDLE handle = FindFirstFileA(pattern.c_str(), &data);
        if (handle == INVALID_HANDLE_VALUE) return;
        do
        {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                files.push_back(directory + "\\" + data.cFileName);
        } while (FindNextFileA(handle, &data));
        FindClose(handle);
    }

    void CollectDataPackFiles(std::vector<std::string>& files)
    {
        files.clear();
        std::set<std::string> uniqueFiles;
        std::vector<std::string> discovered;

        const std::string moduleDirectory = GetModuleDirectory();
        const std::string localDataDirectory = moduleDirectory + "\\NutritionData";
        CollectIniFiles(localDataDirectory, discovered);

        const std::string modsDirectory = FindModsDirectory(moduleDirectory);
        if (g_config.logDataLoading)
        {
            DebugLog(std::string("[") + kPluginName + ":DataSearchRoot] ModuleDirectory=\"" + moduleDirectory
                     + "\" ModsDirectory=\"" + modsDirectory + "\"");
        }

        WIN32_FIND_DATAA modData;
        HANDLE modHandle = FindFirstFileA((modsDirectory + "\\*").c_str(), &modData);
        if (modHandle == INVALID_HANDLE_VALUE)
        {
            ErrorLog(std::string("[") + kPluginName + ":DataWarning] Could not enumerate mods directory: " + modsDirectory);
        }
        else
        {
            do
            {
                if ((modData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
                const std::string name = modData.cFileName;
                if (name == "." || name == "..") continue;

                const std::string dataDirectory = modsDirectory + "\\" + name + "\\NutritiousFoodData";
                if (!DirectoryExists(dataDirectory)) continue;

                if (g_config.logDataLoading)
                    DebugLog(std::string("[") + kPluginName + ":DataDirectoryFound] Directory=\"" + dataDirectory + "\"");
                CollectIniFiles(dataDirectory, discovered);
            } while (FindNextFileA(modHandle, &modData));
            FindClose(modHandle);
        }

        for (std::vector<std::string>::const_iterator it = discovered.begin(); it != discovered.end(); ++it)
        {
            const std::string normalized = ToLower(*it);
            if (uniqueFiles.insert(normalized).second) files.push_back(*it);
        }

        std::sort(files.begin(), files.end(), DataPackPathLess);

        if (g_config.logDataLoading)
        {
            for (std::vector<std::string>::const_iterator it = files.begin(); it != files.end(); ++it)
                DebugLog(std::string("[") + kPluginName + ":DataLoadOrder] File=\"" + *it + "\"");
        }
    }

    void ParseBaseIngredientFile(const std::string& path)
    {
        std::ifstream input(path.c_str());
        if (!input)
        {
            ErrorLog(std::string("[") + kPluginName + ":DataError] Could not open data pack: " + path);
            return;
        }

        std::string currentSid;
        std::string line;
        while (std::getline(input, line))
        {
            if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
            const std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;
            if (trimmed[0] == '[' && trimmed[trimmed.size() - 1] == ']')
            {
                const std::string section = Trim(trimmed.substr(1, trimmed.size() - 2));
                if (section.compare(0, std::strlen(kBasePrefix), kBasePrefix) == 0)
                {
                    currentSid = Trim(section.substr(std::strlen(kBasePrefix)));
                    if (!currentSid.empty()) g_baseIngredients[currentSid];
                }
                else currentSid.clear();
                continue;
            }
            if (currentSid.empty()) continue;
            const std::string::size_type equals = trimmed.find('=');
            if (equals == std::string::npos) continue;
            const std::string key = Trim(trimmed.substr(0, equals));
            std::string value = Trim(trimmed.substr(equals + 1));
            const std::string::size_type semicolon = value.find(';');
            if (semicolon != std::string::npos) value = Trim(value.substr(0, semicolon));
            if (key.empty()) continue;

            double numericValue = 0.0;
            if (!ParseNumber(value, numericValue))
            {
                ErrorLog(std::string("[") + kPluginName + ":DataWarning] Invalid number. File=\"" + path + "\" SID=\"" + currentSid + "\" Attribute=\"" + key + "\"");
                continue;
            }
            if (!_finite(numericValue) || numericValue < 0.0)
            {
                ErrorLog(std::string("[") + kPluginName + ":DataWarning] Negative or non-finite value clamped to zero. File=\"" + path + "\" SID=\"" + currentSid + "\" Attribute=\"" + key + "\"");
                numericValue = 0.0;
            }

            std::string& previousSource = g_attributeSources[currentSid][key];
            if (!previousSource.empty() && g_config.logDataLoading)
            {
                std::ostringstream overrideMessage;
                overrideMessage << "[" << kPluginName << ":DataOverride] SID=\"" << currentSid
                                << "\" Attribute=\"" << key << "\" PreviousFile=\"" << previousSource
                                << "\" NewFile=\"" << path << "\"";
                DebugLog(overrideMessage.str());
            }
            g_baseIngredients[currentSid][key] = numericValue;
            previousSource = path;
        }
        if (g_config.logDataLoading) DebugLog(std::string("[") + kPluginName + ":DataLoaded] File=\"" + path + "\"");
    }

    void ParseBaseIngredientSections()
    {
        g_baseIngredients.clear();
        g_attributeSources.clear();
        std::vector<std::string> files;
        CollectDataPackFiles(files);
        for (std::vector<std::string>::const_iterator it = files.begin(); it != files.end(); ++it)
            ParseBaseIngredientFile(*it);

        std::ostringstream summary;
        summary << "[" << kPluginName << "] Loaded data packs: " << files.size()
                << ", explicit base ingredients: " << g_baseIngredients.size();
        DebugLog(summary.str());
    }

    std::string SafeName(RootObject* object)
    {
        if (object == NULL) return "<null>";
        const std::string name = object->getName();
        return name.empty() ? "<unnamed>" : name;
    }

    std::string SafeDataName(GameData* data)
    {
        if (data == NULL) return "<null>";
        return data->name.empty() ? "<unnamed>" : data->name;
    }

    std::string SafeDataSid(GameData* data)
    {
        if (data == NULL) return "<null>";
        return data->stringID.empty() ? "<empty>" : data->stringID;
    }

    GameData* ResolveReference(const GameDataReference& reference)
    {
        if (reference.ptr != NULL) return reference.ptr;
        if (ou == NULL || reference.sid.empty()) return NULL;
        return ou->gamedata.getData(reference.sid);
    }

    const Ogre::vector<GameDataReference>::type* GetIngredients(GameData* data)
    {
        return data != NULL ? data->getReferenceListIfExists(kIngredientsListName) : NULL;
    }

    void AddScaled(AttributeMap& destination, const AttributeMap& source, double multiplier)
    {
        for (AttributeMap::const_iterator it = source.begin(); it != source.end(); ++it)
            destination[it->first] += it->second * multiplier;
    }

    void LogAttributes(const char* tag, GameData* data, int depth, double multiplier, const AttributeMap& values)
    {
        if (!g_config.logTree) return;
        if (values.empty())
        {
            std::ostringstream empty;
            empty << "[" << kPluginName << ":" << tag << "]"
                  << " Depth=" << depth
                  << " Name=\"" << SafeDataName(data) << "\""
                  << " SID=\"" << SafeDataSid(data) << "\""
                  << " EffectiveMultiplier=" << std::fixed << std::setprecision(6) << multiplier
                  << " Attributes=<none>";
            DebugLog(empty.str());
            return;
        }

        for (AttributeMap::const_iterator it = values.begin(); it != values.end(); ++it)
        {
            std::ostringstream line;
            line << "[" << kPluginName << ":" << tag << "]"
                 << " Depth=" << depth
                 << " Name=\"" << SafeDataName(data) << "\""
                 << " SID=\"" << SafeDataSid(data) << "\""
                 << " EffectiveMultiplier=" << std::fixed << std::setprecision(6) << multiplier
                 << " Attribute=\"" << it->first << "\""
                 << " Value=" << it->second;
            DebugLog(line.str());
        }
    }

    AttributeMap ResolveAttributes(GameData* data, int depth, std::set<std::string>& activePath)
    {
        AttributeMap result;
        if (data == NULL)
        {
            if (g_config.logTree) DebugLog(std::string("[") + kPluginName + ":Node] Reason=MissingGameData");
            return result;
        }

        const std::string sid = SafeDataSid(data);
        if (depth > g_config.maximumDepth)
        {
            if (g_config.logTree)
            {
                std::ostringstream line;
                line << "[" << kPluginName << ":Node] Depth=" << depth << " Name=\"" << SafeDataName(data)
                     << "\" SID=\"" << sid << "\" Reason=MaximumDepthReached";
                DebugLog(line.str());
            }
            return result;
        }

        if (activePath.find(sid) != activePath.end())
        {
            if (g_config.logTree)
            {
                std::ostringstream line;
                line << "[" << kPluginName << ":Node] Depth=" << depth << " Name=\"" << SafeDataName(data)
                     << "\" SID=\"" << sid << "\" Reason=CircularReference";
                DebugLog(line.str());
            }
            return result;
        }

        if (g_config.enableNutritionCache && depth == 0)
        {
            NutritionCache::const_iterator cached = g_nutritionCache.find(sid);
            if (cached != g_nutritionCache.end())
            {
                if (g_config.logTree) DebugLog(std::string("[") + kPluginName + ":CacheHit] SID="" + sid + """);
                return cached->second;
            }
        }

        const BaseIngredientMap::const_iterator base = g_baseIngredients.find(sid);
        if (base != g_baseIngredients.end())
        {
            result = base->second;
            if (g_config.logTree)
            {
                std::ostringstream line;
                line << "[" << kPluginName << ":Node] Depth=" << depth << " Name=\"" << SafeDataName(data)
                     << "\" SID=\"" << sid << "\" Base=true Reason=ExplicitBaseIngredient StopRecursion=true";
                DebugLog(line.str());
                LogAttributes("BaseValue", data, depth, 1.0, result);
            }
            return result;
        }

        const Ogre::vector<GameDataReference>::type* ingredients = GetIngredients(data);
        if (ingredients == NULL || ingredients->empty())
        {
            if (g_config.logUnknownTerminalIngredients)
            {
                std::ostringstream warning;
                warning << "[" << kPluginName << ":UnknownTerminal] Depth=" << depth
                        << " Name=\"" << SafeDataName(data) << "\" SID=\"" << sid
                        << "\" Result=ZeroAttributes";
                DebugLog(warning.str());
            }
            return result;
        }

        if (g_config.logTree)
        {
            std::ostringstream line;
            line << "[" << kPluginName << ":Node] Depth=" << depth << " Name=\"" << SafeDataName(data)
                 << "\" SID=\"" << sid << "\" Base=false Reason=ExpandIngredients";
            DebugLog(line.str());
        }

        activePath.insert(sid);
        int index = 0;
        for (Ogre::vector<GameDataReference>::type::const_iterator it = ingredients->begin();
             it != ingredients->end() && index < g_config.maximumIngredientsPerItem;
             ++it, ++index)
        {
            GameData* child = ResolveReference(*it);
            double ratio = static_cast<double>(it->values.value[0]) / g_config.ratioDivisor;
            if (!_finite(ratio) || ratio < 0.0)
            {
                ErrorLog(std::string("[") + kPluginName + ":RecipeWarning] Invalid ingredient ratio clamped to zero. ParentSID="" + sid + """);
                ratio = 0.0;
            }
            const AttributeMap childValues = ResolveAttributes(child, depth + 1, activePath);
            AddScaled(result, childValues, ratio);

            if (g_config.logTree)
            {
                std::ostringstream edge;
                edge << "[" << kPluginName << ":Ingredient] ParentDepth=" << depth
                     << " ParentName=\"" << SafeDataName(data) << "\" ParentSID=\"" << sid << "\""
                     << " Index=" << index
                     << " ChildName=\"" << SafeDataName(child) << "\""
                     << " ChildSID=\"" << (child != NULL ? SafeDataSid(child) : it->sid) << "\""
                     << " RawValues=" << it->values.value[0] << "," << it->values.value[1] << "," << it->values.value[2]
                     << " Ratio=" << std::fixed << std::setprecision(6) << ratio
                     << " Resolved=" << (child != NULL ? "true" : "false");
                DebugLog(edge.str());
            }
        }
        activePath.erase(sid);

        LogAttributes("CalculatedNode", data, depth, 1.0, result);
        if (g_config.enableNutritionCache && depth == 0) g_nutritionCache[sid] = result;
        return result;
    }

    void ApplyExperienceEffects(Character* character, GameData* foodData, const AttributeMap& attributes)
    {
        if (character == NULL || foodData == NULL) return;
        const bool animal = character->isAnimal();
        if (animal && !g_config.affectAnimals)
        {
            if (g_config.logEatCalls) DebugLog(std::string("[") + kPluginName + ":ExperienceSkipped] Character="" + SafeName(character) + "" Reason=AnimalDisabled");
            return;
        }
        CharStats* stats = character->getStats();
        if (stats == NULL)
        {
            DebugLog(std::string("[") + kPluginName + ":Experience] Character=\"" + SafeName(character) + "\" Result=NoStats");
            return;
        }

        for (ExperienceEffectMap::const_iterator it = g_experienceEffects.begin(); it != g_experienceEffects.end(); ++it)
        {
            const ExperienceEffect& effect = it->second;
            AttributeMap::const_iterator attribute = attributes.find(it->first);
            const double attributeValue = attribute != attributes.end() ? attribute->second : 0.0;
            double xpBeforeGlobal = attributeValue * effect.multiplier;
            double xp = xpBeforeGlobal * g_config.globalExperienceMultiplier;
            if (!_finite(xp) || xp < 0.0) xp = 0.0;
            if (effect.maximumXpPerMeal > 0.0 && xp > effect.maximumXpPerMeal) xp = effect.maximumXpPerMeal;

            if (g_config.logBalanceInfo)
            {
                std::ostringstream diagnostic;
                diagnostic << "[" << kPluginName << ":EffectMatch] Character=\"" << SafeName(character)
                           << "\" FoodName=\"" << SafeDataName(foodData) << "\" FoodSID=\"" << SafeDataSid(foodData)
                           << "\" Attribute=\"" << it->first << "\" AttributeFound=" << (attribute != attributes.end() ? "true" : "false")
                           << " AttributeValue=" << std::fixed << std::setprecision(6) << attributeValue
                           << " Enabled=" << (effect.enabled ? "true" : "false")
                           << " Stat=\"" << effect.statName << "\" EffectMultiplier=" << effect.multiplier
                           << " GlobalExperienceMultiplier=" << g_config.globalExperienceMultiplier
                           << " XPBeforeGlobal=" << xpBeforeGlobal
                           << " XPRequested=" << xp;
                DebugLog(diagnostic.str());
            }

            if (!effect.enabled || xp <= 0.0) continue;

            const float before = stats->getStat(effect.stat, true);
            stats->xpStat_eventBased(effect.stat, static_cast<float>(xp));
            const float after = stats->getStat(effect.stat, true);

            if (g_config.logBalanceInfo)
            {
                std::ostringstream line;
                line << "[" << kPluginName << ":Experience] Character=\"" << SafeName(character)
                     << "\" FoodName=\"" << SafeDataName(foodData) << "\" FoodSID=\"" << SafeDataSid(foodData)
                     << "\" Attribute=\"" << it->first << "\" AttributeValue=" << std::fixed << std::setprecision(6) << attributeValue
                     << " Stat=\"" << effect.statName << "\" EffectMultiplier=" << effect.multiplier
                     << " GlobalExperienceMultiplier=" << g_config.globalExperienceMultiplier
                     << " XPGranted=" << xp << " StatBefore=" << before << " StatAfter=" << after;
                DebugLog(line.str());
            }
        }
    }

    void CalculateFood(Character* character, GameData* foodData)
    {
        if (foodData == NULL) return;
        const std::string sid = SafeDataSid(foodData);
        const bool firstCalculation = g_calculatedSids.insert(sid).second;
        const bool logCalculation = !g_config.calculateOnlyOncePerSid || firstCalculation;

        if (g_config.logBalanceInfo)
        {
            std::ostringstream begin;
            begin << "[" << kPluginName << ":CalculationBegin] FoodName=\"" << SafeDataName(foodData)
                  << "\" FoodSID=\"" << sid << "\"";
            DebugLog(begin.str());
        }

        std::set<std::string> activePath;
        const AttributeMap result = ResolveAttributes(foodData, 0, activePath);

        if (g_config.logBalanceInfo)
        {
            if (result.empty())
            {
                std::ostringstream empty;
                empty << "[" << kPluginName << ":CalculationResult] FoodName=\"" << SafeDataName(foodData)
                      << "\" FoodSID=\"" << sid << "\" Attributes=<none>";
                DebugLog(empty.str());
            }
            else
            {
                for (AttributeMap::const_iterator it = result.begin(); it != result.end(); ++it)
                {
                    std::ostringstream line;
                    line << "[" << kPluginName << ":CalculationResult] FoodName=\"" << SafeDataName(foodData)
                         << "\" FoodSID=\"" << sid << "\" Attribute=\"" << it->first
                         << "\" Value=" << std::fixed << std::setprecision(6) << it->second;
                    DebugLog(line.str());
                }
            }

        }

        if (g_config.logBalanceInfo)
            DebugLog(std::string("[") + kPluginName + ":EffectApplicationBegin] Character=\"" + SafeName(character) +
                     "\" FoodName=\"" + SafeDataName(foodData) + "\" FoodSID=\"" + sid + "\"");
        ApplyExperienceEffects(character, foodData, result);
        if (g_config.logBalanceInfo)
        {
            DebugLog(std::string("[") + kPluginName + ":EffectApplicationEnd] Character=\"" + SafeName(character) +
                     "\" FoodName=\"" + SafeDataName(foodData) + "\" FoodSID=\"" + sid + "\"");
            std::ostringstream end;
            end << "[" << kPluginName << ":CalculationEnd] FoodName=\"" << SafeDataName(foodData)
                << "\" FoodSID=\"" << sid << "\"";
            DebugLog(end.str());
        }
    }

    bool HookedEatItem(Character* self, Item* food, Inventory* from)
    {
        const std::string characterName = SafeName(self);
        const std::string itemName = SafeName(food);
        GameData* foodData = food != NULL ? food->data : NULL;
        const std::string sid = SafeDataSid(foodData);
        const int quantityBefore = food != NULL ? food->quantity : -1;
        const bool vanillaFood = foodData != NULL && Item::isFood(foodData);

        const bool result = g_originalEatItem != NULL ? g_originalEatItem(self, food, from) : false;

        if (g_config.logEatCalls && result && vanillaFood)
        {
            std::ostringstream message;
            message << "[" << kPluginName << ":EatItem] Character=\"" << characterName
                    << "\" Item=\"" << itemName << "\" SID=\"" << sid
                    << "\" QuantityBefore=" << quantityBefore << " Result=true";
            DebugLog(message.str());
        }

        if (g_config.enabled && result && vanillaFood)
            CalculateFood(self, foodData);

        return result;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

__declspec(dllexport) void startPlugin()
{
    DebugLog(std::string("[") + kPluginName + "] Starting version " + kPluginVersion);
    g_iniPath = GetModuleDirectory() + "\\NutritiousFood.ini";

    g_config.enabled = ReadBool("General", "Enabled", true);
    g_config.affectAnimals = ReadBool("General", "AffectAnimals", true);
    g_config.enableNutritionCache = ReadBool("Performance", "EnableNutritionCache", true);
    g_config.logDataLoading = ReadBool("Logging", "LogDataLoading", true);
    g_config.logBalanceInfo = ReadBool("Logging", "LogBalanceInfo", false);
    g_config.globalExperienceMultiplier = ReadDouble("Balance", "GlobalExperienceMultiplier", 0.10);
    g_config.maximumDepth = ReadInt("Recursion", "MaximumDepth", 16);
    g_config.maximumIngredientsPerItem = ReadInt("Recursion", "MaximumIngredientsPerItem", 64);
    g_config.ratioDivisor = ReadDouble("Recursion", "RatioDivisor", 100.0);
    g_config.calculateOnlyOncePerSid = ReadBool("Recursion", "CalculateOnlyOncePerSID", true);
    g_config.logEatCalls = ReadBool("Logging", "LogEatCalls", true);
    g_config.logTree = ReadBool("Logging", "LogTree", true);
    g_config.logUnknownTerminalIngredients = ReadBool("Logging", "LogUnknownTerminalIngredients", true);

    if (g_config.maximumDepth < 1) g_config.maximumDepth = 1;
    if (g_config.maximumIngredientsPerItem < 1) g_config.maximumIngredientsPerItem = 1;
    if (g_config.ratioDivisor <= 0.0) g_config.ratioDivisor = 100.0;
    if (!_finite(g_config.globalExperienceMultiplier) || g_config.globalExperienceMultiplier < 0.0)
    {
        DebugLog(std::string("[") + kPluginName + ":Warning] Invalid GlobalExperienceMultiplier; clamped to 0.");
        g_config.globalExperienceMultiplier = 0.0;
    }

    g_nutritionCache.clear();
    ParseBaseIngredientSections();
    ParseExperienceEffectSections();

    std::ostringstream config;
    config << "[" << kPluginName << "] Config loaded: Enabled=" << (g_config.enabled ? "true" : "false")
           << ", AffectAnimals=" << (g_config.affectAnimals ? "true" : "false")
           << ", EnableNutritionCache=" << (g_config.enableNutritionCache ? "true" : "false")
           << ", GlobalExperienceMultiplier=" << g_config.globalExperienceMultiplier
           << ", MaximumDepth=" << g_config.maximumDepth
           << ", MaximumIngredientsPerItem=" << g_config.maximumIngredientsPerItem
           << ", RatioDivisor=" << g_config.ratioDivisor
           << ", CalculateOnlyOncePerSID=" << (g_config.calculateOnlyOncePerSid ? "true" : "false")
           << ", LogTree=" << (g_config.logTree ? "true" : "false")
           << ", LogBalanceInfo=" << (g_config.logBalanceInfo ? "true" : "false")
           << ", LogUnknownTerminalIngredients=" << (g_config.logUnknownTerminalIngredients ? "true" : "false");
    DebugLog(config.str());

    const intptr_t eatItemAddress = KenshiLib::GetRealAddress(&Character::eatItem);
    std::ostringstream addressMessage;
    addressMessage << "[" << kPluginName << "] Resolved address: eatItem=0x"
                   << std::hex << std::uppercase << eatItemAddress;
    DebugLog(addressMessage.str());

    if (eatItemAddress == 0)
    {
        ErrorLog(std::string("[") + kPluginName + "] Failed: eatItem address was null.");
        return;
    }

    const KenshiLib::HookStatus status = KenshiLib::AddHook(
        eatItemAddress,
        reinterpret_cast<void*>(&HookedEatItem),
        &g_originalEatItem);

    if (status != KenshiLib::SUCCESS || g_originalEatItem == NULL)
    {
        ErrorLog(std::string("[") + kPluginName + "] Failed to install Character::eatItem hook.");
        return;
    }

    DebugLog(std::string("[") + kPluginName + "] Beta foundation installed. Explicit base ingredients stop recursion before their own ingredients are inspected. Configured experience effects are applied after successful vanilla meals; food, recipes, inventory, hunger, and AI are not modified.");
}

__declspec(dllexport) void stopPlugin() {}
