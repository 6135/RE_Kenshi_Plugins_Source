# Multi XP - Mining Data Pack Author Guide

Multi XP - Mining reads source definitions from `SourceData/*.ini` in filename order. Later files override earlier values one key at a time.

## Add a mining source

Create an INI file in `SourceData`, or add a section to `99_UserOverride.ini`:

```ini
[12345-example.mod]
Enabled=true
DisplayName=Example Mine
StrengthMultiplier=0.25
StrengthCap=15.0
ToughnessMultiplier=0.0
ToughnessCap=0.0
DexterityMultiplier=0.0
DexterityCap=0.0
```

The section name must be the equipment's full Kenshi `StringID`.

## Multiplier meaning

- `0.0`: disabled
- `0.25`: one additional XP tick for every four Labouring XP ticks
- `1.0`: the same number of additional XP ticks as Labouring XP ticks
- `2.0`: twice the Labouring XP tick count

The effective multiplier is:

`source multiplier × GlobalXpMultiplier`

## Cap meaning

Caps use the normal displayed stat level. The effective cap is:

`source cap × GlobalCapMultiplier`

Set `EnableCaps=false` in `MultiXPMining.ini` to disable cap checks globally.

## Unknown sources

When an unregistered Labouring source is used, the plugin writes a disabled template to `UnknownSources.ini`. Copy that section to `SourceData/99_UserOverride.ini`, then change the values you want.

Unknown sources are never enabled automatically.

Each recorded block also notes where the equipment was resolved from
(`GoalSubject` or `GoalSubtarget`). Equipment worked by slaves is normally
reported as `GoalSubtarget`; slave camp mines that are not part of the vanilla
data pack appear here like any other unregistered source and have to be enabled
by hand.
