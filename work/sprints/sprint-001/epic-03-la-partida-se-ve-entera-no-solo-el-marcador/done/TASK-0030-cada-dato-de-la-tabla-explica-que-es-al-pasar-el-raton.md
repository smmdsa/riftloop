---
id: TASK-0030
title: Cada dato de la tabla explica que es al pasar el raton
work: M
eye: GLANCE
owner: agent
---


# TASK-0030 — Cada dato de la tabla explica que es al pasar el raton

## Why

El usuario, el 2026-09-07: "el tooltip que implementamos debe de ser visible solo en la
columna de RANKING 1º-10º, luego cada otra parte que tenga su propio tooltip embellecido,
como nombre de spells, runas completas dibujando el arbol completo, los items mostrar su
nombre y data".

Hoy el tooltip del podio se abre sobre **toda la fila**, asi que tapa lo que el lector
mira y responde a una pregunta que no hizo. Y los iconos no dicen que son: una fila de seis
cuadrados solo informa a quien ya conoce los objetos de memoria.

El dato esta entero y offline, medido sobre `tests/fixtures/ddragon/`:

```text
item.json  -> name, plaintext, description, gold.total, stats, tags
runesReforged.json -> 4 slots por estilo, cada runa con name, shortDesc, longDesc, icon
summoner.json -> nombre y descripcion de cada hechizo
```

## What to do

1. El tooltip del podio se limita a la columna del puesto. Fuera de esa columna no aparece.
2. Zonas de tooltip por columna, cada una con su contenido:
   - **hechizo**: nombre y que hace;
   - **runa**: el **arbol completo** del estilo, con sus cuatro filas, marcando la que el
     jugador eligio;
   - **objeto**: nombre, coste, estadisticas y el texto de Data Dragon, con el HTML de Riot
     convertido a texto plano (`<mainText>`, `<stats>`, `<attention>`, `<br>`);
   - **KDA** y **nivel/CS/oro**: que mide cada numero y por minuto cuando aplica.
3. Un solo mecanismo: `RVItem` declara sus zonas y el control busca la que contiene el
   cursor. Nada de una rama por columna en el pintor.
4. Los iconos del tooltip salen del cache de siempre (`src/core/imagecache.h`). Sin red
   nueva.

## Done when

- El tooltip del podio solo sale sobre el puesto.
- Cada objeto, runa y hechizo muestra su nombre al pasar por encima.
- El tooltip de runa dibuja las cuatro filas del arbol y marca la elegida.
- El texto de un objeto se lee sin etiquetas HTML dentro.
- El veredicto del usuario.

## Not covered

Traducir nada: Data Dragon ya llega en el idioma del cliente (`resolveDataLocale`).

Los datos que el importador no guarda, que son TASK-0027.

## Verdict

- 2026-09-08 · by user · "perfecto todo esto funciono"
