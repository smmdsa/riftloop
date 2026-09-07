---
id: TASK-0013
title: Maquetar el Draft Lab en dos columnas y usar el alto libre de la pagina
work: M
eye: GLANCE
owner: agent
---


# TASK-0013 — Maquetar el Draft Lab en dos columnas y usar el alto libre de la pagina

## Why

El usuario lo dijo así: "misma pagina hay espacio no utilizado, podemos de hecho mejorar
mucho el maquetado de toda la información que le mostramos al usuario, runas, items y
otra data importante, de forma clara".

El espacio libre está medido. La ventana es fija y las coordenadas son literales:

- `src/desktop/desktop_main.cpp:107-108` — `kWinW = 1560, kWinH = 900`, `kSidebarW = 200`.
- `src/desktop/desktop_main.cpp:1776-1778` — `X = kSidebarW + 28` (228), `Y = 108`,
  `W = kWinW - kSidebarW - 72` (1288).
- `src/desktop/desktop_main.cpp:1913` — `mkReport(4, X, Y + 150, W, 376, IDC_DRAFT_VIEW)`.

La vista de informe acaba en el píxel 634 de una ventana de 900: **266 px de alto sin
usar**. A lo ancho, la fila de combos más larga llega a `X + 70 + 4 * 168 = 898` de 1288:
**390 px de ancho sin usar** a la derecha.

Y todo el contenido cae en una sola columna, porque `ReportView` es una lista vertical:
`RVItem` (`src/desktop/ui.h:63-84`) no tiene rejilla. Runas, hechizos, items y quiz se
apilan uno tras otro en `rvPlan` (`src/desktop/desktop_main.cpp:363-401` y siguientes),
así que los items quedan fuera de pantalla y hay que rodar para verlos.

## What to do

1. Partir el área de contenido del Draft Lab en dos vistas de informe lado a lado, en vez
   de una. Izquierda para la decisión (tira de draft y runas), derecha para el plan de
   compra y el quiz. La jerarquía sale del PRD §10.1: primero la decisión inmediata.
2. Subir el alto de las dos vistas hasta el borde inferior útil de la ventana. Los 266 px
   libres pasan a contenido.
3. Partir `rvPlan` (`src/desktop/desktop_main.cpp:363`) en dos funciones, una por columna.
   No cambiar lo que cada bloque dice: esta tarea mueve y ordena, no reescribe textos.
4. Escribir los números nuevos como constantes con nombre junto a `kWinW` y `kSidebarW`,
   no como literales repartidos. Hoy cada control lleva su offset a mano.
5. No añadir `WM_SIZE`. La ventana es de tamaño fijo y ninguna página del proyecto se
   reajusta. Un sistema de layout es otra tarea, y nadie la ha pedido.

## Done when

- Una captura del Draft Lab con un plan cargado muestra runas e items a la vez, sin rodar.
- El borde inferior del contenido queda a menos de 40 px del borde de la ventana.
- Las otras cuatro páginas se ven igual que antes. Una captura de cada una lo muestra.
- El veredicto del usuario sobre las capturas.

## Not covered

El post-match y su reproductor de clips (`src/desktop/player.cpp`). Esta tarea solo toca
la página 4.

Tampoco cubre el contenido: qué dice cada bloque de runas es TASK-0011, y la tira de
picks y baneos, con la retirada de los desplegables, es TASK-0010. Esta tarea coloca lo
que exista.

Tampoco cubre una ventana redimensionable ni WinUI 3, que sigue siendo la deuda conocida
del PRD §14.1.
