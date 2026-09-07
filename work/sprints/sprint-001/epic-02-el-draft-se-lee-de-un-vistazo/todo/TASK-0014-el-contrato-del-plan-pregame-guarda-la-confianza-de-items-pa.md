---
id: TASK-0014
title: El contrato del plan pregame guarda la confianza de items para las runas
work: S
eye: NONE
owner: agent
priority: 1
priority-by: user
priority-date: 2026-09-07
priority-why: "Primero lo barato: 0009, 0011 y 0014"
---


# TASK-0014 — El contrato del plan pregame guarda la confianza de items para las runas

## Why

Defecto encontrado al mapear el motor de runas, el 2026-09-07. No lo pidió el usuario.

El contrato del plan pregame guarda la confianza del plan de **items** y un
`uncertainty_reason` constante, aunque el mismo contrato lleva dentro las runas, que
tienen su propia confianza:

- `src/desktop/desktop_main.cpp:1071-1074`:

```cpp
json contract = makeContract("pregame_plan", g->dd.version(),
                             json{{"champion", pi.champion}, {"role", pi.role}}, payload,
                             ip.confidence, "plan manual desde Draft Lab");
```

`ip` es el `ItemPlan`. La confianza de las runas se calcula aparte y se descarta aquí:

- `src/core/planner.cpp:280` — `plan.confidence = degraded ? "baja" : meta ? meta->sample.confidence : "baja";`
- `src/core/planner.cpp:295` — `if (!plan.draftClosed && plan.confidence == "alta") plan.confidence = "media";`

La insignia de pantalla sí usa la buena (`src/desktop/desktop_main.cpp:305`), así que el
usuario ve "confianza baja" mientras el registro auditable guarda otra cosa. PRD §30 pide
que el contrato permita auditar la recomendación. Un contrato que guarda la confianza de
otro subsistema no la permite.

Segundo hueco del mismo sitio: `RunePlan` (`src/core/models.h:242-253`) no tiene
`uncertaintyReason`, y `Top3` sí lo tiene (`src/core/models.h:229`). Por eso la razón de
incertidumbre del plan es una cadena fija escrita en la UI, que es justo lo que PRD §12.4
prohíbe.

## What to do

1. Añadir `std::string uncertaintyReason;` a `RunePlan` (`src/core/models.h:242-253`) y
   rellenarlo en `src/core/planner.cpp` donde ya se decide la confianza (líneas 280 y 295)
   y donde se degrada por parche o por plantilla (líneas 283-290).
2. Serializarlo en `src/core/serial.h:62-77`.
3. En `src/desktop/desktop_main.cpp:1071-1074`, guardar la confianza más baja de las dos
   partes del plan y unir las dos razones de incertidumbre. Un plan con runas de confianza
   baja no se registra como plan de confianza alta.
4. Un test que construya un plan con runas "baja" e items "alta" y compruebe que el
   contrato guarda "baja". El test debe fallar antes del cambio.

## Done when

- `riftloop_tests.exe` pasa, y el test nuevo falla con el código de hoy.
- La insignia de pantalla y el campo `confidence` del contrato guardado coinciden en los
  dos casos: runas peores que items, e items peores que runas.
- Ninguna razón de incertidumbre se escribe ya en `src/desktop/`.

## Not covered

Los otros campos de PRD §30 que `makeContract` (`src/core/contracts.h:10-27`) ya emite:
`recommendation_id`, `expires_at`, `policy_mode` y el resto se quedan como están.

Tampoco cubre el contrato de `top3`, que sí usa su propia confianza
(`src/desktop/desktop_main.cpp:1043`).
