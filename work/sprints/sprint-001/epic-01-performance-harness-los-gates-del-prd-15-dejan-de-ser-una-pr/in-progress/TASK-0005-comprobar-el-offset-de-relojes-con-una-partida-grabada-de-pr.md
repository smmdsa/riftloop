---
id: TASK-0005
title: Comprobar el offset de relojes con una partida grabada de principio a fin
epic: EP-01
work: S
eye: RUN
owner: agent
---


# TASK-0005 — Comprobar el offset de relojes con una partida grabada de principio a fin

## Why

Se descubrio que el reloj de la Live Client Data API y los timestamps del timeline de
Riot no comparten origen: la diferencia medida fue de 153 s y puso los clips casi tres
minutos antes de su momento. El arreglo guarda `game_start_offset_sec` en el sidecar
(`src/capture/capture_main.cpp`) y lo aplica en `videoPositionSec`
(`src/core/videocut.cpp`), pero **no se ha verificado**: hace falta una partida grabada
entera con el binario nuevo. Las grabaciones anteriores no llevan el campo y el codigo
las descarta a proposito.

## What to do

1. Grabar una partida completa con la captura activada.
2. Comprobar que el sidecar trae `game_start_offset_sec` con un valor mayor que cero.
3. Dejar que el analisis genere los clips solo.
4. Sacar un fotograma del inicio de un clip con `--frame` y leer el reloj del juego.

## Done when

- El reloj del fotograma coincide con el instante de la evidencia menos los 15 s de
  margen, con un error por debajo de dos segundos.
- El usuario lo confirma mirando el clip en el post-match.

## Not covered

Las grabaciones sin ese campo. Quedan descartadas por diseno; recuperarlas exigiria
leer el reloj del propio video, que es otra tarea.

## Medido el 2026-09-05: el paso 2 falla siempre, y es un bug de Capture

Hay siete grabaciones completas en `%LOCALAPPDATA%\RiftLoop\clips`, 3,4 GB, hechas con el
binario nuevo entre las 00:18 y las 03:48 UTC del 5 de septiembre. Las siete se alinean
con su partida: `recordingsFor` las encuentra dentro de la ventana de 60 s, asi que
`hasRecordingFor` dice que si y el boton toma el camino de nuestra grabacion, no el del
replay del cliente.

**Las siete traen `game_start_offset_sec: -1.0`.** Ninguna excepcion. Y `-1` es
justamente lo que hace que `clipmaker.cpp:88` se niegue a cortar, que es lo correcto:
sin el origen del reloj los clips caerian dos minutos antes de donde deben.

La causa esta en `lcu.cpp:276`. `liveGameStartOffsetSec()` pide
`/liveclientdata/eventdata` y busca el evento `GameStart`. Capture lo llama una sola vez,
en `capture_main.cpp:110`, al escribir el sidecar, y eso pasa en el instante en que el
Agent ve `GameState::InGame`. Las mismas siete grabaciones lo demuestran:
`start_game_time_sec` vale entre 0,029 y 0,047 segundos. Capture arranca con el reloj de
la API a cero, antes de que el evento `GameStart` exista, asi que la funcion devuelve -1
y el sidecar se escribe ya invalido. Es una carrera, y siempre la pierde.

El sidecar se escribe una vez y nunca se vuelve a tocar. Ese es el arreglo: escribirlo al
empezar, como ahora, y actualizar `game_start_offset_sec` en cuanto el evento aparezca.

Nota aparte: la negativa a cortar se muestra en la etiqueta de estado, no en un dialogo.
Para el usuario el boton "no hace nada". El mensaje esta, pero no se ve.

## Al cierre del 2026-09-06: la cuenta esta probada, el automatismo no

Con el offset escrito a mano (56,5 s, el punto medio de la banda que deja el reloj al
truncar) la cadena entera funciona: `--autoclip LA2_1622009391` corto tres clips, y el
fotograma del segundo 15 del primero marca 06:07 contra los 6:08 de la evidencia. Un
segundo de error, por debajo de los dos que pide el criterio de arriba.

Asi que la aritmetica de `videoPositionSec` es correcta y el margen de 15 s cae donde
debe. Lo que sigue sin probarse es el paso 2 de esta tarea: que Capture escriba ese
numero solo. El arreglo esta en `5fc8e14` pero necesita una partida entera con el
binario nuevo. Hasta entonces, esta tarea no cierra.

El segundo criterio, el del ojo, tambien sigue abierto: el usuario no ha visto los
clips en el post-match todavia.
