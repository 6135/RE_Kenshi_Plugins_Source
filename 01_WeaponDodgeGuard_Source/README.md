# Weapon Dodge & Guard v1.2.0

## Overview

v1.2.0 adds the optional `Adaptive Priority` feature while preserving the previous Weapon Dodge & Guard behavior by default.

Adaptive Priority is **OFF by default**.

Users who do not enable it will therefore retain the previous combat behavior.

---

## Adaptive Priority

`WeaponDodgeGuard.ini`

```ini
[Priority]
AdaptivePriority=0
```

### OFF (Default)

```ini
AdaptivePriority=0
```

or

```ini
AdaptivePriority=false
```

Weapon Dodge & Guard behaves as before.

### ON

```ini
AdaptivePriority=1
```

or

```ini
AdaptivePriority=true
```

During combat, the plugin compares Kenshi's effective values:

```text
Effective Dodge >= Effective Melee Defence
→ Weapon Dodge enabled

Effective Dodge < Effective Melee Defence
→ Weapon Dodge's additional pre-block dodge is suppressed for that attack
→ Kenshi's original weapon-block path is left untouched
```

When the values are equal, the normal Weapon Dodge behavior is retained.

---

## Design Notes

This feature does not require any specific Guard → Dodge mod.

Adaptive Priority only decides whether:

**Weapon Dodge & Guard's own pre-block dodge is allowed or suppressed for that attack.**

What happens after a failed guard is left to Kenshi itself or other mods.

This also allows the feature to work alongside future combat extensions without depending on a particular implementation.

When used with a compatible mod that adds Dodge after a failed Guard, the intended flow is:

```text
Higher Dodge:
Dodge → Guard → Dodge

Higher Defence:
Guard → Dodge
```

High-Dodge characters may therefore receive two dodge opportunities. This is intentional: avoiding interference with the behavior of other mods was prioritized over altering their mechanics.

---

## Effective Values

The comparison uses KenshiLib's dedicated effective-stat functions:

```cpp
stats->getDodge(true)
stats->getMeleeDefence(true)
```

During QA, changing equipment that raised or lowered Dodge or Melee Defence during combat caused the priority to switch immediately as the effective values changed.

---

## Logging

Normally use:

```ini
LogDecisions=0
```

For troubleshooting only:

```ini
LogDecisions=1
```

When enabled, the log will contain entries similar to:

```text
Adaptive priority:
dodgeEffective=...
meleeDefenceEffective=...
priority=dodge
weaponDodge=enabled
```

or:

```text
priority=defence
weaponDodge=suppressed
```

---

## Compatibility

With Adaptive Priority OFF, the previous Weapon Dodge & Guard behavior is preserved.

With Adaptive Priority ON, when Melee Defence has priority the plugin does not create a new guard mechanic. It simply does not add Weapon Dodge's pre-block dodge for that attack.

No specific Guard → Dodge mod is required. Weapon Dodge & Guard can still be used on its own.

---

## Note for Steam Workshop Users

Steam Workshop updates may replace the distributed INI file.

If you have customized `WeaponDodgeGuard.ini`, it is recommended that you keep a copy of your settings before updating.
