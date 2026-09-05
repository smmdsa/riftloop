---
id: TASK-0003
title: Medir la degradacion de FPS del juego contra los gates de PRD 15.2
epic: EP-01
work: L
eye: RUN
owner: agent
---


# TASK-0003 — Medir la degradacion de FPS del juego contra los gates de PRD 15.2

## Why

Es el gate que decide si el producto se puede enviar: PRD 15.2 admite 2 % de caida en
la mediana de FPS, 3 % en el 1 % low y 0,5 ms de impacto en el frame time p95. Tambien
es el mas dificil de medir, porque exige comparar la misma partida con y sin RiftLoop,
y dos partidas nunca son iguales.

## What to do

1. Elegir el metodo de medida y escribirlo antes de programar nada: PresentMon es la
   via estandar y no toca el proceso del juego, que PRD 14.5 prohibe.
2. Definir el escenario reproducible: mismo mapa, misma resolucion, misma duracion.
3. Medir con el overlay activo y con todo apagado, varias corridas.
4. Reportar mediana, 1 % low y frame time p95 de cada condicion, con la diferencia.

## Done when

- El metodo esta escrito y justificado, incluido por que no se inyecta nada.
- Hay al menos tres corridas por condicion.
- El informe da la diferencia por metrica y dice si pasa o no cada gate.

## Not covered

Memoria, CPU y disco. La maquina base de PRD 15.1 (i5-8250U) no esta disponible: se
mide en la maquina del usuario y el informe lo dice.
