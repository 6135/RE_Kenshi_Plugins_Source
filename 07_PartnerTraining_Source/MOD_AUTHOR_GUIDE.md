# Partner Training — Mod Author Guide

## 1. Create an FCS facility

The facility must use Kenshi's training path so that `CharStats::xpTraining` is called. It may support two or more operators.

Record its FCS StringID.

## 2. Register the facility

Create a new INI in `SourceData`, or add an entry to `99_UserOverride.ini`:

```ini
[12345-Your Mod.mod]
Enabled=true
DisplayName=Your Training Mat
TrainingProfile=YourProfile
StrengthMultiplier=0.0
StrengthCap=0.0
ToughnessMultiplier=0.0
ToughnessCap=0.0
DexterityMultiplier=0.0
DexterityCap=0.0
```

An unregistered facility can also generate a disabled stub in `UnknownSources.ini`.

## 3. Create or reuse a profile

```ini
[YourProfile]
AttackXpMultiplier=1.0
AttackTeacherRatio=0.50
KatanasXpMultiplier=1.0
KatanasTeacherRatio=0.50
```

- `XpMultiplier=0.0`: disabled
- `XpMultiplier=1.0`: enabled at the normal framework rate
- `TeacherRatio=0.50`: stops at 50% of the strongest other operator's corresponding skill

## 4. Bundled templates

### Combat templates

- `Template_AllCombatSkills`
- `Template_MartialArts`
- `Template_Crossbows`
- `Template_Turrets`

### Weapon templates

- `Template_Katanas`
- `Template_Sabres`
- `Template_Hackers`
- `Template_HeavyWeapons`
- `Template_Blunt`
- `Template_Polearms`

The bundled templates are disabled by default. Copy and rename them before distribution, or override their values in a later-loading file.

## 5. Important behavior

Partner roles are determined independently for every skill and recalculated continuously. One character can teach Katanas while learning Heavy Weapons from the same partner at the same time.


## UnknownSources.ini filtering

Unknown-source assistance records only unregistered String IDs ending in `.mod`. Vanilla records such as `gamedata.base` are deliberately ignored. The framework does not inspect equipment names, so detection is language-independent and does not rely on words such as “training”, “dummy”, or “mat”.
