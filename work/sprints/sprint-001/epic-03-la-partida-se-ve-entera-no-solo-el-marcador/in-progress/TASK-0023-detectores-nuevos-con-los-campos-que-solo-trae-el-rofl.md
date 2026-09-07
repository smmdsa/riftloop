---
id: TASK-0023
title: Detectores nuevos con los campos que solo trae el rofl
work: L
eye: GLANCE
owner: agent
---


# TASK-0023 — Detectores nuevos con los campos que solo trae el rofl

## Why

El usuario pidio "un analisis deep de micro y macro game del player, como de el en funcion
a su team". TASK-0022 trae 367 campos por jugador que la API no da. Esta tarea los usa.

Tres senales que hoy no se pueden medir y que ahi estan:

- **Tiempo muerto.** `TOTAL_TIME_SPENT_DEAD 384` sobre `TIME_PLAYED 2360`: el 16 % de la
  partida. Es macro puro y no esta en ningun detector de `src/core/detectors.cpp`.
- **Uso de habilidades.** `SPELL1_CAST 124`, `SPELL4_CAST 10`. La definitiva lanzada diez
  veces en cuarenta minutos es una pregunta legitima sobre micro.
- **Comunicacion.** `ENEMY_MISSING_PINGS 12`, `GET_BACK_PINGS 11`, `COMMAND_PINGS 20`. Es
  lo mas cerca que se puede estar de medir al jugador en funcion de su equipo sin senalar
  a nadie.

## What to do

1. Cada detector nuevo respeta PRD 12.2: define **evento de oportunidad**, exclusiones,
   conducta esperada, ventana y confianza. El denominador es una oportunidad valida, nunca
   el numero de partidas.
2. Empezar por uno solo, el de tiempo muerto, y llevarlo entero hasta la evidencia. Un
   detector completo vale mas que tres a medias.
3. La evidencia se serializa con el contrato de PRD 30, como el resto
   (`src/core/contracts.h:10-27`).
4. **Sin blame** (PRD 9.11). Los pings del usuario son decisiones suyas. Los de sus
   companeros no se miran, y ningun hallazgo puede leerse como "tu equipo fallo".
5. La comparacion con el equipo se hace por rol y por metrica agregada, nunca nombrando a
   un companero.

## Done when

- Un detector nuevo pasa la suite con casos que dan positivo y casos que dan negativo.
- Produce evidencia con todos los campos del contrato, y el post-match la muestra.
- Ninguna salida del detector nombra ni senala a un companero.
- El veredicto del usuario sobre el hallazgo en una partida suya.

## Not covered

Los otros dos detectores propuestos. Cuando el primero este cerrado, se decide si siguen.

Cualquier cosa que exija los chunks cifrados del replay.

Depende de TASK-0022: sin el lector, no hay campos que medir.
