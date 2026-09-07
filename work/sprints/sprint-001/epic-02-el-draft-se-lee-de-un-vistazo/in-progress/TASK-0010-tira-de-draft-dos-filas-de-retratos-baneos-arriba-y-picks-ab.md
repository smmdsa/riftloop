---
id: TASK-0010
title: "Tira de draft: retratos que se rellenan solos, y fuera los nueve desplegables"
work: L
eye: GLANCE
owner: agent
---


# TASK-0010 — Tira de draft: retratos que se rellenan solos, y fuera los nueve desplegables

## Why

El usuario vio la pantalla el 2026-09-07 y dijo: "sigue siendo unos dropdowns, el objetivo
es que sea auto fillable por los picks en si, y que no me de ahi para que el usuario lo
complete, simplemente todo se debe completar con el diseño e icono de personaje correcto
para picks y bans".

Antes habia pedido "un lado nuestro team vs el otro team (red vs blue team), con dos filas
los baneados arriba y los picks abajo... con un peso visual bien ordenado".

Los nueve desplegables no son un paso previo al diseño. Son el diseño que hay que quitar.

Estado de hoy:

- `src/desktop/desktop_main.cpp:1901-1908` — cuatro combos `Aliados` y cinco `Rivales`,
  `CBS_DROPDOWNLIST` sin `CBS_OWNERDRAW`: texto puro, sin un solo retrato.
- `src/desktop/desktop_main.cpp:1891-1896` — los combos `Rol` y `Tu campeón`, del mismo
  tipo.
- `src/desktop/desktop_main.cpp:1910` — `mk(4, L"STATIC", L"Baneos: -", ...)`.
- `src/desktop/desktop_main.cpp:988-1017` — `applyDraftView` ya rellena todo eso solo,
  pero unicamente mientras el agente esta en `ChampSelect`
  (`src/agent/agent_main.cpp:281`). Fuera de un draft el usuario ve nueve huecos vacios y
  la unica lectura posible es "rellename".

Lo que hace falta ya esta en el proyecto:

- Los retratos: `src/desktop/ui.h:41-56` (`icons::get("champ", ...)`), y el Draft Lab ya
  se repinta cuando llega un lote (`src/desktop/desktop_main.cpp:2316-2320`).
- El lado de cada baneo: TASK-0009 lo dejo separado en `bansAllies`, `bansEnemies` y
  `bansUnknown`.

El obstaculo es que `ReportView` es una lista de una sola columna: `RVItem`
(`src/desktop/ui.h:63-84`) tiene un icono, un texto, un texto derecho y una sangria. Una
fila de cinco retratos enfrentada a otra fila de cinco no se puede expresar con eso.

## What to do

1. Anadir `RVKind::TeamStrip` a `src/desktop/ui.h:61`. Un item de esa clase lleva dos
   listas de hasta cinco entradas, una por lado, cada entrada con `iconKind`, `iconId`,
   `iconUrl` y un estado: `pick`, `ban` o vacio.
2. Dibujarlo en `src/desktop/ui.cpp`: retratos de 40 px, el lado aliado a la izquierda y
   el enemigo a la derecha, con un canal central para la etiqueta de la fila. Un hueco sin
   pick se dibuja como marco punteado y mantiene su sitio: la fila no debe descolocarse
   cuando llega el retrato que falta.
3. Color por lado con los tokens que ya existen en `src/desktop/ui.h:14-25`. El usuario
   decidio el 2026-09-07 que el color signifique **el lado real del mapa**: azul solo si
   la partida es blue side. Blue side `kAccent`, red side `kDanger`. No inventar colores
   nuevos. Si TASK-0017 demuestra que el cliente no expone el lado, esta decision vuelve
   al usuario antes de dibujar nada.
4. Un baneo se dibuja al 45 % de opacidad con una diagonal encima. Un pick va a plena
   opacidad. La diferencia debe leerse sin leer la etiqueta.
5. Dos items `TeamStrip`: el primero con los baneos, el segundo con los picks. El campeon
   del usuario lleva un marco de 2 px en `kGood` dentro de la fila aliada. Los baneos de
   `bansUnknown` van en el canal central, sin lado: el cliente no dijo de quien son y la
   UI no lo inventa (PRD 3.3).
6. **Quitar los nueve combos de campeon y la etiqueta `IDC_DRAFT_BANS`.** Quitar tambien
   `Rol` y `Tu campeon`: el champion select los da. Con ellos se van
   `draftContextFromUi` (`src/desktop/desktop_main.cpp:968-983`), que lee los combos, y
   las llamadas de relleno de `src/desktop/desktop_main.cpp:2394-2395`. El contexto pasa a
   salir del ultimo draft recibido por IPC.
7. Estado sin draft: la pantalla dice que se rellena en champion select y no ofrece nada
   que completar. El subtitulo de la pagina ya lo anuncia
   (`src/desktop/desktop_main.cpp:96-103`).
8. Los botones `Top 3`, `Plan pregame` y `Probar quiz` se quedan, pero se apagan mientras
   no haya draft: sin picks no hay nada que recomendar.

## Done when

- Una captura durante un champion select real muestra dos filas de retratos: baneos
  arriba, picks abajo, un lado por equipo.
- No queda ni un desplegable en la pagina.
- Un lector que no conoce la aplicacion dice que campeones son suyos y cuales del rival,
  sin leer texto.
- Un baneo y un pick del mismo campeon se distinguen a simple vista.
- Fuera de champion select la pagina explica que se rellena sola y no pide nada.
- El veredicto del usuario sobre las capturas.

## Not covered

El resto del maquetado de la pagina: runas, items y el espacio libre son TASK-0013.

El orden de pick y el temporizador del draft. RF-CS-001 los pide como input y esta tarea
no los toca.

Depende de tres tareas:

- **TASK-0016**, que conserva la ranura de cada pick. Sin ella los retratos saltan cuando
  llega un pick tardio, y no hay hueco que reservar.
- **TASK-0015**, que inyecta un draft de prueba. Al quitar los combos, sin ella la unica
  forma de ver esta pantalla es entrar en un champion select real.
- **TASK-0017**, que dice si el lado del mapa existe como dato. El color no se puede pintar
  con el significado que el usuario pidio hasta que esa tarea responda.
