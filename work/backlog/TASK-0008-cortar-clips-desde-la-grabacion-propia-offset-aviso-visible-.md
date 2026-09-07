---
id: TASK-0008
title: "Cortar clips desde la grabacion propia: offset, aviso visible y carpeta por partida"
work: M
eye: GLANCE
owner: agent
---


# TASK-0008 — Cortar clips desde la grabacion propia: offset, aviso visible y carpeta por partida

## Why

El usuario pulsaba "Generar clips de evidencia" y no pasaba nada. Ni un clip, ni un
error, ni un aviso. Habia siete grabaciones completas en disco, 3,4 GB, y ninguna
llegaba a cortarse.

Tres defectos encadenados, los tres medidos:

1. **Capture nunca guardaba el origen del reloj.** `lcu.cpp:276` pide el evento
   `GameStart` a `/liveclientdata/eventdata`, y `capture_main.cpp` lo llamaba una sola
   vez, al escribir el sidecar. Eso pasa en el instante en que el Agent ve `InGame`.
   Las siete grabaciones lo prueban: `start_game_time_sec` vale entre 0,029 y 0,047
   segundos, o sea que Capture arranca con el reloj de la API a cero, antes de que el
   evento exista. Las siete traian `game_start_offset_sec: -1.0`.
2. **`clipmaker.cpp:88` se negaba a cortar**, que es lo correcto: sin el origen, los
   cortes caen desplazados. Pero el motivo solo se escribia en `IDC_CLIP_STATUS`, una
   etiqueta pegada al reproductor, arriba del panel y lejos del boton. Para el usuario
   el boton no hacia nada.
3. Los clips cortados caian en la misma carpeta que las grabaciones completas, y cada
   lector los distinguia por el nombre del archivo.

El hueco entre los dos relojes estaba supuesto, no medido: los comentarios decian "mas
de dos minutos". Medido con `--frame` sobre la grabacion de `LA2_1622009391`, en dos
puntos separados 600 s: en el segundo 300 el reloj marca 04:03 y en el 900 marca 14:03.
Los dos dan **57,0 s**.

## What to do

Hecho en `5fc8e14` y `1e26669`:

1. Capture vuelve a pedir el evento cada dos segundos, hasta cinco minutos, y reescribe
   el sidecar en cuanto llega. Los fotogramas entran por los hilos del frame pool, asi
   que la lectura no cuesta ninguno.
2. Una corrida que no produce ningun clip abre un dialogo con el motivo.
3. `util::clipsDir()` y `util::clipDirFor(matchId)`. Los cortes van a
   `clips/<matchId>/`. Las dos son funciones puras: crea la carpeta quien escribe.
4. "Liberar espacio" abre un dialogo con las dos cuentas y dos botones con nombre:
   solo las grabaciones completas, o todo.

Queda por hacer:

5. Grabar una partida entera con el binario nuevo y comprobar que el sidecar trae el
   offset sin que nadie lo escriba a mano. Eso es TASK-0005.

## Done when

- El boton genera clips de una partida grabada, o dice por que no. Medido con
  `--autoclip LA2_1622009391`: tres clips cortados.
- El fotograma del segundo 15 del primer clip cae en el instante de la evidencia con
  menos de dos segundos de error. Medido: 06:07 contra 6:08, un segundo.
- Los clips estan en `clips/<matchId>/` y las grabaciones completas en `clips/`.
  Medido: `clips/LA2_1622009391/` con tres archivos.
- "Liberar espacio" pregunta que borrar antes de borrar. **El usuario ya lo vio:** "ya
  funciona bien los botones para limpiar los raw".
- **Falta el ojo del usuario sobre los clips**: abrir el post-match de
  `LA2_1622009391` y ver que los tres clips muestran el momento que dicen mostrar.

## Not covered

El offset de las otras seis grabaciones. Siguen en -1 y no se pueden cortar. Cada una
necesita su propia lectura del reloj, porque el tiempo de carga cambia en cada partida.
Rescatarlas a mano son seis lecturas con `--frame`; automatizarlo es el lector de
digitos, que es otra tarea.
