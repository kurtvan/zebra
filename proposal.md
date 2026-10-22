Here is a fork  we made of the project M/milkdrop visualizer: https://github.com/kurtvan/libprojectM-4.1.4. Please read this to get an understanding of the codebase.

I'd like to rewrite/expand this to become a fully-fledged VJ/visual performance designer that we can easily design performances with. We should be able to distribute this afterwards for non-technical artists to design performances with this. The main features I'd like to have are: 

- Visually selecting presets from a menu rather than collecting them in a txt
  - Ideally we could load the presets in small "thumbnail" views that we can render cheaply, so that we can visually select them easily rather than having to search through them.
- "favouriting" presets to keep them in a saved place to access when building out performances
- Ability to load videos by filepath, with a file picker dialog.
- Syphon support for each s
- Creating "performances" made out of "scenes" that are serialized as performance files and can be loaded, with configurable CC midi numbers that are easy to re-assign.
- Currently we'll only use Ableton/Syphon support for routing MIDI and video, but we may expand this later.

I want to have discussions of a few things before we begin implementation:
1. Where is the project M fork non-production-ready? What can we rewrite and what looks good? I want to avoid shipping messy abstractions.
2. Are there any product gaps or additional questions you have for me?

Once we answer those, dont implement, ask me first and we'll begin technical planning.