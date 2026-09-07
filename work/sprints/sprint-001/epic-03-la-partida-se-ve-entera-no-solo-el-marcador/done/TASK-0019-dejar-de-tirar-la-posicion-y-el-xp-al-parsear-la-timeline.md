---
id: TASK-0019
title: Dejar de tirar la posicion y el xp al parsear la timeline
work: S
eye: NONE
owner: agent
---


# TASK-0019 — Dejar de tirar la posicion y el xp al parsear la timeline

## Why

El dato ya esta pagado y guardado. El parser lo descarta.

`src/core/ingest.cpp:113-121` lee **cuatro** metricas por participante y frame: `totalGold`,
`currentGold`, `minionsKilled` mas `jungleMinionsKilled` sumados, y `level`. `TLFrame`
(`src/core/models.h:76-83`) no tiene ningun campo de posicion.

Lo que hay de verdad en el disco, comprobado el 2026-09-07 sobre la base del usuario:

```text
participantFrames['1'] -> currentGold, dominionScore, jungleMinionsKilled, level,
                          minionsKilled, participantId, position, teamScore,
                          totalGold, xp
position -> {'x': 1078, 'y': 11422}
```

El JSON crudo entero vive en `matches.timeline_json` (`src/core/db.cpp:75`), y ni la ruta
del LCU (`src/core/lcu_history.cpp:127`) ni la de la API lo recortan. **El limite es el
parser, no el almacenamiento**: 67 partidas ya guardadas tienen las coordenadas dentro.

Y hay dato muerto: `TLEvent::posX/posY` ya se parsea en `src/core/ingest.cpp:136-139`, pero
un grep sobre todo `src/` demuestra que **nadie lo lee**.

## What to do

1. Anadir a `TLFrame` (`src/core/models.h:76-83`) la posicion por participante y el `xp`.
   Separar tambien `minionsKilled` de `jungleMinionsKilled`: hoy se suman y no se pueden
   volver a separar, y el CS de jungla dice otra cosa que el de linea.
2. Rellenarlos en `src/core/ingest.cpp:113-121`. Una partida sin `position` sigue siendo
   valida: el campo queda ausente, no en cero. Cero es una coordenada real de la Grieta.
3. Anadir `WARD_KILL` al mapeador de tipos (`src/core/ingest.cpp:13-22`). Falta hoy, y el
   filtro de `ingest.cpp:125` lo descarta antes de guardarlo.
4. Un test sobre el fixture de TASK-0018 que compruebe posicion y xp de un frame concreto.

## Done when

- `riftloop_tests.exe` pasa, y el test nuevo falla si se vuelve a ignorar `position`.
- Una partida ya guardada, re-parseada desde `matches.timeline_json`, da coordenadas sin
  descargar nada.
- Una timeline sin `position` no produce coordenadas en (0,0).

## Not covered

El dibujo, que es TASK-0020 y TASK-0021.

Los tipos de evento que el LCU no emite. Medido sobre las 67 partidas del usuario: la
timeline del cliente solo trae `CHAMPION_KILL`, `BUILDING_KILL` y `ELITE_MONSTER_KILL`.
No hay `WARD_PLACED` ni `ITEM_PURCHASED` que parsear por esa via. El paso 3 prepara el
parser para cuando la fuente sea la API v5.
