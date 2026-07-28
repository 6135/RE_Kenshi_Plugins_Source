# Multi XP - Mining v1.0 RC3

Final release-candidate source package for a RE_Kenshi/KenshiLib plugin that grants configurable additional stat XP while characters perform registered mining work.

## Requirements

- RE_Kenshi
- KenshiLib
- Kenshi 1.0.65 x64
- Visual Studio 2010 v100 toolset for rebuilding this source package

## Default behavior

- Vanilla Labouring XP remains unchanged.
- Strength gains at `0.25` of Labouring time-based XP ticks.
- Toughness and Dexterity are disabled by default.
- Each mining source has its own stat caps.
- Machine-assisted sources are disabled by default.
- Unknown Labouring sources are recorded safely and remain disabled.

## Configuration

Global settings:

`MultiXPMining.ini`

Equipment definitions:

- `SourceData/00_Vanilla.ini`
- `SourceData/50_OfficialExtensions.ini`
- `SourceData/90_ModSupport.ini`
- `SourceData/99_UserOverride.ini`

Later INI files override earlier files key by key.

## RC3 scope

RC3 adds no gameplay features. It freezes the Mining implementation for final validation, aligns version text, and records build information in the RE_Kenshi log.

Do not install this build beside an older `MiningStrengthXP` or `MultiXPMining` test build.
