---
id: TASK-0006
title: Levantar el stack RAG y comprobar que el brief deja de buscar a ciegas
work: S
eye: GLANCE
owner: agent
---


# TASK-0006 — Levantar el stack RAG y comprobar que el brief deja de buscar a ciegas

## Why

`session open` abre con `RAG: BROKEN — this session searches blind`. El indice vive en
dos contenedores (`infra/rag/`) que nunca se levantaron en esta maquina. Docker 29.2.1
esta disponible. Sin indice, las sesiones buscan a ciegas sobre un repositorio que ya
lleva un WORK-LOG de casi 900 lineas y quince bloques de trabajo.

## What to do

1. Leer `infra/rag/README.md` y decidir si se usa la variante con GPU
   (`docker-compose.gpu.yml`) o la normal.
2. Levantar el stack con `infra/rag/up.sh` y medir cuanto pesan las imagenes.
3. Comprobar los puertos con `python -m harness ports`.
4. Volver a abrir un brief y ver que la primera linea deja de avisar.

## Done when

- `python -m harness session open` no imprime la linea `RAG: BROKEN`.
- Una consulta de prueba devuelve algo del repositorio, no una lista vacia.
- El informe dice cuanto ocupan las imagenes en disco.

## Not covered

La calidad de las respuestas del indice. Esta tarea solo comprueba que responde.

## Nota del 2026-09-05, tras subir a harness 0.2.0

El brief dejo de decir `RAG: BROKEN` y ahora avisa de que el 62 % del indice son trozos
huerfanos. Pero el demonio de Docker no responde y `harness ports` da 8410, 8411 y 8412
libres, asi que ese indice **no es el stack del harness**: hay un `qmd` accesible por otra
via, con contenido que puede no ser de este repositorio. Antes de levantar nada, averiguar
que responde y sobre que corpus. Indexar dos repositorios en el mismo indice explicaria
los huerfanos.

## Nota del 2026-09-05, sesion de la tarde: hecho, falta el ojo

La sospecha era correcta. Los contenedores de 8410 a 8412 montaban
`~/personal/harness-rag-pipeline`, el repositorio del propio harness dentro
de WSL. Ni un solo archivo de este proyecto estaba indexado. Los huerfanos eran de aquel
corpus, no de este.

Lo hecho, con el detalle en `CLAUDE.md`:

- Stack propio en WSL, en los puertos 8510, 8511 y 8512, para convivir con el otro
  repositorio que usa los 8410 por defecto.
- Sin build: las imagenes se etiquetaron desde las que ya existian, y los modelos de
  embeddings se copiaron de un volumen a otro.
- `rag config` anadio la coleccion `repo-code` que faltaba, con `third_party` y `build`
  excluidos.

Medido: `pitero-rag:cpu` 615 MB y `pitero-board:latest` 338 MB en imagenes; los volumenes
suman 333,6 MB de modelos, 12,45 MB de indice y 36,9 kB de cache del board. El indice
tiene 37 documentos de docs, 57 de codigo, 4 de memoria y **0 trozos huerfanos**. Los tres
puertos responden 200 en `/health`.

Queda el ojo del usuario. Y esta tarea sigue en el backlog porque no hay epica donde
ponerla: EP-01 es el performance harness y no le corresponde.
