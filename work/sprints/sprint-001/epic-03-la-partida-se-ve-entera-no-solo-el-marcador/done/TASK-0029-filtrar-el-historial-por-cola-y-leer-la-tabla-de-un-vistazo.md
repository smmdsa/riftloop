---
id: TASK-0029
title: Filtrar el historial por cola y leer la tabla de un vistazo
work: M
eye: GLANCE
owner: agent
---


# TASK-0029 — Filtrar el historial por cola y leer la tabla de un vistazo

## Why

El usuario, el 2026-09-07, sobre la pantalla Partidas: "estaría bien que agregaramos botones
para poder filtrar ranked soloq, flex, normal reclutamiento, aram etc ahora esta todo junto
y no se entiende".

Tiene razon y ademas mezclar colas rompe una regla que el proyecto ya respeta en otro sitio:
`queueFamilyOf` (`src/core/db.h`) separa clasificatoria, normal y modos alternos porque
"no son la misma muestra" (PRD 13.3). La lista de partidas las junta todas.

Del mismo mensaje salen tres cosas mas de la misma pantalla:

- **No sale el nombre del invocador.** Ya esta en `match_json`: `riotIdGameName` y
  `riotIdTagline` de los diez. La vista dibuja el campeon y calla el nombre.
- **La fila no dice que es cada numero.** `3 / 6 / 5` y `niv 18 · 244 CS · 11.3k` no llevan
  encabezado.
- **La formacion no esta ordenada.** Los cinco salen en el orden del payload, no por
  posicion.

## What to do

1. Una fila de botones de cola sobre la lista: Todas, Clasificatoria solo, Clasificatoria
   flex, Normal, ARAM, Otros. El filtro se aplica sobre `MatchRow::queue`, que ya se guarda.
2. El boton activo se ve activo, y la vista dice cuantas partidas quedan tras el filtro.
3. Anadir el nombre del invocador a la fila de la tabla, junto al campeon. Es una partida ya
   jugada: PRD 17.2 lo marca verde. En champion select seguiria siendo rojo.
4. Encabezado de columna en la tabla: K/D/A, nivel, CS y oro.
5. Ordenar cada equipo por posicion: TOP, JUNGLE, MIDDLE, BOTTOM, UTILITY. Los que no
   declaren posicion van al final, en su orden original.
6. Icono de posicion junto a cada fila. **Data Dragon no tiene iconos de rol y el cliente
   tampoco los sirve**: medido el 2026-09-07, `/lol-game-data/assets/v1/icons-role/*` y
   `/position-icons/*` devuelven 400. Community Dragon si los tiene y **queda descartado**:
   el CLAUDE.md prohibe terceros. Se dibujan con GDI, cinco formas simples.

## Done when

- Una captura por cada filtro muestra solo partidas de esa cola.
- La tabla muestra el nombre de invocador de los diez.
- Los cinco de cada equipo salen en orden TOP, JUNGLE, MIDDLE, BOTTOM, UTILITY.
- Un encabezado nombra cada columna de numeros.
- El veredicto del usuario.

## Not covered

Los tooltips de items, runas y hechizos, que son TASK-0030.

El seguimiento de companeros entre partidas, que es TASK-0031 y necesita una decision de
politica del usuario.

## Verdict

- 2026-09-07 · by user · "el usuario vio la pantalla y pidio estos cambios; quedan a su ojo junto con TASK-0030"
- 2026-09-08 · by user · "perfecto todo esto funciono; se ve todo mejor"
