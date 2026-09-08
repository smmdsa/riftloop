---
id: TASK-0031
title: Con quien juegas mejor: companeros repetidos en tu propio historial
work: L
eye: RUN
owner: user
---


# TASK-0031 — Con quien juegas mejor: companeros repetidos en tu propio historial

## Why

El usuario, el 2026-09-07: "mediante data guardada local sqlite podriamos validar como nos
va con los que tengamos 2 o mas partidas, mientras mas partidas juntos mas relevancia para
ver que esta pasando como duo, y de hecho podria ser una feature para que mejoren como duo".

Es una buena idea y el dato ya esta en el disco: `match_json` guarda el `puuid` de los diez
de cada partida, asi que agrupar por companero es una consulta, no una descarga.

## Why this task belongs to the user

**Esta tarea cambia una decision que el usuario ya tomo.** El 2026-09-07, sobre TASK-0025,
eligio "guardar con fecha, sin identidad": el rango de los rivales se guarda contra la
ranura de esa partida y nunca contra una cuenta.

Un indice de companeros hace lo contrario por definicion: para decir "con este juegas mejor"
hay que reconocer a la misma persona en dos partidas, y eso es una identidad persistente de
un tercero en la base del usuario. PRD 16 y 29 lo restringen.

Hay una forma de hacerlo sin guardar a nadie: **un hash irreversible del puuid con una sal
local**. Permite "el mismo de antes" sin permitir "quien es". El nombre se lee de
`match_json` al dibujar, no del indice.

El agente no decide esto. Antes de escribir codigo hacen falta dos respuestas del usuario:

1. Indice por hash con sal local, o por puuid en claro.
2. Solo companeros de su propio equipo, o tambien rivales repetidos.

## What to do

Cuando esas dos respuestas existan:

1. Un indice de companeros: por cada partida, la ranura de cada uno y el resultado.
2. Un umbral de relevancia. Dos partidas juntos no dicen nada; el propio `kMetaMinSample`
   del proyecto exige 5 filas antes de ofrecer un resultado. Toda cifra sale con su
   denominador: "7 partidas juntos, 5 ganadas" y nunca "71 %" a secas.
3. La vista compara **contigo mismo**: tu tasa con esa persona contra tu tasa general del
   mismo rol. Nunca un juicio sobre esa persona.
4. **Sin blame (PRD 9.11).** La vista no puede decir "juegas peor con X". Puede decir con
   quien te va mejor y callar el resto.
5. Borrado: el indice entra en `wipeAll` como todo lo demas (PRD 16.4).

## Done when

- Las dos decisiones del usuario estan escritas en esta hoja.
- Un test comprueba que el indice no guarda ningun nombre ni puuid en claro, si esa fue la
  decision.
- La vista lista los companeros con 5 o mas partidas y su resultado, con denominador.
- El veredicto del usuario.

## Not covered

Cualquier consulta a Riot sobre esas personas. El indice se construye con lo que ya esta
importado.

Champion select. Reconocer a un rival anonimo es exactamente lo que PRD 17.2 marca en rojo.
