---
id: TASK-0020
title: Mapa de calor de muertes y asesinatos sobre la Grieta
work: M
eye: GLANCE
owner: agent
---


# TASK-0020 — Mapa de calor de muertes y asesinatos sobre la Grieta

## Why

El usuario lo pidio el 2026-09-07: "mapas de calor de movimiento por usuario, vision,
puntos de asesinatos/muertes/asistencias".

El material ya existe. Medido sobre la base del usuario ese mismo dia:

```text
67 partidas · 5890 eventos · 5890 con coordenadas (el 100 %)
  CHAMPION_KILL       4373
  BUILDING_KILL        931
  ELITE_MONSTER_KILL   586
```

Cuatro mil trescientas muertes con posicion exacta, y nadie las dibuja. `TLEvent::posX/posY`
se parsea desde siempre (`src/core/ingest.cpp:136-139`) y ningun consumidor lo lee.

Esto no pide nada nuevo a Riot. Es leer lo que ya esta en `matches.timeline_json`.

## What to do

1. Un control nuevo en `src/desktop/`, al lado de `ReportView`, que pinte el mapa de la
   Grieta y encima una nube de puntos. El sistema de coordenadas de la timeline va de 0 a
   ~14820 en los dos ejes, con el origen abajo a la izquierda: **el eje Y se invierte al
   dibujar** o el mapa sale del reves.
2. Tres capas que se encienden por separado: muertes del usuario, asesinatos del usuario,
   asistencias. Cada una con su color de `src/desktop/ui.h:14-25`. Muertes en `kDanger`.
3. Agregar sobre las N ultimas partidas, no sobre una sola. Una muerte es anecdota; el
   patron esta en el monton. El numero de partidas se ve en pantalla.
4. Filtro por ventana de tiempo de partida: antes del minuto 14 y despues. Los patrones de
   linea y de mediojuego no son el mismo problema.
5. El mapa de fondo: usar la imagen de minimapa de Data Dragon, que ya se descarga por
   `src/core/imagecache.h`. Si no esta, dibujar un cuadrado con las tres lineas y el rio,
   nunca una pantalla vacia.

## Done when

- Una captura muestra el mapa con las muertes del usuario de sus ultimas 20 partidas.
- El numero de puntos coincide con el conteo de eventos de la base para ese filtro.
- Un punto en el nexo aliado cae abajo a la izquierda del mapa, no arriba a la derecha.
- Sin partidas importadas, la vista lo dice y no pinta un mapa vacio sin explicacion.
- El veredicto del usuario sobre la captura.

## Not covered

La vision y los wards. La timeline del LCU no emite `WARD_PLACED`: medido, cero en 67
partidas. Ese mapa necesita la timeline v5 con clave de Riot y no entra aqui.

El movimiento continuo del jugador. Las posiciones de `participantFrames` llegan **una vez
por minuto**, asi que un rastro de movimiento seria una linea de 30 puntos por partida, no
un recorrido. Se puede dibujar, pero no es lo que la gente entiende por "mapa de
movimiento", y prometerlo seria enganar.

Depende de TASK-0019 para las coordenadas de frame y de TASK-0018 para poder testearlo.
