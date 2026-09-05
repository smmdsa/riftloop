---
id: TASK-0005
title: Comprobar el offset de relojes con una partida grabada de principio a fin
epic: EP-01
work: S
eye: RUN
owner: agent
---


# TASK-0005 — Comprobar el offset de relojes con una partida grabada de principio a fin

## Why

Se descubrio que el reloj de la Live Client Data API y los timestamps del timeline de
Riot no comparten origen: la diferencia medida fue de 153 s y puso los clips casi tres
minutos antes de su momento. El arreglo guarda `game_start_offset_sec` en el sidecar
(`src/capture/capture_main.cpp`) y lo aplica en `videoPositionSec`
(`src/core/videocut.cpp`), pero **no se ha verificado**: hace falta una partida grabada
entera con el binario nuevo. Las grabaciones anteriores no llevan el campo y el codigo
las descarta a proposito.

## What to do

1. Grabar una partida completa con la captura activada.
2. Comprobar que el sidecar trae `game_start_offset_sec` con un valor mayor que cero.
3. Dejar que el analisis genere los clips solo.
4. Sacar un fotograma del inicio de un clip con `--frame` y leer el reloj del juego.

## Done when

- El reloj del fotograma coincide con el instante de la evidencia menos los 15 s de
  margen, con un error por debajo de dos segundos.
- El usuario lo confirma mirando el clip en el post-match.

## Not covered

Las grabaciones sin ese campo. Quedan descartadas por diseno; recuperarlas exigiria
leer el reloj del propio video, que es otra tarea.
