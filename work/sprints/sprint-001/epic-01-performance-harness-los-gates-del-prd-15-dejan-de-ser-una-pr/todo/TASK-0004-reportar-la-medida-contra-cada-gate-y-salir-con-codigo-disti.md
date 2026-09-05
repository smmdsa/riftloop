---
id: TASK-0004
title: Reportar la medida contra cada gate y salir con codigo distinto de cero si alguno se supera
epic: EP-01
work: M
eye: GLANCE
owner: agent
---


# TASK-0004 — Reportar la medida contra cada gate y salir con codigo distinto de cero si alguno se supera

## Why

Una medida que nadie compara contra el gate no cambia ninguna decision. PRD 15 los
llama release gates; para que lo sean, superarlos tiene que romper algo.

## What to do

1. Leer los gates de un archivo, no del codigo, para poder ajustarlos sin recompilar.
2. Comparar cada medida contra su gate.
3. Salir con codigo distinto de cero cuando alguno se supera, e imprimir cual y por
   cuanto.
4. Dejar el informe en un formato que se pueda guardar por version del cliente.

## Done when

- Con todos los gates cumplidos, el comando sale con 0.
- Bajando un gate a proposito, sale con codigo distinto de cero y nombra la metrica.
- El informe queda en disco con la version y la marca de compilacion del cliente.

## Not covered

La toma de medidas. Esta tarea solo compara y reporta.
