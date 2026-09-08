# Where these icons come from

Nine PNG files, 35 KB in total. They are game icons of Riot Games: the five lane
positions and four scoreboard marks (creep score, gold, kills, level).

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

Copying nine small files removes the dependency: RiftLoop reads them from disk,
downloads nothing, and works offline.

**Ownership.** The images are Riot Games'. RiftLoop is a local, non-commercial
companion and uses them the way Data Dragon assets are used everywhere else in
this project. Riot Games does not endorse or sponsor this project.

To remove them: delete this folder. `rlui::localIcon` returns nullptr for a
missing file and every view falls back to text.
