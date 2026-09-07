---
id: TASK-0009
title: Separar los baneos por equipo desde el LCU hasta la UI
work: S
eye: NONE
owner: agent
priority: 1
priority-by: user
priority-date: 2026-09-07
priority-why: "Primero lo barato: 0009, 0011 y 0014"
---


# TASK-0009 — Separar los baneos por equipo desde el LCU hasta la UI

## Why

El usuario pidió ver el champion select con "un lado nuestro team vs el otro team (red vs
blue team)... dos filas los baneados arriba y los picks abajo". Los picks ya llegan
separados. Los baneos no.

El dato existe y se tira. `parseChampSelect` lee las dos listas del cliente y las junta
en una sola:

- `src/core/lcu.cpp:145-193` — el parser lee `bans.myTeamBans` y `bans.theirTeamBans`.
- `src/core/lcu.h:19-27` — `std::vector<int> banIds;  // completed bans, both teams, in draft order`.
- `src/core/models.h:203-216` — `DraftContext::bans` repite la lista plana.
- `src/agent/agent_main.cpp:103-115` — `draftView()` la serializa como el campo `bans`.
- `src/desktop/desktop_main.cpp:998-1005` — la UI la concatena en un STATIC: `"Baneos (5): A, B, C, D, E"`.

Nadie puede decir quién baneó qué. Sin este campo, TASK-0010 no puede pintar dos lados.

## What to do

1. En `ChampSelectView` (`src/core/lcu.h:19-27`), cambiar `banIds` por `myTeamBanIds` y
   `enemyTeamBanIds`. Mantener el orden de draft dentro de cada lista.
2. En `parseChampSelect` (`src/core/lcu.cpp:145-193`), rellenar las dos listas por
   separado. El camino de `actions` con `type=="ban"` no sabe de equipos: cuando solo
   exista ese camino, dejar la lista propia vacía y la enemiga también, y marcarlo.
3. Propagar el corte a `DraftContext` (`src/core/models.h:203-216`) y a `draftView()`
   (`src/agent/agent_main.cpp:103-115`): dos campos json, `bansAllies` y `bansEnemies`.
4. En `applyDraftView` (`src/desktop/desktop_main.cpp:988-1005`), leer los dos campos.
   Si el json trae el campo viejo `bans` y no los nuevos, tratarlo como equipo
   desconocido en vez de perder el dato.
5. Un test del parser con un fixture de `/lol-champ-select/v1/session` que traiga
   `myTeamBans` y `theirTeamBans` distintos. El test debe fallar antes del cambio.

## Done when

- `riftloop_tests.exe` pasa, y el test nuevo falla si se vuelve a juntar las dos listas.
- El fixture con 3 baneos propios y 2 enemigos produce 3 y 2, no 5.
- Un `DraftContext` construido desde ese fixture conserva la separación.

## Lo que cambio al implementar, 2026-09-07

El paso 2 asumia que el camino de `actions` no sabe de equipos. Es falso. Cada accion de
ban trae `actorCellId`, y `myTeam` y `theirTeam` traen `cellId`. El equipo se deriva
tambien en vivo, que es justo cuando el sumario `bans` todavia esta vacio.

Por eso el resultado tiene tres listas, no dos. `allyBanIds`, `enemyBanIds` y
`unknownBanIds`. La tercera recoge el baneo cuya celda el cliente no atribuye a ningun
equipo. Meterlo en un lado seria inventar el dato (PRD 3.3: evidencia o silencio).

El orden de proceso importa: el sumario `bans` manda porque nombra el lado sin inferencia,
y las acciones rellenan lo que falte. Un baneo que ya tiene lado no se vuelve a anadir.

Defecto encontrado en la suite: un `v[i]` fuera de rango aborta el proceso en el runtime
de depuracion de MSVC, y se lleva por delante la salida. El informe no dice que CHECK
fallo. Los CHECK nuevos usan el ayudante `at()` de `tests/tests_main.cpp:57`. Los CHECK
que ya existian siguen con el acceso directo: cambiarlos queda fuera de esta tarea.

## Not covered

El dibujo. Esta tarea solo mueve el dato. La tira visual es TASK-0010.

Tampoco cubre el orden de pick ni el turno del draft: el PRD RF-CS-001 los pide como
input, y no hay tarea escrita para ellos.
