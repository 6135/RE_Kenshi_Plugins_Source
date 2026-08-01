# Guard Break & Bash v1.0.0

A combat-extension plugin for RE_Kenshi and KenshiLib.

After a successful block, the plugin independently evaluates two possible stagger effects.

## Bash

The blocker may stagger the attacker.

- Source score: highest of Strength, Dexterity, and Toughness
- Resistance score: highest of Strength, Dexterity, and Toughness

## GuardBreak

The attacker may stagger the blocker.

- Source score: highest of Strength and Dexterity
- Resistance score: highest of Strength, Dexterity, and Toughness

## Provisional default preset

`BalancePreset=1`

| Skill difference | Chance |
|---:|---:|
| -60 or lower | 2.5% |
| -30 | 7.5% |
| 0 | 17.5% |
| +30 | 35.0% |
| +60 or higher | 47.5% |

Presets:

- `0`: Fully custom
- `1`: Recommended two-stage curve
- `2`: Growth-focused
- `3`: Vanilla-shaped

## Requirements

- RE_Kenshi
- KenshiLib

This source archive does not include a compiled DLL.
