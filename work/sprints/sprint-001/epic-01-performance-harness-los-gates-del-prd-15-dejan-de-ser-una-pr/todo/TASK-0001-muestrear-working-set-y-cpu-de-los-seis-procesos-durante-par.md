---
id: TASK-0001
title: Muestrear working set y CPU de los seis procesos durante partida
epic: EP-01
work: M
eye: GLANCE
owner: agent
---


# TASK-0001 — Muestrear working set y CPU de los seis procesos durante partida

## Why

PRD 15.2 fija el working set p95 total en 80 MB y la CPU media en 0,75 % durante
partida, y los llama gates de release, no metas. Nadie los ha medido nunca. El cliente
ya arranca seis procesos y desde esta sesion carga ademas Media Foundation para el
reproductor (`src/desktop/player.cpp`) y para el corte (`src/core/videocut.cpp`), que
son las dos piezas mas caras que se han anadido.

## What to do

1. Un ejecutable o modo del Analyzer que muestree los seis procesos por nombre.
2. Working set privado y CPU por proceso, cada segundo, sin bajar de ese intervalo
   (PRD 15.4 prohibe polling por debajo de 250 ms).
3. Guardar la serie en disco para poder calcular percentiles despues.
4. Calcular p95 de working set y media y p95 de CPU, por proceso y en total.

## Done when

- Una corrida sobre una partida real deja un archivo con la serie temporal.
- El resumen imprime working set p95 y CPU media y p95, por proceso y total.
- El propio medidor no aparece en la medida.

## Not covered

FPS del juego y escrituras de disco. Cada uno tiene su tarea.
