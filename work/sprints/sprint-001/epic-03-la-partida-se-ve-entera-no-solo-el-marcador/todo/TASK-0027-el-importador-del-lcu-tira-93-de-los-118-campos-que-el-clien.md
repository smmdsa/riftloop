---
id: TASK-0027
title: El importador del LCU tira 93 de los 118 campos que el cliente da
work: M
eye: GLANCE
owner: agent
---


# TASK-0027 — El importador del LCU tira 93 de los 118 campos que el cliente da

## Why

Es el mismo defecto que TASK-0019, en otro sitio: **el limite es el parser, no el dato**.

Medido el 2026-09-07 preguntando al cliente del usuario por
`/lol-match-history/v1/games/1622439669`. Cada participante trae un bloque `stats` con
**118 campos**. Lo que `src/core/lcu_history.cpp` guarda en `match_json` son **25**:

```text
assists champLevel championId championName deaths goldEarned item0..item5 kills
neutralMinionsKilled participantId perks puuid riotIdGameName riotIdTagline
summoner1Id summoner2Id teamId teamPosition totalMinionsKilled win
```

Entre los 93 que se tiran estan los que hacen falta para que la tabla del historial diga
algo mas que el marcador:

```text
totalDamageDealtToChampions  physicalDamageDealtToChampions  magicDamageDealtToChampions
damageDealtToObjectives  damageDealtToTurrets  totalDamageTaken  damageSelfMitigated
visionScore  wardsPlaced  wardsKilled  visionWardsBoughtInGame
turretKills  inhibitorKills  totalHeal  totalUnitsHealed  totalTimeCrowdControlDealt
```

El usuario lo dijo el 2026-09-07 al ver la pantalla nueva: quiere "datos como su farmeo,
items, runas, MVP puntaje", el modelo de op.gg. TASK-0026 dibujo lo que habia. Esto trae lo
que falta, y no pide ni una llamada nueva a Riot: el cliente ya lo devuelve en la misma
respuesta que ya se pide.

## What to do

1. Ampliar el conversor v4 a v5 de `src/core/lcu_history.cpp` con los campos de arriba. Los
   nombres de match-v5 no son los del LCU (`totalDamageDealtToChampions` existe en los dos;
   `wardsPlaced` no cambia; comprobar uno a uno y no adivinar).
2. Anadirlos a `Participant` (`src/core/models.h`) y a `parseMatch`.
3. **Las 67 partidas ya guardadas no los tienen.** Decidir con el usuario si se re-importan
   desde el cliente. `Db::matchesMissingField` y `replaceMatchJson` ya existen para esto,
   y el cliente conserva el historial.
4. La tabla de TASK-0026 gana una columna de dano a campeones y otra de vision.
5. El "puntaje" de una partida pasa a calcularse con dano, vision y objetivos, no solo con
   kills y asistencias. Sigue siendo de **una partida**, no del jugador, y la formula se
   escribe en la UI (PRD 7: nada equivalente a un MMR alternativo).

## Done when

- Una partida re-importada guarda dano a campeones y vision de los diez.
- La tabla del historial los muestra.
- Una partida vieja, sin esos campos, no rompe la vista: dice que ese dato no se importo.
- El veredicto del usuario.

## Not covered

Cualquier campo que el cliente no de. La lista sale de un volcado real, no de la
documentacion.

Los campos del `.rofl`, que son TASK-0022 y TASK-0023 y necesitan el archivo en disco.
