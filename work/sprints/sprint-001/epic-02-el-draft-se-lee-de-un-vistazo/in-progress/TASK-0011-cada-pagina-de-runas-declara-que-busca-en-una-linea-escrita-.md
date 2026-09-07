---
id: TASK-0011
title: Cada pagina de runas declara que busca, en una linea escrita por el core
work: S
eye: GLANCE
owner: agent
priority: 1
priority-by: user
priority-date: 2026-09-07
priority-why: "Primero lo barato: 0009, 0011 y 0014"
---


# TASK-0011 — Cada pagina de runas declara que busca, en una linea escrita por el core

## Why

El usuario pidió: "si hay 2 sets de runas, el usuario debe de ver que impacto tiene o
busca cada set y poder decidir si cargar uno u otro set". Esta tarea cubre la primera
mitad. La segunda es TASK-0012.

El plan ya trae dos páginas. `RunePlan` tiene `main` y `situational`
(`src/core/models.h:242-253`), y `src/core/planner.cpp:300-332` elige la alternativa
contra una amenaza concreta. Pero ninguna de las dos dice para qué sirve. `RunePage`
(`src/core/models.h:235-240`) solo tiene `name`, los estilos, los perks y `reasons`.

En pantalla la diferencia queda enterrada. La principal se pinta con icono por runa; la
alternativa es una línea gris de texto:

- `src/desktop/desktop_main.cpp:395-401` — `L"Alternativa: " + styleName + L" secundaria con " + perkName + ...`.

El usuario ve dos bloques con distinto peso visual y ninguna frase que diga qué gana con
cada uno. RF-RUN-002 lo exige: "Una página alternativa debe cambiar por una razón
estratégica real, no para aparentar variedad".

La frase la escribe el core, no la UI. PRD §12.4: la UI consume el objeto y no compone
explicaciones por su cuenta. Hoy ya se cumple: las razones se arman en
`src/core/planner.cpp:215` y `src/core/meta.cpp:113-127`, y la UI solo las imprime
(`src/desktop/desktop_main.cpp:386`).

## What to do

1. Añadir `std::string intent;` a `RunePage` (`src/core/models.h:235-240`). Una línea,
   máximo 90 caracteres, en la voz del PRD §10.3: habla de la decisión, no acusa.
2. Rellenarlo en `src/core/planner.cpp` para las dos páginas. La principal declara el
   patrón que persigue. La alternativa declara la amenaza que responde, tomada del
   `AltCandidate` que ganó (`src/core/planner.cpp:300-332`).
3. Serializarlo en `src/core/serial.h:62-77`, junto al resto de `RunePage`.
4. Pintar `intent` bajo la cabecera de cada página, antes de las razones, en `rvPlan`
   (`src/desktop/desktop_main.cpp:373-401`). La alternativa deja de ser una línea gris y
   recibe su propia `RVKind::Section` con sus runas en `RVKind::IconRow`, igual que la
   principal.
5. La regla de las tres razones sigue viva: `src/core/planner.cpp:340` recorta a 3.
   `intent` no cuenta como razón y no consume ese presupuesto.

## Done when

- Una captura del plan de un campeón con alternativa muestra dos bloques, cada uno con su
  frase de intención y sus runas con icono.
- Las dos frases dicen cosas distintas. Si las dos dicen lo mismo, la alternativa no
  debería existir y el caso se anota.
- Un plan sin alternativa sigue mostrando `noAlternativeReason`
  (`src/core/models.h:242-253`) y no inventa una segunda página.
- El veredicto del usuario sobre la captura.

## Lo medido, 2026-09-07

Suite: 3347 comprobaciones, 0 fallos. El barrido de campeones y composiciones que ya
existia (`tests/tests_main.cpp:396-411`) ahora exige que ninguna pagina salga sin frase, y
que las dos paginas de un plan nunca digan lo mismo.

La prueba de que el test se pone rojo: al quitar `alt.intent = c.intent;` de
`src/core/planner.cpp`, la alternativa hereda la frase de la principal por la copia
`RunePage alt = page;`, y la suite dio 8 fallos de
`rv.situational->intent != rv.main.intent`. Ese es el defecto real que el test guarda.

Salida de `--plan Ahri MIDDLE Sivir Jinx Caitlyn Ashe Kalista`:

```text
  Busca escalado y alcance de habilidades, y no cede dano: nada en la comp rival lo obliga
  ...
  [alt] Cambia dano por regeneracion: ganas los intercambios que se alargan
```

Se anadio la frase al CLI del Analyzer (`src/analyzer/analyzer_main.cpp:604` y `:613-618`),
que no estaba en el plan de la tarea. Sin eso no hay forma de comprobar el cambio sin
abrir la UI, y CLAUDE.md llama al CLI "la via rapida para probar sin UI".

## Defecto encontrado, fuera de esta tarea

`Ddragon::teamTraits` clasifica como burst una composicion de DPS sostenido puro. Las
etiquetas secundarias suman: `src/core/ddragon.cpp:338-339` da `burst += 0.6` por cada
Mage y `+= 1.0` por cada Assassin. Ashe, Sivir, Kayle, KogMaw y Vayne son cinco tiradores,
y suman burst 2.2 contra un umbral de 1.8, con sustainedDps por encima de 4.

`src/core/planner.cpp:196-197` decide con umbrales absolutos y sin comparar entre si, y el
candidato "vs DPS sostenido" es el tercero de la lista, asi que no gana nunca mientras
haya algo de burst. El resultado es una pagina que dice "el burst rival castiga los
intercambios" contra una composicion sin un solo asesino real.

El defecto ya existia. Esta tarea solo lo saca a la primera linea del bloque.

## Not covered

Aplicar la alternativa al cliente. Eso es TASK-0012.

Tampoco cubre el cálculo de la alternativa: `src/core/planner.cpp:300-332` se queda como
está. Esta tarea solo hace visible lo que ya decide.
