---
id: TASK-0002
title: Medir escrituras de disco con y sin captura activa
epic: EP-01
work: S
eye: GLANCE
owner: agent
---


# TASK-0002 — Medir escrituras de disco con y sin captura activa

## Why

PRD 15.2 pide escrituras de disco cercanas a cero sin captura activa. Hoy hay al menos
tres caminos que escriben durante o cerca de la partida: SQLite, la cache de iconos y
el refresco diario de la muestra local, que el Agent lanza en estado calmado
(`src/agent/agent_main.cpp`). Con captura activa el gate sube a 4 Mbps.

## What to do

1. Contar bytes escritos por proceso con los contadores de E/S de Windows.
2. Medir dos escenarios: partida sin captura y partida con captura.
3. Comparar contra los gates de PRD 15.2 y 15.3.

## Done when

- El informe da bytes por segundo por proceso en los dos escenarios.
- Si el escenario sin captura no da cerca de cero, el informe nombra el proceso y la
  ruta que escribe.

## Not covered

CPU, memoria y FPS.
