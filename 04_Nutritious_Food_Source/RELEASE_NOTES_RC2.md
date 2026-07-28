# Nutritious Food v1.0 RC2

## Status

This is the first release-candidate source package. The feature set is frozen.

## Confirmed core behavior

- Detects successful vanilla eating.
- Resolves Kenshi `ingredients` recursively.
- Stops unconditionally at configured base ingredients.
- Grants configurable Strength, Toughness, Dexterity, and Perception experience.
- Preserves native race and age experience modifiers.
- Supports humans and animals.
- Uses `GlobalExperienceMultiplier = 0.10` by default.
- Loads local and independent external data packs.
- Uses deterministic filename-based load order and last-definition-wins overrides.
- Caches resolved food results by SID.

## Default balance

The default is intentionally restrained:

```ini
[Balance]
GlobalExperienceMultiplier = 0.10
```

Observed reference: at stat level 30 and race XP modifier 1.0, a Dustwich advanced Toughness by roughly 7% of the next-level gauge.

## RC2 exclusions

- Temporary buffs or debuffs
- Penalties or repetition penalties
- Consumed-fraction scaling
- Custom UI or food-description changes
- Process-depth bonuses

## Remaining release checks

- Normal-play cumulative balance observation
- Vanilla edible outlier audit
- Clean dependency-only installation test
- Steam subscription-path test
- Final English/Japanese Workshop documentation
## RC2 balance audit changes

The global experience multiplier remains `0.10`.

The vanilla base ingredient data was revised after in-game testing showed that
meat and fish are consumed in much smaller recipe quantities than crops.

Updated defaults:

```text
Raw Meat:      Power 8.0
Foul Raw Meat: Power 5.0
Grand Fish:    Power 6.4 / Technique 1.6
Thinfish:      Power 3.2 / Technique 2.1
Dried Fish:    Power 4.8 / Technique 1.8
```

Dried Fish is an independent vanilla ITEM (`50518-Newwworld.mod`). Its
`material = Fish` entry is not an ITEM ingredient relationship. Because a
finished item does not retain whether a mod crafted it from Grand Fish or
Thinfish, RC2 assigns Dried Fish a fixed midpoint value.
