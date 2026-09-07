---
id: TASK-0015
title: Inyectar un champion select de prueba para ver la pantalla sin jugar
work: S
eye: NONE
owner: agent
---


# TASK-0015 — Inyectar un champion select de prueba para ver la pantalla sin jugar

## Why

TASK-0010 quita los nueve desplegables del Draft Lab, porque el usuario pidio que la
pantalla se rellene sola: "que no me de ahi para que el usuario lo complete" (2026-09-07).

Esos combos eran, sin querer, la unica forma de ver la pantalla fuera de una partida. Hoy
el Desktop solo recibe un draft cuando el agente esta en `ChampSelect`
(`src/agent/agent_main.cpp:281`), y ese estado sale de una lectura del cliente
(`src/core/lcu.cpp:195-199`). Al quitar los combos, comprobar el Draft Lab exige entrar en
un champion select real.

Eso convierte cada comprobacion de la pantalla en una cola de emparejamiento. El ojo del
usuario ya es el cuello de botella de este proyecto, y esto lo empeora.

La via correcta no es devolver los controles manuales, que es lo que el usuario rechazo.
Es inyectar el mensaje que el agente enviaria, por el mismo canal que ya existe:

- `src/agent/agent_main.cpp:103-115` — `draftView()` arma el json.
- `src/desktop/desktop_main.cpp:1729-1751` — `processIpcQueue` recibe `top3` y `plan` y
  llama a `applyDraftView`.

Y hay una segunda pregunta que solo un draft real contesta. El proyecto no sabe hoy si el
usuario esta en blue side o red side: `parseChampSelect` no lee ningun campo que lo diga
(`src/core/lcu.cpp:157-225`), y `teamId` solo existe en partidas ya jugadas
(`src/core/models.h:20`). El 2026-09-07 se consulto el cliente en lobby: `gameData.teamOne`
y `gameData.teamTwo` existen en `/lol-gameflow/v1/session` pero llegan vacios fuera de
partida. Sin un volcado en champion select no se puede afirmar que ese dato exista.

## What to do

1. Un comando del Analyzer, `--dump-champselect <archivo>`, que guarde la respuesta cruda
   de `/lol-champ-select/v1/session` mientras hay un draft. **Anonimizar antes de escribir:
   fuera puuids, nombres de invocador y cualquier identidad** (PRD 16 y 29). Solo quedan
   ids de campeon, celdas, posiciones, acciones y baneos.
2. Un comando del Analyzer, `--fake-draft [archivo.json]`, que envie por el pipe de IPC el
   mismo mensaje que manda el agente. Sin argumento, usa un draft de ejemplo empotrado con
   los diez campeones y cinco baneos por lado.
3. El json de ejemplo vive en `tests/fixtures/`, no en el codigo, para que se pueda editar
   sin recompilar. El volcado anonimizado del paso 1 se convierte en ese fixture, y de paso
   da el primer fixture real del parser: hoy el unico es un objeto construido a mano en
   `tests/tests_main.cpp:1042-1117`.
4. El comando falla con un mensaje claro si el Desktop no esta escuchando. Regla de
   escritura 11: que paso, por que, que hacer despues.
5. Marcar el draft como de prueba en el propio mensaje, y que el Desktop lo diga en
   pantalla. Un draft falso que parece real es peor que no tenerlo.

## Done when

- Con el Desktop abierto y sin League en champion select, `--fake-draft` deja la pagina
  del Draft Lab rellena.
- La pagina dice que ese draft es de prueba.
- Sin el Desktop abierto, el comando explica que hacer y sale con codigo distinto de cero.
- Sobre un json de sesion de ejemplo, `--dump-champselect` no escribe ni un puuid ni un
  nombre de invocador. Un test lo comprueba.

## Not covered

La tira de retratos, que es TASK-0010. Esta tarea solo hace llegar el dato.

Correr el volcado en un champion select real es TASK-0017, y la corre el usuario. Esta
tarea entrega los comandos, no la respuesta sobre el lado del mapa.

No escribe nada al cliente de League. El volcado es una lectura de un endpoint que el
proyecto ya consulta (`src/core/lcu.cpp:228`), y la inyeccion es un mensaje entre dos
procesos propios.

No es un modo manual: el usuario no elige campeones desde la UI. El draft de prueba llega
del disco o del ejemplo empotrado.
