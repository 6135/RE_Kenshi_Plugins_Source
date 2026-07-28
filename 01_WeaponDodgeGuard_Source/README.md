# Weapon Dodge & Guard v1.1.0 RC1

A RE_Kenshi plugin that checks Dodge first even while a weapon is equipped. If the dodge attempt fails, combat falls back to the weapon block originally selected by Kenshi.

Version 1.1 also supports standard dodge animations added by dodge animation mods.

---

## AnimationMode

- `0 = VanillaOnly`
  Uses only the vanilla `dodgeback` animation.

- `1 = Compatible`
  Automatically detects and uses compatible standing dodge animations.

- `2 = CustomAllowList`
  Uses only the animations listed in `AllowedAnimations`.

In **Compatible** mode, the plugin searches for techniques that satisfy the following conditions:

- `isDodge = true`
- `stumbleDodge = false`
- `isProne = false`

It also checks `minSkill` and `maxSkill`, and uses `chanceMult` as the relative selection weight.

`Taunt` and `Battlecry` animations are automatically excluded.

---

## Configuration Examples

```ini
AnimationMode=1
BlockedAnimations=Roll dodge
```

```ini
AnimationMode=2
AllowedAnimations=Fast dodge | GROUNDED_DodgeL2
```

---

## About maxEncumbrance

`maxEncumbrance` is **not** used when determining animation candidates.

Testing showed that Kenshi can select animations with `maxEncumbrance = 25` even when `getDodgePenalty_encumbrance()` is far below -100.

Because of this, directly comparing these two values was determined to be incorrect.

Until the internal purpose of `maxEncumbrance` is fully understood, the plugin intentionally does not apply any assumptions or restrictions based on this value.

The normal reduction in dodge chance caused by equipment weight and encumbrance is still handled by Kenshi itself through `calculateDodgeChance()`.

---

## Verified

- Dodge works immediately after game startup while wielding a weapon
- One-on-one combat
- Group combat
- Save / Load
- Game restart
- Vanilla dodge animation
- Dodge animations added by animation mods
- Automatic exclusion of Taunt / Battlecry animations
- AllowedAnimations / BlockedAnimations
- No confirmed abnormal experience gain, soft lock, or crashes

---

## RC Checklist

- `AnimationMode=0` uses only `dodgeback`
- `AnimationMode=1` uses automatically detected dodge animations
- `AnimationMode=2` uses only animations listed in `AllowedAnimations`
- Stable during extended gameplay with normal logging disabled