# Nutritious Food v1.0 RC2

Release Candidate 1 for Kenshi. The v1.0 feature scope is frozen. Nutritious Food detects successful vanilla eating, resolves the food through Kenshi's `ingredients` data, stops at explicitly configured base ingredients, and grants configured stat experience.

## Build
1. Open `NutritiousFood.sln`.
2. Select `Release | x64`.
3. Rebuild the solution.
4. Copy `ModPackage\NutritiousFood` to Kenshi's `mods` directory and enable it.

## Data packs
The plugin loads all `.ini` files from:

- `NutritiousFood\NutritionData\*.ini`
- `Kenshi\mods\*\NutritiousFoodData\*.ini`

Recommended filename convention:

```text
00_Vanilla.ini
10_Official_DLC.ini
20_ModCompatibility.ini
90_User.ini
99_UserOverride.ini
```

Definitions are merged in deterministic ascending path order. For the same SID and attribute, the later loaded definition wins. Every load and override can be written to `RE_Kenshi.log` through `LogDataLoading`.

`99_UserOverride.ini` is included but fully commented out. It does nothing until the user explicitly adds or enables a section.

## Compatibility data-pack example
A separate compatibility MOD may contain only:

```text
Some Food Mod - Nutritious Food Patch\
├─ Some Food Mod - Nutritious Food Patch.mod
└─ NutritiousFoodData\
   └─ 20_SomeFoodMod.ini
```

Example content:

```ini
; New base ingredient from another MOD
[BaseIngredient.12345-SomeFoodMod.mod]
Power = 0
Endurance = 0
Technique = 1
Awareness = 0
```

No Nutritious Food file needs to be replaced.

## Frozen v1.0 behavior
- Trigger only after successful vanilla `Character::eatItem()`.
- Resolve `ingredients` recursively.
- Stop immediately at any configured `BaseIngredient` SID.
- Unknown terminal ingredients contribute zero and may emit a warning.
- One successful eating event grants the full calculated effect.
- Kenshi applies its normal race and age experience multipliers.
- Animals are enabled by default and can be disabled in the main INI.
- Negative and non-finite values are clamped safely.
- Nutrition results are cached by food SID.

## Outside v1.0 scope
Temporary buffs, penalties, repetition penalties, consumed-fraction scaling, UI additions, and food-description modifications are intentionally excluded.

## Global balance control

`NutritiousFood.ini` contains a global XP scaler:

```ini
[Balance]
GlobalExperienceMultiplier = 0.10
```

The final requested XP is calculated as:

```text
food attribute × Effect multiplier × GlobalExperienceMultiplier
```

The per-effect `MaximumXPPerMeal` cap is applied after the global multiplier.
The recommended default is `0.10`, because food is consumed continuously by every squad member and is intended as a long-term flavor/progression supplement rather than a primary training method.

## Balance diagnostics

```ini
[Logging]
LogBalanceInfo = false
```

Set this to `true` while balancing or investigating compatibility. Detailed log lines include the resolved food attributes, the Effect multiplier, the global multiplier, requested/granted XP, and the stat value before and after application. Keep it `false` for normal play to avoid unnecessary log volume.


## RC2 policy

RC2 accepts only crash fixes, incorrect calculations, compatibility fixes, packaging corrections, and documentation changes. New gameplay features are deferred beyond v1.0.

## Current validation status

Confirmed in game: recursive recipe resolution, explicit base-ingredient stops, human and animal XP, native race/age modifiers, shared-food behavior, global XP scaling, local data-pack overrides, and discovery of independent external `NutritiousFoodData` packs.
## RC2 meat and fish balance

RC2 increases meat and fish attribute values because vanilla recipes consume
far fewer meat/fish items than crops. Dried Fish uses a fixed midpoint value:
the game does not preserve whether a finished Dried Fish was crafted from
Grand Fish or Thinfish, and FCS `material = Fish` is not a recipe ingredient.
