# Changelog

## v1.2.0-rc1

### Added

- Optional Adaptive Priority.
- Default is OFF to preserve the behavior of previous releases.
- Accepts both numeric and boolean-style values:
  - `AdaptivePriority=0`
  - `AdaptivePriority=1`
  - `AdaptivePriority=false`
  - `AdaptivePriority=true`
- Compares effective Dodge and effective Melee Defence at combat time.
- When effective Melee Defence is strictly higher, Weapon Dodge does not add
  its pre-block dodge for that attack.
- Equal values preserve the normal Weapon Dodge behavior.
- Optional priority decision logging.

### Compatibility

- No dependency on any particular block-then-dodge mod.
- The feature only controls Weapon Dodge & Guard's own pre-block dodge.
- The original Kenshi weapon-block path is left untouched when suppressed.

### Defaults

```ini
[Priority]
AdaptivePriority=0
LogDecisions=0
```
