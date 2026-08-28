# Multi XP - Mining v1.0 RC4

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
- Characters working as slaves are covered by the same rules as free characters.

## Configuration

Global settings:

`MultiXPMining.ini`

Equipment definitions:

- `SourceData/00_Vanilla.ini`
- `SourceData/50_OfficialExtensions.ini`
- `SourceData/90_ModSupport.ini`
- `SourceData/99_UserOverride.ini`

Later INI files override earlier files key by key.

## RC4 scope

RC4 is a bug fix release candidate. Mining equipment used to be identified only from the current AI goal subject, which never held the equipment for slave labour, so slaves received no additional XP. Equipment is now also identified from the goal subtarget, and a match that lists the character among the equipment's current operators is preferred.

`DetailedLogging=true` now also reports Labouring XP that arrives without a resolvable mining source, at most once per `XpSummaryIntervalMs` per character.

Do not install this build beside an older `MiningStrengthXP` or `MultiXPMining` test build.
