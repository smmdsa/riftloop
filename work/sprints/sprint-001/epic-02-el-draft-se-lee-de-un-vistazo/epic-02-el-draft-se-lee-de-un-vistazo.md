---
id: EP-02
title: El draft se lee de un vistazo
work: M
eye: GLANCE
---

# EP-02 — El draft se lee de un vistazo

## Goal

El usuario ve quién baneó qué y quién eligió qué, sin leer texto, y elige cuál de las dos
páginas de runas se escribe al cliente.

Las palabras del usuario, 2026-09-07: "en esa sección del match pick los champs tanto
pickeados como los baneados necesitamos que sean visualmente visible y bien claros, un
lado nuestro team vs el otro team (red vs blue team), con dos filas los baneados arriba y
los picks abajo... si hay 2 sets de runas, el usuario debe de ver que impacto tiene o
busca cada set y poder decidir si cargar uno u otro set".

## The measurement this epic stands on

Cuatro medidas tomadas el 2026-09-07 sobre el código, no sobre una impresión:

1. **Los baneos no distinguen equipos.** `src/core/lcu.cpp:145-193` lee `myTeamBans` y
   `theirTeamBans` del cliente y los junta en un `banIds` plano
   (`src/core/lcu.h:19-27`). La UI los concatena en una sola línea de texto
   (`src/desktop/desktop_main.cpp:998-1005`). El dato existe y se tira.
2. **La zona del draft no tiene un solo icono.** Los nueve combos de campeón son
   `CBS_DROPDOWNLIST` sin `CBS_OWNERDRAW` (`src/desktop/desktop_main.cpp:1901-1908`) y
   los baneos son un STATIC (`src/desktop/desktop_main.cpp:1910`). El almacén de retratos
   ya existe y ya repinta esta página (`src/desktop/ui.h:41-56`,
   `src/desktop/desktop_main.cpp:2316-2320`).
3. **La segunda página de runas no se puede aplicar.** `RunePlan::situational` se calcula
   (`src/core/planner.cpp:300-332`) y se pinta como una línea gris
   (`src/desktop/desktop_main.cpp:395-401`), pero el botón siempre escribe `rp.main`:
   `g->lastRunePage = rp.main;` (`src/desktop/desktop_main.cpp:1075` y `:1739`) es el
   único valor que llega a `applyRunePage` (`src/desktop/desktop_main.cpp:1139`).
4. **Espacio libre en la página.** Ventana fija de 1560 x 900 con barra lateral de 200
   (`src/desktop/desktop_main.cpp:107-108`). La vista de informe del Draft Lab acaba en
   el píxel 634 (`src/desktop/desktop_main.cpp:1913`): **266 px de alto sin usar**. La
   fila de combos más larga llega a 898 de 1288 de ancho: **390 px sin usar**.

El PRD ya pedía la mitad de esto. RF-RUN-001 (línea 533) pide "Página principal. Una
alternativa situacional." RF-RUN-002 pide que la alternativa cambie "por una razón
estratégica real, no para aparentar variedad". Está a medias desde que se escribió.

## Technical sheet

| component | file | what changes |
|---|---|---|
| LCU parser | `src/core/lcu.cpp:145-193` | los baneos salen en dos listas, una por equipo |
| modelos | `src/core/lcu.h:19-27`, `src/core/models.h:203-216` | `banIds` se parte en dos campos |
| modelos | `src/core/models.h:235-240` | `RunePage` gana `intent` |
| modelos | `src/core/models.h:242-253` | `RunePlan` gana `uncertaintyReason` |
| planner | `src/core/planner.cpp:215-340` | escribe `intent` y la razón de incertidumbre |
| IPC | `src/agent/agent_main.cpp:103-115` | `draftView()` emite `bansAllies` y `bansEnemies` |
| control de UI | `src/desktop/ui.h:61-84`, `src/desktop/ui.cpp` | `RVKind::TeamStrip` y su dibujo |
| pantalla | `src/desktop/desktop_main.cpp:363-401` | `rvPlan` se parte en dos columnas |
| pantalla | `src/desktop/desktop_main.cpp:139, 1075, 1108, 1139` | el plan entero sustituye a `lastRunePage` |
| contrato | `src/desktop/desktop_main.cpp:1071-1074` | guarda la confianza más baja de las dos partes |

## Board

`python3 -m harness board`. This sheet holds no progress. The tree computes it.

## Verdicts

(the tool appends a line here when a task with an eye closes)

## Out of scope

- El post-match, su playlist de clips y el reproductor (`src/desktop/player.cpp`).
- Una ventana redimensionable y WinUI 3. La deuda del PRD §14.1 sigue abierta.
- El orden de pick y el temporizador del draft. RF-CS-001 los pide como input y ninguna
  tarea de esta épica los toca.
- El motor que elige la alternativa (`src/core/planner.cpp:300-332`). Esta épica hace
  visible y aplicable lo que ya decide. No cambia la decisión.
- Champion Guard (RF-CS-006), que sigue sin implementar.
