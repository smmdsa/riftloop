---
id: TASK-0018
title: Fixture real anonimizado de timeline para poder testear lo espacial
work: S
eye: NONE
owner: agent
---


# TASK-0018 — Fixture real anonimizado de timeline para poder testear lo espacial

## Why

Los tres fixtures del repositorio son sinteticos y no sirven para nada espacial. Medido el
2026-09-07: `tests/fixtures/sample/demo_1.json`, `demo_2` y `demo_3` pesan **exactamente
39867 bytes cada uno** y solo cambian en `metadata.matchId`. La palabra `position` no
aparece ni una vez en los tres. Sus `participantFrames` tienen cinco claves y sus eventos
son cuatro `CHAMPION_KILL` y un `ELITE_MONSTER_KILL`, ninguno con coordenadas.

La base de datos del usuario, en cambio, tiene el dato real: 67 partidas, 5890 eventos,
**los 5890 con coordenadas**, y `position` en cada `participantFrame`.

Sin un fixture real no se puede escribir un test que falle para el mapa de calor ni para
las curvas. Se estaria escribiendo codigo de dibujo sin red.

## What to do

1. Un comando del Analyzer, `--export-fixture <matchId> <archivo>`, que saque una partida
   de `matches.timeline_json` y `matches.match_json` y la deje lista para `tests/fixtures/`.
2. **Anonimizar al escribir, no despues.** Fuera `puuid`, `riotIdGameName`, `riotIdTagline`,
   `summonerName`, `summonerId` y `participantId` de cuenta. El usuario propio queda como
   `me-puuid`, como ya hacen los fixtures de hoy. Los otros nueve quedan como `p2`..`p10`
   (PRD 16, 29).
3. Conservar intacto todo lo que no identifica: `position`, `xp`, oro, CS, nivel, tipos de
   evento, timestamps y `championId`.
4. Guardar una partida en `tests/fixtures/sample/real_1.json` y usarla en la suite.

## Done when

- `tests/fixtures/sample/real_1.json` existe, y un grep de `puuid` sobre el archivo solo
  encuentra `me-puuid` y los `p2`..`p10`.
- Un test comprueba que el fixture trae al menos un evento con `position` y al menos un
  `participantFrames` con `position`.
- La suite pasa.

## Not covered

El dibujo. Esta tarea solo consigue con que probarlo.

No exporta datos a ningun sitio fuera del repositorio: escribe un archivo local que el
usuario revisa antes de commitear.
