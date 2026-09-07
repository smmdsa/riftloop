---
id: TASK-0026
title: El historial muestra la tabla real de los diez, no solo el campeon
work: M
eye: GLANCE
owner: agent
---


# TASK-0026 — El historial muestra la tabla real de los diez, no solo el campeon

## Why

El usuario lo dijo el 2026-09-07, mirando la pantalla Partidas recien hecha: "se ve muy
raro, simple, sin data real de esos participantes (como el modelo op.gg, porofessor etc)
que te da datos como su farmeo, items, runas, MVP puntaje".

Tiene razon y el dato ya esta pagado. `MatchSummary::Participant`
(`src/core/models.h`) trae por cada uno de los diez, en cada partida ya importada:

```text
kills deaths assists  goldEarned  totalCs  champLevel
finalItems[6]  summonerSpells[2]  perks[6]  perkPrimaryStyle  perkSubStyle
```

La pantalla dibuja hoy dos de esos campos: el campeon y el rol. Todo lo demas se lee del
disco y no se muestra. No hace falta red, ni clave, ni un sitio de terceros.

## What to do

1. La lista de partidas gana KDA, CS y oro del usuario. Hoy son seis columnas y ninguna
   dice como fue la partida.
2. El panel de abajo pasa de una linea por jugador a una fila real: retrato, campeon y rol,
   KDA, CS, oro, nivel, los seis objetos con su icono, las dos runas principales y los dos
   hechizos. El rango de TASK-0025 se queda donde esta, en la misma fila.
3. Un `RVKind` nuevo en `src/desktop/ui.h` para esa fila. `IconRow` dibuja un icono y dos
   textos; esto necesita una tira de iconos pequenos.
4. **El puntaje del jugador se calcula, no se inventa.** Si se muestra algo tipo "MVP", sale
   de una formula escrita y visible, sobre datos de esa partida, y la UI dice cual es. PRD 7
   prohibe una puntuacion equivalente a un MMR alternativo: esto puntua **una partida**, no
   a un jugador, y no se guarda ni se compara entre partidas.
5. Sin blame (PRD 9.11): la tabla informa, y ninguna fila de un companero se etiqueta como
   causa de nada.

## Done when

- Una captura de Partidas muestra los diez con KDA, CS, oro, objetos y runas.
- Los numeros de una partida coinciden con los del cliente de League para esa misma partida.
- La lista de arriba muestra el KDA del usuario en cada fila.
- El veredicto del usuario sobre la captura.

## Not covered

Cualquier dato que no este ya en `match_json`. Nada de red nueva.

El rango, que es TASK-0025 y ya ocupa su columna.

Comparar al jugador con jugadores de fuera de la partida. No hay muestra global y fabricar
un percentil seria inventar.
