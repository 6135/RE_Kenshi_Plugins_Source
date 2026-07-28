# Nutritious Food Data-Pack Author Guide

## When a patch is required

A patch is needed only when another mod introduces a new base ingredient. New recipes made entirely from already configured ingredients are normally supported automatically.

## Find the SID

Open the ingredient item in FCS and copy its **String ID**. Use the complete value, including the source file suffix.

Example:

```text
1913-gamedata.base
```

## Folder structure

Create an independent Kenshi mod containing:

```text
Your Patch Mod\
├─ Your Patch Mod.mod
└─ NutritiousFoodData\
   └─ 20_YourPatch.ini
```

## INI format

```ini
; Ingredient name for human readers
[BaseIngredient.12345-YourFoodMod.mod]
Power = 0
Endurance = 0
Technique = 1
Awareness = 0
```

The presence of the section declares a recursion stop. Nutritious Food will not inspect that item's own `ingredients` after it is configured as a base ingredient.

## Load-order convention

```text
00_Vanilla.ini
10_Official_DLC.ini
20_ModCompatibility.ini
90_User.ini
99_UserOverride.ini
```

Files are loaded by filename, then full path. Later definitions override earlier values for the same SID and attribute.

## Attribute themes

- `Power` → Strength
- `Endurance` → Toughness
- `Technique` → Dexterity
- `Awareness` → Perception

These are game-facing attributes, not a claim of realistic nutrition.

## Safety rules

- Negative and non-finite values are clamped to zero.
- Unknown terminal ingredients contribute zero and may be logged.
- Do not replace files inside the main Nutritious Food mod.
