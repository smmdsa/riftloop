---
id: TASK-0016
title: Conservar la posicion de cada pick y distinguir el fijado del hover
work: S
eye: NONE
owner: agent
---


# TASK-0016 — Conservar la posicion de cada pick y distinguir el fijado del hover

## Why

El usuario lo pidio el 2026-09-07: "que se vayan dando a medida que ocurren... los picks a
medida que van fijando deben de ir apareciendo con su icono, del lado correcto".

El modelo de hoy no lo permite. `src/core/lcu.cpp:165-185` mete los picks con `addUnique`,
que descarta el cero y compacta la lista:

```cpp
void addUnique(std::vector<int>& out, int id) {
    if (id <= 0) return;
    ...
}
```

Dos consecuencias medidas sobre ese codigo:

1. **La posicion se pierde.** Si el aliado de la celda 3 fija y el de la celda 1 no, la
   lista tiene un elemento y nada dice de que celda es. No se puede dibujar el hueco
   correcto ni dejarlo reservado.
2. **Los retratos ya puestos saltarian.** El parser recorre `myTeam` en orden de celda. Si
   el de la celda 1 fija despues, entra antes en la lista y desplaza al que ya estaba. Un
   pick nuevo moveria a los demas, que es lo contrario de "ir apareciendo".

Tercer punto, de honestidad: para los aliados el parser ya usa `championPickIntent`
(`src/core/lcu.cpp:167`), o sea el hover, antes de que nadie fije. Es informacion real que
el cliente muestra, pero si se pinta igual que un pick confirmado, la pantalla afirma algo
que todavia puede cambiar (PRD 3.3).

La tuberia progresiva ya existe: el agente relee cada segundo y solo manda mensaje cuando
la firma del draft cambia (`src/agent/agent_main.cpp:146-147`). Falta el dato.

## What to do

1. En `ChampSelectView` (`src/core/lcu.h:19-35`), cambiar `allyChampionIds` y
   `enemyChampionIds` por listas de cinco ranuras, cada una con el id del campeon (0 =
   vacio) y su estado: `locked` o `hover`. El jugador local ocupa su ranura, no va aparte.
2. Ordenar cada lado por `cellId` ascendente, siempre. La ranura de un jugador no cambia
   durante el draft.
3. En `DraftContext` (`src/core/models.h:203-227`), guardar las mismas ranuras y exponer
   `allyPicks()` y `enemyPicks()`, que devuelven la lista compacta de campeones conocidos.
   Los motores siguen preguntando lo mismo que hoy. Una sola fuente de verdad, no dos
   listas en paralelo.
4. Migrar los ocho consumidores del recuento: `src/core/models.h:225-227` (`closed`,
   `knownPicks`, `missingPicks`), `src/core/recommend.cpp:143`, `src/core/planner.cpp:511`
   y `src/agent/agent_main.cpp:154-155`, `:185`, `:191-192`.
5. Serializar las ranuras en `draftView()` (`src/agent/agent_main.cpp:103-115`) con su
   estado, y leerlas en `applyDraftView` (`src/desktop/desktop_main.cpp:988-1017`).
6. Tests: un fixture donde la celda 3 fija y la celda 1 no, y una segunda lectura donde la
   celda 1 fija despues. La ranura de la celda 3 debe ser la misma en las dos lecturas.

## Done when

- `riftloop_tests.exe` pasa, y el test nuevo falla si se vuelve a compactar la lista.
- Un pick que llega tarde no cambia la ranura de ninguno de los que ya estaban.
- Un hover y un pick fijado se distinguen en el dato, no solo en el dibujo.
- `closed()`, `knownPicks()` y `missingPicks()` dan los mismos numeros que hoy para un
  draft completo y para uno a medias.

## Lo medido, 2026-09-07

Suite: 3373 comprobaciones, 0 fallos.

La prueba de que el test se pone rojo: al volver a compactar las ranuras en
`src/core/lcu.cpp`, la suite dio 4 fallos, y los tres que importan son
`allySeats[3].championId == 412`. Ese es el error exacto que la tarea arregla: la celda 3
fija primero, la celda 1 fija despues, y con la lista compacta el campeon de la celda 3 se
movia de sitio.

El test tambien cubre un caso que no estaba en el plan: el cliente puede listar el equipo
en cualquier orden. La ranura sale del `cellId`, no de la posicion en el array, y hay un
caso con el array barajado que lo comprueba.

Decisiones de diseno:

- `allyChampions()` y `enemyChampions()` son **metodos derivados**, no campos. No hay una
  segunda copia de la lista que pueda quedarse desfasada. Los 39 usos del modelo viejo se
  migraron con el compilador como red: un olvido no compila.
- `localChampionId()` y `localHoverChampionId()` tambien se derivan de la ranura local. Un
  hover devuelve 0 en el primero: un hover no es un pick.
- La firma de de-dup del agente (`src/agent/agent_main.cpp:159-166`) firma ranura y estado.
  Un hover que pasa a fijado cambia la firma aunque el campeon sea el mismo, asi que el
  Desktop repinta.
- `addUnique` quedo sin usar y se quito.

## Lo que se arreglo sobre la marcha

Al cambiar el json de IPC, `applyDraftView` seguia leyendo los campos `allies` y `enemies`,
que el agente ya no envia. El autorrelleno de los combos quedaba roto entre esta tarea y
TASK-0010. Se migro el Desktop a las ranuras en el mismo cambio
(`src/desktop/desktop_main.cpp:999-1030`), y las ranuras quedan guardadas en el estado
para que TASK-0010 las dibuje.

## Not covered

El dibujo de la tira, que es TASK-0010. Esta tarea solo lleva el dato hasta la UI.

El lado del mapa (blue side o red side). El usuario lo eligio como significado del color
el 2026-09-07, pero ese dato no existe hoy en champion select: `parseChampSelect` no lee
ningun campo que lo indique y `teamId` solo aparece en partidas ya jugadas
(`src/core/models.h:20`). TASK-0017 dice si existe. Si existe, el campo se anade a
`ChampSelectView` en aquella tarea, no en esta.

El orden de pick y el temporizador del draft.
