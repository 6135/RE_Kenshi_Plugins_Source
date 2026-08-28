# Changelog

## 1.0.0-rc4

- Fixed additional XP not being granted while a character works as a slave.
  Slave labour goals keep the mining equipment in the AI goal subtarget, which
  the previous subject-only lookup ignored.
- Mining equipment is now matched from the goal subject and the goal subtarget,
  preferring a match that lists the character as a current operator.
- Source matched and UnknownSources entries record which handle the equipment
  was resolved from.
- Added a throttled warning when Labouring XP arrives without a resolvable
  mining source (DetailedLogging only).

## 1.0.0-rc3

- Final release candidate.
- Added build information to startup logs.
- Unified version and documentation wording.
- No gameplay behavior changes from RC2.

## 0.91.0-rc2

- Added ConfigVersion and SourceDataVersion validation.
- Organized logs into INFO/WARNING/ERROR levels.
- Added four-layer SourceData layout.
- Improved UnknownSources guidance.
