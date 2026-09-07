---
id: TASK-0017
title: Comprobar en un draft real si el cliente expone el lado del mapa
work: S
eye: RUN
owner: user
---


# TASK-0017 — Comprobar en un draft real si el cliente expone el lado del mapa

## Why

El 2026-09-07 el usuario eligio que el color de la tira del draft signifique **el lado real
del mapa**, no "tu equipo contra el rival". Azul solo si de verdad esta en blue side.

Ese dato no existe hoy en el proyecto:

- `parseChampSelect` (`src/core/lcu.cpp:157-225`) lee diecisiete campos y ninguno indica
  lado. Todo es relativo: `myTeam`, `theirTeam`, `bans.myTeamBans`, `actorCellId`.
- `ChampSelectView` (`src/core/lcu.h:19-35`) no tiene campo de lado.
- `teamId` con los valores 100 y 200 solo aparece en partidas ya jugadas
  (`src/core/models.h:20`), servido por Riot en el json del historial
  (`src/core/lcu_history.cpp:56`). El proyecto no lo calcula.

Sondeo del cliente el 2026-09-07, con League en Lobby:

```text
/lol-gameflow/v1/gameflow-phase  -> "Lobby"
/lol-gameflow/v1/session         -> gameData.teamOne = [] , gameData.teamTwo = []
```

Los dos campos existen y llegan vacios fuera de partida. Nadie ha mirado que traen durante
un champion select, y este repositorio no guarda ni un volcado de ese endpoint.

Sin esta comprobacion, TASK-0010 no puede pintar el color con el significado que el usuario
pidio. Con ella, o se confirma el dato, o se sabe que hay que volver al color relativo.

## Sondeo del 2026-09-07, con una partida en curso

Se leyo `/lol-gameflow/v1/session` con el cliente en `InProgress`:

```text
phase: InProgress
teamOne  n=5   claves: championId, puuid, selectedPosition, selectedRole,
                       summonerId, summonerName, teamOwner, teamParticipantId
teamTwo  n=5   las mismas
```

Las dos listas se llenan. **Ningun campo dice el lado**: no hay `teamId`, ni `side`, ni
`blue`, ni `red`. Solo los nombres `teamOne` y `teamTwo`.

Queda una hipotesis por demostrar: que `teamOne` sea siempre el equipo 100 (blue). El
proyecto no puede darla por buena sin medirla.

La medida es barata y no necesita otra partida entera. En una partida:

1. Guardar en que lista esta el puuid del usuario, `teamOne` o `teamTwo`.
2. Cuando la partida acabe, leer su `teamId` del historial, que Riot ya sirve
   (`src/core/lcu_history.cpp:56`).
3. Si `teamOne` coincide con 100 en varias partidas, la hipotesis se sostiene. Con una sola
   partida no: hace falta al menos una de cada lado.

Aviso de privacidad: ese endpoint trae `puuid`, `summonerName` y `summonerId` de los diez
jugadores. Solo se puede usar la pertenencia del usuario a una de las dos listas. Ninguna
identidad se guarda ni se muestra (PRD 17.2, RF-CS-001).

## What to do

Esta tarea la corre el usuario. El agente no puede entrar en un champion select.

1. Con el binario de `build/x64-debug`, entrar en una cola y llegar a champion select.
2. Durante el draft, correr `RiftLoop.Analyzer.exe --dump-champselect <archivo>`
   (TASK-0015). Guarda la sesion anonimizada.
3. En el mismo draft, guardar tambien `/lol-gameflow/v1/session`.
4. Decir de que lado del mapa estaba la partida, para poder contrastar el volcado con la
   realidad. Sin ese dato observado, el volcado no se puede interpretar.

## Done when

- Existe un volcado anonimizado de un champion select real en `tests/fixtures/`.
- El volcado responde una de estas dos cosas, por escrito:
  - el cliente expone el lado, y se nombra el campo exacto que lo dice;
  - el cliente no lo expone, y entonces el usuario decide si el color pasa a ser relativo
    o si se busca el dato por otra via.
- El veredicto del usuario con lo que observo en la partida.

## Not covered

El dibujo de la tira (TASK-0010) y el modelo de ranuras (TASK-0016). Esta tarea solo
consigue el dato que decide el significado del color.

Los comandos que hacen falta para correrla son TASK-0015. Sin esa tarea hecha, esta no se
puede empezar.

No se guarda ninguna identidad. El volcado sale anonimizado del propio comando (PRD 16, 29).
