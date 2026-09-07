---
id: TASK-0021
title: Curvas de oro CS y xp contra la referencia del rol
work: M
eye: GLANCE
owner: agent
---


# TASK-0021 — Curvas de oro CS y xp contra la referencia del rol

## Why

El usuario pidio el 2026-09-07 "graficas de progresion de oro, minions, enfocado tambien
en el rol".

El dato esta y se parsea ya: `TLFrame` guarda `totalGold`, `currentGold`, `cs` y `level`
por participante y por frame (`src/core/ingest.cpp:113-121`), y las 67 partidas del usuario
traen un frame por minuto. TASK-0019 anade `xp` y separa el CS de jungla del de linea.

Lo que falta es la referencia. Una curva de oro sola no dice nada: la pregunta util es
"cuanto me separo de lo normal en mi rol, y en que minuto". El `INDIVIDUAL_POSITION` y el
`TEAM_POSITION` estan en cada partida, asi que la muestra por rol se puede construir con
las partidas del propio usuario, igual que ya se hace con las runas y los items
(`src/core/meta.cpp`).

## What to do

1. Un control de grafica de lineas en `src/desktop/`. Eje X en minutos, eje Y en la unidad
   de la serie. Sin libreria nueva: GDI, como el resto de la UI.
2. Tres series por partida: oro total, CS y xp del usuario.
3. La referencia: la mediana del mismo rol en las partidas del propio usuario, dibujada
   como banda de fondo. **Nunca una linea de "lo que deberias"**: es una comparacion con
   uno mismo, no un objetivo inventado (PRD 12.2).
4. El rival de linea, cuando la partida lo permita: la misma serie del oponente con el
   mismo `TEAM_POSITION`, para ver el diferencial. Es un dato de la partida ya jugada, no
   scouting.
5. Marcar sobre el eje X los momentos que ya detecta el analisis, para que la curva y la
   evidencia hablen del mismo instante.
6. Declarar la muestra: cuantas partidas forman la referencia. Con menos de las que exige
   `kMetaMinSample` (`src/core/meta.h:19`), se dibuja la curva y se dice que no hay
   referencia, en vez de dibujar una banda que no significa nada.

## Done when

- Una captura muestra la curva de oro de una partida con su banda de referencia y el numero
  de partidas que la forman.
- Con una sola partida importada, la vista dibuja la curva y dice que no hay referencia.
- El valor del ultimo punto de la curva de oro coincide con el `goldEarned` de esa partida.
- El veredicto del usuario sobre la captura.

## Not covered

El elo de la partida y de los rivales, que son TASK-0024 y TASK-0025.

El mapa de calor, que es TASK-0020.

Comparar con jugadores que no sean el usuario o su rival de linea de esa partida. La
muestra es local (`src/core/meta.cpp`), no hay percentiles globales, y fabricar uno seria
una puntuacion inventada.
