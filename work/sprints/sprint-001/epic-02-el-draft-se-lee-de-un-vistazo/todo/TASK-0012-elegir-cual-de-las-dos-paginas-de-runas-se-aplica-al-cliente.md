---
id: TASK-0012
title: Elegir cual de las dos paginas de runas se aplica al cliente
work: M
eye: RUN
owner: agent
---


# TASK-0012 — Elegir cual de las dos paginas de runas se aplica al cliente

## Why

El usuario pidió "poder decidir si cargar uno u otro set". Hoy no se puede: la
alternativa se calcula, se muestra y no es aplicable.

El botón siempre escribe la página principal. `rp.situational` no se asigna nunca a
`lastRunePage` en todo `src/`:

- `src/desktop/desktop_main.cpp:139` — `RunePage lastRunePage;`
- `src/desktop/desktop_main.cpp:1075` — `g->lastRunePage = rp.main;`
- `src/desktop/desktop_main.cpp:1739` — lo mismo en la ruta IPC.
- `src/desktop/desktop_main.cpp:1139` — `applyRunePage(*g->db, lcu, g->dd, g->lastRunePage, pageName, force);`

La función que escribe ya es agnóstica. Recibe una `RunePage` cualquiera:

- `src/core/perkpages.h:92-93` — `WriteResult applyRunePage(Db&, Lcu&, const Ddragon&, const RunePage& wanted, const std::string& pageName, bool force);`

Lo que ata el botón a una sola página es la UI, no el core.

Segundo obstáculo: el nombre de la página no distingue variantes. Se arma solo con
campeón y rol (`src/desktop/desktop_main.cpp:1104-1105`), así que las dos páginas
compiten por el mismo hueco en el cliente.

## What to do

1. Guardar el plan completo, no una página suelta: cambiar `lastRunePage`
   (`src/desktop/desktop_main.cpp:139`) por el `RunePlan` y un índice de la elegida.
2. Hacer clicables los dos bloques de runas que TASK-0011 dibuja. `RVItem::action` ya
   existe (`src/desktop/ui.h:63-84`) y el padre lee la acción con `rvActionAt` tras
   `WM_RV_ACTION`. La página elegida se marca; la otra queda atenuada.
3. Por defecto queda elegida la principal. Un plan nuevo por IPC vuelve a la principal y
   descarta la elección anterior: el draft cambió y la alternativa puede no aplicar.
4. El botón "Aplicar runas" escribe la elegida. El diff previo
   (`src/desktop/desktop_main.cpp:1108`) se calcula sobre la elegida, no sobre `main`.
5. El nombre de página sigue siendo uno solo, el que empieza por "RiftLoop"
   (`src/core/perkpages.cpp`, RF-RUN-003). No se crea una segunda página en el cliente:
   se sobrescribe la misma con la elección. El diálogo de confirmación debe decir qué
   página se va a escribir, por su frase de intención.
6. La auditoría local registra cuál de las dos se escribió.

## Done when

- El usuario elige la alternativa, pulsa "Aplicar runas" y el cliente de League muestra
  esas runas en la página de RiftLoop.
- "Deshacer" devuelve el estado anterior en un clic, igual que hoy.
- Elegir la alternativa y no pulsar el botón no escribe nada.
- RiftLoop sigue gestionando una sola página del cliente. Un recuento de páginas antes y
  después da el mismo número.
- El feature flag `runeWriteEnabled` sigue apagado por defecto.
- El veredicto del usuario tras correr el cliente.

## Not covered

La frase de intención de cada página, que es TASK-0011. Sin ella el selector funciona
pero el usuario elige a ciegas, así que TASK-0011 va primero.

Tampoco cubre una tercera página ni un editor manual de runas.

Toca una escritura al cliente de League (RF-RUN-003, §17.2 amarillo). No añade ninguna
ruta que escriba sin un clic humano. Si la añadiera, dejaría de ser amarillo.
