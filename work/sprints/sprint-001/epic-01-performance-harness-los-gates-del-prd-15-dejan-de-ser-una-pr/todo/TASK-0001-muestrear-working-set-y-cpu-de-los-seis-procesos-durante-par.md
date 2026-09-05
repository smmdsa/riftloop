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

## Observacion del usuario, 2026-09-05

A ojo, sin medidor: Agent, Desktop y Overlay por debajo de 10 MB de RAM cada uno, y CPU
y GPU al 0 %. El usuario lo lee como buen margen contra el gate de 80 MB de working set
p95 del PRD 15.

Vale como senal, no como medida. Falta lo que pide esta tarea: los seis procesos y no
tres, la serie temporal en disco, el p95 y no el valor instantaneo, y la partida en curso
con el overlay dibujando. Un valor en reposo no es el p95 en partida, y el gate se mide
en partida. La GPU no es uno de los gates de PRD 15.2.
