#!/usr/bin/env python3
"""Convert a performance.json from the libprojectM-4.1.4 fork into the Govtech Visualizer format (version 1).

The fork's scenes name presets by alias (looked up in favourites.txt) or by path, and use flat video fields.
The CC 20 value of each scene is its position in the fork's file, matching how the fork triggered scenes.
The app still opens version 1 files and converts their scenes to node graphs (format 3) when it saves them.

Usage:
  scripts/convert_fork_performance.py FORK_PERFORMANCE_JSON FAVOURITES_TXT OUTPUT_JSON
      [--name NAME] [--remap OLD_PREFIX=NEW_PREFIX ...]

--remap rewrites path prefixes, e.g. when the preset library moved:
  --remap /Users/kurt/projects/visualizers/libprojectM-4.1.4/presets/milk=/Users/kurt/Presets/milk
"""

import argparse
import json
import os
import sys


def load_favourites(path):
    aliases = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or ":" not in line:
                continue
            alias, preset = line.split(":", 1)
            alias, preset = alias.strip(), preset.strip()
            if alias and preset:
                aliases[alias] = preset
    return aliases


def remap(path, remaps):
    for old, new in remaps:
        if path.startswith(old):
            return new + path[len(old):]
    return path


def convert(fork, aliases, remaps, name):
    scenes = []
    problems = []
    for index, old in enumerate(fork.get("scenes", [])):
        scene_id = old.get("id") or f"scene-{index}"
        preset = old.get("preset", "")
        preset_path = remap(aliases.get(preset, preset), remaps)
        if preset not in aliases and not os.path.isabs(preset):
            problems.append(f"scene {scene_id}: preset '{preset}' is neither a favourites alias nor an absolute path")

        source_value = old.get("videoSource", "syphon")
        if source_value == "syphon":
            # The fork connected to whichever Syphon server it found first; empty names do the same here.
            source = {"type": "syphon", "appName": "", "serverName": ""}
            if old.get("videoEnabled", False):
                problems.append(f"scene {scene_id}: uses Syphon from any sender; set video.source.appName/serverName to pick one")
        else:
            source = {"type": "file", "path": remap(source_value, remaps)}

        scenes.append({
            "id": scene_id,
            "preset": {"path": preset_path},
            "video": {
                "enabled": bool(old.get("videoEnabled", False)),
                "source": source,
                "mix": float(old.get("videoMix", 0.4)),
                "lumaMin": float(old.get("lumaMin", 0.0)),
                "restartOnEnter": bool(old.get("restartVideo", True)),
            },
            "midi": {"cc20Value": index},
        })
        if index > 127:
            problems.append(f"scene {scene_id}: position {index} is beyond CC 20's range (0-127)")

    return {"version": 1, "name": name, "scenes": scenes}, problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("fork_performance")
    parser.add_argument("favourites")
    parser.add_argument("output")
    parser.add_argument("--name", default="")
    parser.add_argument("--remap", action="append", default=[], metavar="OLD=NEW")
    args = parser.parse_args()

    remaps = []
    for item in args.remap:
        if "=" not in item:
            parser.error(f"--remap needs OLD=NEW, got {item!r}")
        old, new = item.split("=", 1)
        remaps.append((old, new))

    with open(args.fork_performance, encoding="utf-8") as f:
        fork = json.load(f)
    aliases = load_favourites(args.favourites)
    name = args.name or os.path.splitext(os.path.basename(args.output))[0]

    performance, problems = convert(fork, aliases, remaps, name)

    missing = []
    for scene in performance["scenes"]:
        if not os.path.isfile(scene["preset"]["path"]):
            missing.append(f"scene {scene['id']}: preset not found: {scene['preset']['path']}")
        source = scene["video"]["source"]
        if scene["video"]["enabled"] and source["type"] == "file" and not os.path.isfile(source["path"]):
            missing.append(f"scene {scene['id']}: video not found: {source['path']}")

    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(performance, f, indent=2)
        f.write("\n")

    print(f"Wrote {args.output}: {len(performance['scenes'])} scenes")
    for line in problems + missing:
        print(f"  warning: {line}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
