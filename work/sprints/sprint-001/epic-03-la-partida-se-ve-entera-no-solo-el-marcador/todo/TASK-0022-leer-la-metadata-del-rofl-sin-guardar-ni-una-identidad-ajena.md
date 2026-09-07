---
id: TASK-0022
title: Leer la metadata del rofl sin guardar ni una identidad ajena
work: M
eye: NONE
owner: agent
---


# TASK-0022 — Leer la metadata del rofl sin guardar ni una identidad ajena

## Why

El usuario pregunto el 2026-09-07 si el replay contiene la partida entera y si se puede
sacar de ahi un analisis mas profundo. La respuesta medida sobre su propio archivo
`LA2-1619204908.rofl`, de 18 MB:

```text
magic RIOT · version 16.16.804.9184 en texto plano
gameLength 2360199 ms · 81 chunks · 40 keyframes
bloque JSON al final del archivo, sin cifrar, en el offset 18006494
10 jugadores × 367 campos cada uno
```

Esos 367 campos traen cosas que match-v5 no da:

```text
SPELL1_CAST 124 · SPELL2_CAST 193 · SPELL3_CAST 47 · SPELL4_CAST 10
TOTAL_TIME_SPENT_DEAD 384 · TIME_CCING_OTHERS 17 · LONGEST_TIME_SPENT_LIVING 449
COMMAND_PINGS 20 · ENEMY_MISSING_PINGS 12 · GET_BACK_PINGS 11
WARD_PLACED 18 · VISION_SCORE 42 · Missions_TurretPlatesDestroyed 21
```

El usuario tiene 11 replays en disco. La ruta se pregunta al cliente, que ya se consulta:
`GET /lol-replays/v1/rofls/path` (`src/core/replays.cpp:96-101`), hoy solo se imprime
(`src/analyzer/analyzer_main.cpp:552`).

**Esta tarea no descifra nada.** El bloque es texto plano al final del archivo. Los chunks
y keyframes quedan intactos: van cifrados, la ofuscacion cambia cada parche, y los terminos
de Riot prohiben la ingenieria inversa. El PRD lo cierra en 14.5 y 17.2.

## What to do

1. Un lector en `src/core/` que abra un `.rofl`, compruebe el magic `RIOT`, busque el
   bloque JSON del final y devuelva los campos por jugador. Nada mas: no toca el payload.
2. **Descartar las identidades ajenas al leer, no despues.** El bloque trae `PUUID`,
   `RIOT_ID_GAME_NAME`, `RIOT_ID_TAG_LINE` y `SUMMONER_ID` de los diez. Solo se conserva la
   fila del propio usuario, emparejada por su puuid del perfil. De los otros nueve se
   guarda campeon, equipo y metricas, nunca quien son (PRD 16, 17.2, 29).
3. Emparejar el replay con la partida ya importada por `matchId`, para que las metricas
   caigan sobre el analisis que ya existe.
4. Degradar en silencio: si el magic no coincide, si no hay bloque JSON o si los campos
   cambiaron de nombre, no se lee nada y se registra el motivo. El formato es de Riot y
   puede cambiar en cualquier parche.
5. Un test con un archivo minimo construido a mano: cabecera valida y bloque JSON. No se
   commitea un `.rofl` real: pesa 18 MB y lleva identidades.

## Done when

- `--rofl-stats <matchId>` imprime las metricas del usuario de esa partida.
- Un grep de `PUUID` o `RIOT_ID` sobre lo que se guarda en la base no encuentra ninguna
  identidad que no sea la del usuario.
- Un archivo truncado o de otro formato no rompe nada y explica que paso.
- La suite pasa con el test del lector.

## Not covered

Los chunks y keyframes cifrados, y con ellos las posiciones que contienen. Fuera de alcance
por politica, no por dificultad. Las posiciones se sacan de la timeline oficial
(TASK-0019), que ya las tiene.

Que hacer con esas metricas. Esta tarea las trae; TASK-0023 las convierte en analisis.
