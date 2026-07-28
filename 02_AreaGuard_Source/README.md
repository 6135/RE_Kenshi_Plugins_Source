# Area Guard 1.0 RC1 Fixed

## Overview

Area Guard extends Kenshi's melee combat so that when one character successfully blocks an area attack, later targets processed as part of that same attack can also be protected.

The implementation uses RE_Kenshi and KenshiLib and keeps intervention limited to confirmed melee block and hit processing.

## Requirements

- Kenshi
- RE_Kenshi
- KenshiLib

## Installation

Place the following files in:

```text
Kenshi/mods/AreaGuard/
```

Files:

```text
AreaGuard.mod
AreaGuard.dll
AreaGuard.ini
RE_Kenshi.json
```

## Default behavior

```ini
[AreaGuard]
Enabled=true
ProtectionMode=1
ProtectedReactionMode=1
FactorInDisguises=true
DebugLogging=false
```

### ProtectionMode

```text
1 = All (Recommended)
2 = Friendly
3 = NonEnemy
```

- All: protects every later target of the same blocked area attack.
- Friendly: protects only targets considered allies by the blocker.
- NonEnemy: protects allies and neutral targets, but not enemies.

### ProtectedReactionMode

```text
1 = NoReaction (Recommended)
2 = HitReaction
```

- NoReaction: protected targets take no damage and do not play a hit reaction.
- HitReaction: protected targets take no damage, but the vanilla hit reaction may still play.

### FactorInDisguises

Used by Friendly and NonEnemy relation checks.

### DebugLogging

```text
false = startup and error logs only
true  = detailed combat diagnostics
```

## Important behavior and limitations

Area Guard begins protecting targets only after Kenshi has registered a successful block.

Because Kenshi processes area-attack targets sequentially, a target processed before the successful block may still take damage even when the block appears to occur during the same swing.

This is an intentional limitation of the current Vanilla First implementation, not a guaranteed full-area barrier.

Area Guard protects only later targets that Kenshi processes as part of the same attack and that match all of these conditions:

```text
same attacker
same CombatTechnique
same direction
same tick
ProtectionMode condition
```

A successful dodge does not trigger Area Guard. Dodging avoids the attack for that character but does not stop the weapon from continuing through its area of effect.

## Compatibility

Tested with Weapon Dodge & Guard in both plugin orders.

No load-order dependency was observed.

## RC1 validation completed

- All mode
- Friendly mode
- NonEnemy logic
- NoReaction mode
- HitReaction mode
- Animal area attacks observed working
- Save/load test
- Weapon Dodge & Guard coexistence
- Both plugin orders
- DebugLogging=false
- No confirmed crash or combat-AI stall

## Known limitations

- Targets processed before the successful block are not retroactively protected.
- Dodge events do not protect nearby characters.
- Projectile attacks are not supported.
- Block or dodge animations are not forced onto protected secondary targets.


## RC1 packaging fix

`RE_Kenshi.json` is encoded as plain UTF-8 without BOM. The original RC1 package could be rejected by RE_Kenshi as `Invalid value` because a BOM was added during packaging.
