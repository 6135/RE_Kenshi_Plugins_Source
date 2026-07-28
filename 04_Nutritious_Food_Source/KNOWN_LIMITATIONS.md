# Known limitations

- v0.7 is a beta source package; the user must build the DLL.
- Balance values are provisional.
- The Perception enum name is compiled against the supplied KenshiLib headers; report a build error if that header version uses a different identifier.
- Data packs are loaded at plugin startup only. Restart Kenshi after editing them.
- The current file-order rule is lexical sorting of full paths; numeric prefixes should be used inside each data-pack folder.
- A successful partial meal receives the full configured effect by design.

- `GlobalExperienceMultiplier` is loaded at plugin startup; changing it requires restarting Kenshi.
- Detailed balance logging can produce substantial log volume and is disabled by default.
## Crafting-origin limitation

When multiple recipes produce the same ITEM SID, Nutritious Food cannot know
which production route created a particular inventory item. Dried Fish
therefore uses a fixed configured value rather than attempting to infer
whether it originated from Grand Fish or Thinfish.
