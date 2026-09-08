---
id: TASK-0028
title: Publicar el repositorio en GitHub sin ningun dato personal
work: M
eye: GLANCE
owner: agent
---


# TASK-0028 — Publicar el repositorio en GitHub sin ningun dato personal

## Why

El repositorio vive solo en la maquina del usuario. Un disco que muere se lleva 39 commits,
el PRD y el WORK-LOG. El usuario pidio publicarlo en `git@github.com:smmdsa/riftloop.git`.

Un repositorio publico no se puede despublicar. GitHub indexa, y las copias en cache
sobreviven al borrado. Asi que la publicacion no es un `push`: es un `push` despues de
comprobar que ni un dato personal viaja con el.

Medido el 2026-09-07 antes de publicar, sobre los 34 commits de entonces:

- El nick de invocador del usuario aparecia en `WORK-LOG.md` y en un `CHECK` de la suite.
- Una ruta de Documentos con el nombre de usuario de Windows aparecia en `WORK-LOG.md`.
- Una ruta de WSL con ese mismo nombre aparecia en un documento de sesion y en
  `TASK-0006`.

Ninguno es una credencial. Los cuatro son datos del propietario y ninguno aporta nada al
lector.

## What to do

1. Comprobar que se publica: claves de la API de Riot, identidades ajenas en los fixtures,
   binarios, bases de datos y clips.
2. Quitar los datos personales del arbol de trabajo.
3. Quitarlos tambien del historial. Un `push` sube los 39 commits, no el ultimo.
4. Escribir el mapa de SHA de la reescritura, porque `docs/ACTIVITY.md` y los documentos
   de sesion citan los viejos.
5. `git remote add origin` y `git push -u origin main`.

## Done when

- `git grep -i <nombre-de-usuario> $(git rev-list main)` no devuelve nada, con el nombre
  real en el sitio del marcador. Esta hoja lo escribe asi por la misma razon que mide.
- `git ls-tree -r --name-only main` no lista ningun `.mp4`, `.rofl`, `.exe` ni `.sqlite`.
- El unico `RGAPI-` del arbol es el literal de test que se autodescribe.
- La suite pasa entera sobre el arbol publicado.
- `git ls-remote origin` devuelve el mismo SHA que `git rev-parse main`.
- **El usuario abre <https://github.com/smmdsa/riftloop> y dice que lo ve bien.**

## Not covered

- **La proteccion de la rama `main` en GitHub.** La regla de que solo se entra por PR queda
  escrita en `CLAUDE.md`, pero GitHub todavia acepta un push directo. Falta decidir
  `enforce_admins` y aplicar la proteccion.
- **`refs/original/refs/heads/main` sigue en la maquina del usuario** con el historial
  viejo. No se subio, y un `push` normal no lo sube. Un `git push --all` o `--mirror` si.
- **El email del autor de los 39 commits.** Es el del usuario, y no se toco.
- **Las ramas que no son `main`.** La rama vendorizada del harness upstream se quedo fuera
  a proposito.

## Verdict

- 2026-09-07 · by user · "perfecto repo inicializado y pusheado correctamente"
