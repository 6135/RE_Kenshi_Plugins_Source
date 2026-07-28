# Changelog

## v0.8.0 Beta

- Reworked external data-pack discovery.
- Finds the actual `mods` root by walking parent directories.
- Scans every immediate mod folder for `NutritiousFoodData\*.ini`.
- Adds `DataSearchRoot`, `DataDirectoryFound`, and `DataLoadOrder` diagnostics.
- Deduplicates discovered paths.
- Sorts primarily by INI filename, then full path, so the documented `00_ / 20_ / 99_` convention controls priority across separate mods.
- Keeps last-definition-wins merging.

## v0.8 Beta

- Changed the recommended and fallback `GlobalExperienceMultiplier` from `0.25` to `0.10`.
- Confirmed the v1.0 scope remains experience-only. Temporary buffs and debuffs remain excluded from the frozen design.
- Updated balance-test guidance for the subtle long-term progression target.

## v0.7.2 Beta

- Added `[Balance] GlobalExperienceMultiplier`.
- Set the recommended default to `0.25`.
- Final XP now equals attribute value × per-effect multiplier × global multiplier.
- Per-effect `MaximumXPPerMeal` remains the final safety cap.
- Added `[Logging] LogBalanceInfo`.
- Detailed calculation and XP logs are disabled by default and can be enabled for balance testing.
- Added global multiplier details to diagnostic log lines.
- Invalid negative or non-finite global multipliers are clamped to `0` with a warning.

## v0.7.1 Beta
- Replaced the active-looking example override file with an inert, fully commented `99_UserOverride.ini`.
- Clarified data-pack directory structure and filename convention.
- Added an external compatibility data-pack test procedure.
- Updated documentation for confirmed humanoid, animal, partial-stack, and override behavior.
- No gameplay calculation or experience-balance changes from v0.7.0.

## v1.0 RC2

- Promoted the validated v0.8 implementation to Release Candidate 1.
- Froze the v1.0 feature scope.
- Set normal `LogEatCalls` default to `false` for quieter public use.
- Added RC2 release notes, validation checklist, and data-pack author guide.
- Retained the recommended global XP multiplier of `0.10`.
