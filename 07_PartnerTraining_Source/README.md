# Partner Training Framework — Version 1.0.0 RC4

Partner Training is an RE_Kenshi framework that lets characters using the same registered training facility learn selected skills from one another.

For each configured skill, the framework compares the current operators independently. The character below the partner-relative ceiling receives XP. Trainer and trainee roles are not fixed; they can reverse during training when skill values change.

## Requirements

- Kenshi 1.0.65 x64
- RE_Kenshi 0.3.4
- KenshiLib 0.4.0
- A compatible FCS content mod, such as **Partner Training Mat**

## Package roles

- **Partner Training Framework**: DLL, configuration, profiles, compatibility data, and extension API through INI files.
- **Partner Training Mat**: official FCS buildings and research.

These are separate Kenshi mod folders because both packages contain their own `.mod` file.

## Official mat behavior

The official mats train Melee Attack, Melee Defence, and Dodge.

- Mk I: up to 25% of the partner's corresponding skill
- Mk II: up to 50%
- Mk III: up to 75%
- Mk IV: up to 100%

Other skills and physical attributes are disabled by default.

## Runtime structure

```text
FCS training facility
        ↓ StringID
SourceData mapping
        ↓ TrainingProfile
Configured skills and TeacherRatio
        ↓
Dynamic per-skill partner comparison and XP
```

## Configuration layout

```text
PartnerTraining.ini
SourceData/
  00_OfficialMats.ini
  90_AttributeTemplates.ini
  90_ModSupport.ini
  99_UserOverride.ini
TrainingProfiles/
  00_OfficialProfiles.ini
  90_CombatTemplates.ini
  91_WeaponTemplates.ini
```

Later-loading INI files override matching keys from earlier files. Add separate INI files or use `99_UserOverride.ini`; do not edit official definitions unless necessary.

## Supported partner-relative skills

- Melee Attack
- Melee Defence
- Dodge
- Martial Arts
- Katanas
- Sabres
- Hackers
- Heavy Weapons
- Blunt
- Polearms
- Crossbows
- Precision Shooting
- Perception
- Turrets

Disabled examples are included in `90_CombatTemplates.ini` and `91_WeaponTemplates.ini`.

## Physical attributes

Strength, Toughness, and Dexterity currently use equipment-level XP multipliers and absolute caps. They do not use partner-relative `TeacherRatio` values.

Disabled examples are included in `SourceData/90_AttributeTemplates.ini`.

## Logging

The default configuration uses concise logging.

```ini
[Logging]
DetailedLogging=false
```

Set `DetailedLogging=true` only for diagnosis. It enables operator snapshots, start/block events, and XP summaries.

## Unknown equipment helper

When `GenerateUnknownSourcesIni=true`, use of an unregistered training facility creates a disabled entry in:

```text
UnknownSources.ini
```

Copy the generated block into a SourceData INI, set `Enabled=true`, and assign a bundled or custom `TrainingProfile`.

## Confirmed validation

- Official Mk I–IV facilities are detected.
- All supported combat and weapon skills can receive XP through profiles.
- Disabled templates do not grant XP.
- Partner-relative ceilings are applied per skill.
- The higher-skilled character does not receive unintended XP for that skill.
- Trainer and trainee roles switch dynamically when skill values reverse.
- Concise public logging works with `DetailedLogging=false`.

## Unknown source detection

When `GenerateUnknownSourcesIni=true`, the framework records only unregistered FCS records whose String ID ends in `.mod`. Vanilla/base-game records such as `gamedata.base` are ignored. Display names are never used to classify equipment, so translations and unconventional names do not affect detection.


## Unknown source filtering

`UnknownSources.ini` records unregistered third-party FCS records whose source file ends in `.mod`.
Kenshi's official vanilla data files are excluded even when they use the `.mod` extension:

- `Newwworld.mod`
- `Dialogue.mod`
- `rebirth.mod`
- `gamedatabase` / `gamedata.base`

Display names and translated words are not used for detection.
