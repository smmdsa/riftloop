# Where these icons come from

Twenty PNG files, 88 KB in total. They are game icons of Riot Games:

- 5 lane positions: `lane-TOP` … `lane-UTILITY`
- 4 scoreboard marks: `stat-cs`, `stat-gold`, `stat-kda`, `stat-level`
- 11 rank emblems: `tier-IRON` … `tier-CHALLENGER`, plus `tier-UNRANKED`

The count is written as a breakdown so that adding a family shows up here, in
`CMakeLists.txt` and in `localIcon` at the same time. It drifted once already:
the emblems arrived and three comments kept saying nine.

**Source:** https://github.com/noxelisdev/LoL_DDragon, folder `extras/`, read on
2026-09-07. That repository mirrors Data Dragon and adds files that Data Dragon
does not ship. Its own README says so: "This repository contains some additional
files, not included in Data Dragon".

**Why they are copied here and not downloaded.** Three measurements of
2026-09-07:

- Data Dragon serves `champion`, `item`, `spell`, `passive`, `profileicon`,
  `map`, `sprite` and `perk-images`. Every `img/ui/*` path answers 403, next to
  a 200 for `img/champion/Jinx.png` on the same request shape. The icons are
  not there.
- The League client serves `champion-icons`, `profile-icons` and `perk-images`
  over `/lol-game-data/assets/v1/`, and answers 400 for every position and
  scoreboard path tried. They are not reachable there either.
- The source repository weighs 10.5 GB, declares **no license**, and has one
  maintainer. A program that fetched from it at run time would break the day
  that repository moves.

Copying twenty small files removes the dependency: RiftLoop reads them from disk,
downloads nothing, and works offline.

**Ownership.** The images are Riot Games'. RiftLoop is a local, non-commercial
companion and uses them the way Data Dragon assets are used everywhere else in
this project. Riot Games does not endorse or sponsor this project.

To remove them: delete this folder. `rlui::localIcon` returns nullptr for a
missing file, and every view has a fallback: the lane marks are drawn by hand
in `drawRoleMark`, the rank badge disappears and its words stay, and the
column headers lose their icon and keep their label.
