---
id: TASK-0024
title: Mostrar el rango propio desde el cliente sin API key
work: S
eye: GLANCE
owner: agent
---


# TASK-0024 — Mostrar el rango propio desde el cliente sin API key

## Why

El usuario pidio el 2026-09-07 ver el elo en el historial. El suyo es gratis y no necesita
clave de Riot.

Comprobado ese dia contra su cliente:

```text
GET /lol-ranked/v1/current-ranked-stats
  queueMap.RANKED_SOLO_5x5 -> tier BRONZE, division II, leaguePoints 34,
                              wins 164, losses 186
```

Hoy el proyecto **no guarda ni consulta ningun dato de rango, de nadie**. Un grep de
`lol-ranked`, `league/v4` y `leaguePoints` sobre `src/` da cero resultados. Los unicos hits
de "tier" son `PoolTier` (`src/core/models.h:93`), que es la clasificacion manual del pool
de campeones, y no tiene relacion con el elo.

Es un endpoint del LCU, de solo lectura, del mismo tipo que los que ya se usan
(`src/core/lcu.cpp:228-296`).

## What to do

1. Leer `/lol-ranked/v1/current-ranked-stats` en `src/core/lcu.cpp`, junto al resto.
2. Guardar tier, division, LP, victorias y derrotas de la cola clasificatoria, con la fecha
   de lectura. Un rango sin fecha es un rango que envejece en silencio.
3. Mostrarlo en el perfil y como contexto del historial.
4. Sin cliente abierto, la vista dice cuando se leyo por ultima vez, no un valor a secas.

## Done when

- El perfil muestra el rango real del usuario con la fecha de la lectura.
- Sin League abierto, la vista lo dice y no inventa un valor.
- El veredicto del usuario: el rango que ve coincide con el de su cliente.

## Not covered

El rango de los rivales, que es TASK-0025 y tiene otra clasificacion de politica.

Cualquier estimacion de MMR. PRD 7 prohibe "crear una puntuacion publica equivalente a un
MMR alternativo", y la politica de terceros de Riot prohibe lo mismo. Se muestra el tier y
la division oficiales, sin derivar nada.
