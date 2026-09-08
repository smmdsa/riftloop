---
id: EP-03
title: La partida se ve entera, no solo el marcador
work: L
eye: GLANCE
---

# EP-03 — La partida se ve entera, no solo el marcador

## Goal

El usuario ve donde muere, como progresa contra su propio rol y que hizo con sus
habilidades y su tiempo, sobre datos que ya estan en su disco.

Sus palabras, 2026-09-07: quiere "un analisis deep de micro y macro game del player usuario
de pitero en si, como de el en funcion a su team", y en el historial "mapas de calor de
movimiento por usuario, vision, puntos de asesinatos/muertes/asistencias, graficas de
progresion de oro, minions, enfocado tambien en el roll, elo de la partida promedio, elo de
cada oponente".

## The measurement this epic stands on

Cuatro medidas del 2026-09-07, todas sobre la maquina del usuario.

**1. Las coordenadas ya estan guardadas.** Su base de datos tiene 67 partidas con 5890
eventos, y **los 5890 traen posicion**:

```text
CHAMPION_KILL       4373
BUILDING_KILL        931
ELITE_MONSTER_KILL   586
participantFrames['1'].position -> {'x': 1078, 'y': 11422}
```

El JSON crudo entero vive en `matches.timeline_json` (`src/core/db.cpp:75`) y ninguna ruta
de importacion lo recorta. El parser si: `src/core/ingest.cpp:113-121` lee cuatro metricas
y tira el resto. **El limite es el parser, no el dato.**

**2. Hay dato parseado que nadie lee.** `TLEvent::posX/posY` se rellena en
`src/core/ingest.cpp:136-139` y un grep sobre todo `src/` demuestra que no tiene ni un
consumidor.

**3. El replay da 367 campos por jugador sin descifrar nada.** En
`LA2-1619204908.rofl`, 18 MB, hay un bloque JSON en claro al final del archivo con metricas
que match-v5 no da: `SPELL1_CAST`, `TOTAL_TIME_SPENT_DEAD`, `TIME_CCING_OTHERS`, los pings
desglosados. Los 81 chunks y 40 keyframes que contienen las posiciones van cifrados, la
ofuscacion cambia cada parche, y los terminos de Riot prohiben la ingenieria inversa. Esta
epica lee el bloque en claro y no toca el resto.

**4. El rango propio es gratis.** `GET /lol-ranked/v1/current-ranked-stats` devolvio
`BRONZE II, 34 LP, 164W/186L` sin clave de Riot. El proyecto no guarda hoy ningun dato de
rango, de nadie.

## Order of execution

| # | tarea | work | eye | por que va aqui |
|---|---|---|---|---|
| 1 | TASK-0018 fixture real anonimizado | S | NONE | sin el, nada espacial se puede testear |
| 2 | TASK-0019 parsear posicion y xp | S | NONE | abre el dato que ya esta en disco |
| 3 | TASK-0020 mapa de calor | M | GLANCE | 4373 muertes esperando a que alguien las pinte |
| 4 | TASK-0021 curvas contra el rol | M | GLANCE | mismo dato, otra pregunta |
| 5 | TASK-0022 leer la metadata del rofl | M | NONE | trae los 367 campos, sin identidades |
| 6 | TASK-0023 detectores con esos campos | L | GLANCE | los convierte en analisis |
| 7 | TASK-0024 rango propio por el cliente | S | GLANCE | gratis, sin clave |
| 8 | TASK-0025 rango de los rivales | M | GLANCE | necesita clave y una decision del usuario |

Las cuatro primeras no piden nada a Riot. La quinta y la sexta leen archivos del propio
usuario. Solo la octava necesita una clave y cuota.

## Technical sheet

| component | file | what changes |
|---|---|---|
| modelo | `src/core/models.h:76-83` | `TLFrame` gana posicion, xp y el CS separado |
| ingesta | `src/core/ingest.cpp:113-141` | deja de tirar campos; anade `WARD_KILL` |
| lector nuevo | `src/core/` | metadata del `.rofl`, sin tocar el payload |
| LCU | `src/core/lcu.cpp` | `/lol-ranked/v1/current-ranked-stats` |
| API Riot | `src/core/ingest.cpp:172` | `summoner/v4` y `league/v4` para el historial |
| UI | `src/desktop/` | control de mapa y control de graficas, ambos GDI |
| detectores | `src/core/detectors.cpp` | uno nuevo, con oportunidad y exclusiones |
| CLI | `src/analyzer/analyzer_main.cpp` | `--export-fixture` y `--rofl-stats` |

## Board

`python3 -m harness board`. This sheet holds no progress. The tree computes it.

## Verdicts

(the tool appends a line here when a task with an eye closes)
- 2026-09-08 · TASK-0025 · "pues ahora si todo esta funcionando riot api conectado y funcionando genial"
- 2026-09-08 · TASK-0026 · "perfecto todo esto funciono"
- 2026-09-08 · TASK-0029 · "perfecto todo esto funciono; se ve todo mejor"
- 2026-09-08 · TASK-0030 · "perfecto todo esto funciono"

## Out of scope

- **Los chunks cifrados del `.rofl`.** Fuera por politica, no por dificultad. Riot prohibe
  la ingenieria inversa y la ofuscacion cambia cada parche; `lolrofl` murio por eso y
  `roflxd`, el proyecto vivo, solo lee metadata. Las posiciones salen de la timeline
  oficial, que ya las tiene.
- **La vision y las compras.** La timeline del LCU solo emite tres tipos de evento: cero
  `WARD_PLACED` en las 67 partidas. Ese mapa necesita la timeline v5 con clave.
- **El rango en champion select.** PRD 17.2 lo clasifica en rojo. No se resuelve mas
  adelante: no se hace.
- **Cualquier MMR estimado.** PRD 7 y la politica de terceros de Riot prohiben una
  puntuacion equivalente a un ranking alternativo. Solo tier y division oficiales.
- **El rastro de movimiento continuo.** Las posiciones llegan una vez por minuto: son 30
  puntos por partida, no un recorrido. Se puede dibujar, pero no es lo que se entiende por
  mapa de movimiento.
- Las identidades de los otros nueve jugadores, en cualquier fuente y en cualquier momento.
