---
id: TASK-0025
title: Rango de los rivales en el historial, no en champion select
work: M
eye: GLANCE
owner: agent
---


# TASK-0025 — Rango de los rivales en el historial, no en champion select

## Why

El usuario lo pidio el 2026-09-07: "elo de la partida promedio, elo de cada oponente, como
los datos que brindan op gg, porofessor".

Se puede, con dos condiciones duras, y el titulo de esta tarea es la primera.

**Donde si y donde no.** PRD 17.2 clasifica "Scouting de identidad oculta" como **rojo, no
implementar**, y PRD 7 prohibe "hacer scouting invasivo de jugadores deliberadamente
anonimizados". En champion select los rivales estan anonimizados: ahi no se toca. En el
historial de una partida jugada, esos nombres ya son publicos para el usuario. Esa mitad es
verde.

**Como lo hacen los demas.** op.gg y porofessor usan la API oficial de Riot con clave de
produccion. No hay scraping. Este proyecto tampoco lo haria: el CLAUDE.md ya prohibe el
scraping de terceros.

**Que hace falta.** Dos llamadas, no una: `GET /lol/summoner/v4/summoners/by-puuid/{puuid}`
da el `summonerId`, y `GET /lol/league/v4/entries/by-summoner/{summonerId}` da tier,
division y LP. Necesita una clave de Riot, que hoy es un fallback de CLI
(`Config::loadApiKey`), y una clave de desarrollo tiene cuota corta.

## What to do

1. Antes de escribir codigo, el usuario decide dos cosas. Esta tarea no empieza sin ellas:
   - si acepta que el cliente gaste cuota de su clave en consultar el rango de nueve
     jugadores por partida;
   - si quiere guardar ese rango o solo mostrarlo y olvidarlo.
2. Solo en la vista de una partida ya jugada. Ninguna ruta desde champion select puede
   llamar a esto, y un test debe probarlo.
3. Cachear por jugador con fecha, y no repetir la consulta de una partida ya resuelta.
4. El elo promedio de la partida se calcula **con los tier y division oficiales**. Si falta
   el rango de alguien, se dice cuantos faltan en vez de promediar lo que hay y llamarlo el
   promedio.
5. Sin clave, la vista lo explica y ofrece donde ponerla. No es un error: es una funcion
   que necesita algo que el usuario no ha dado.

## Done when

- El historial de una partida muestra el rango de los diez, o dice de cuantos no lo sabe.
- Un test comprueba que ninguna ruta de champion select llega a este codigo.
- Sin clave, la vista lo explica y nada falla.
- El veredicto del usuario.

## Not covered

Champion select, por politica. No es una limitacion tecnica y no se resuelve mas adelante.

Cualquier MMR estimado o puntuacion propia. Solo tier y division oficiales.

El rango propio, que sale gratis del cliente en TASK-0024.
