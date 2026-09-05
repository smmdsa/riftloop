---
id: TASK-0007
title: Reportar al harness los tres defectos de portabilidad a Windows
work: S
eye: NONE
owner: agent
---


# TASK-0007 — Reportar al harness los tres defectos de portabilidad a Windows

## Why

Instalar el harness en este repositorio fue la primera vez que salio del suyo, y saco
tres defectos que impiden que funcione en Windows sin parches locales. El autor es el
usuario; el informe le sirve mas que el parche.

1. `git clone` del harness falla sin `core.longpaths=true`: las rutas de sus propias
   tareas pasan de 260 caracteres.
2. `python3` en Windows es el stub de la Microsoft Store. Abre la tienda en vez de
   ejecutar Python, asi que los hooks mueren en silencio.
3. `manifest.REQUIRED_HOOKS` compara la cadena literal `python3 -m harness hook`, de
   modo que cambiar el interprete deja el `doctor` en "damaged". Los puntos 2 y 3
   juntos dejan Windows sin salida limpia.

El parche local aqui fue `tools/bin/python3`, un shim que reenvia a `python`, con los
hooks anteponiendo ese directorio al PATH.

## What to do

1. Abrir los tres puntos en el repositorio del harness, con el detalle de arriba.
2. Proponer la solucion de raiz: que el instalador resuelva el interprete probando
   `python3`, luego `python`, luego `py -3`, y guarde el elegido en el manifiesto, en
   vez de exigir una cadena fija.
3. Mencionar tambien que `taskkill` en la lista deny rompe el ciclo de build en
   repositorios Windows con ejecutables, porque el linker falla mientras corren.

## Done when

- Los tres puntos estan escritos donde el harness los recoge.
- La propuesta del interprete resoluble esta escrita con su porque.

## Not covered

Arreglar el harness. Esta tarea informa; el arreglo vive en su repositorio.
