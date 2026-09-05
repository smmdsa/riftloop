---
id: TASK-0007
title: Reportar al harness los tres defectos de portabilidad a Windows
work: S
eye: NONE
owner: agent
---


# TASK-0007 — Reportar al harness los tres defectos de portabilidad a Windows

## Why

Instalar el harness en este repositorio fue la primera vez que salio del suyo, y saco
tres defectos que impiden que funcione en Windows sin parches locales. El autor es el
usuario; el informe le sirve mas que el parche.

1. `git clone` del harness falla sin `core.longpaths=true`: las rutas de sus propias
   tareas pasan de 260 caracteres.
2. `python3` en Windows es el stub de la Microsoft Store. Abre la tienda en vez de
   ejecutar Python, asi que los hooks mueren en silencio.
3. `manifest.REQUIRED_HOOKS` compara la cadena literal `python3 -m harness hook`, de
   modo que cambiar el interprete deja el `doctor` en "damaged". Los puntos 2 y 3
   juntos dejan Windows sin salida limpia.

El parche local aqui fue `tools/bin/python3`, un shim que reenvia a `python`, con los
hooks anteponiendo ese directorio al PATH.

Al subir a 0.2.0 aparecio un cuarto defecto, este no exclusivo de Windows:

4. **`upgrade` no respeta `.claude/settings.json` aunque este adoptado.** Duplica los
   hooks, anadiendo el juego de la plantilla junto al que ya estaba, de modo que cada
   evento dispara dos veces. Y restaura la lista `deny` de la plantilla, deshaciendo lo
   que el proyecto habia quitado. Peor: `doctor` sigue diciendo "sound", porque solo
   comprueba que la cadena exista, no que no este repetida.

Montar el indice de este repositorio saco dos mas, los dos de Windows:

5. **El slug del proyecto no se calcula en Windows.** `harness/env.py:18` hace
   `slug = root.replace("/", "-")`, y una ruta de Windows no lleva barras, asi que el
   slug queda `E:\pitero`. Como es una ruta absoluta con unidad, el `os.path.join` de
   la linea 24 la trata como raiz y descarta el prefijo, de modo que
   `HARNESS_MEMORY_DIR` acaba en `E:\pitero\memory`, un directorio que no existe. La
   memoria real de Claude Code vive en
   `C:\Users\<user>\.claude\projects\e--pitero\memory`. El contenedor monta el
   directorio vacio, docker lo crea como root, y la coleccion `memory` queda a cero sin
   un solo aviso.
6. **`harness ports` mide libre un puerto que un contenedor sirve.** `is_free`, en
   `harness/ports.py:50`, intenta un `bind` a `127.0.0.1:puerto`. Con el motor de
   Docker dentro de WSL2 el bind desde Windows funciona igual, porque el reenvio de WSL
   solo actua cuando nadie ocupa el puerto en Windows. Medido: `ports` dijo `free 8510
   8511 8512` mientras los tres contenedores respondian 200 en esos mismos puertos. La
   ley 11 pide comprobar el puerto antes de usarlo, y en Windows con WSL la
   comprobacion miente. Un `connect` antes del `bind` lo arregla.

## What to do

1. Abrir los tres puntos en el repositorio del harness, con el detalle de arriba.
2. Proponer la solucion de raiz: que el instalador resuelva el interprete probando
   `python3`, luego `python`, luego `py -3`, y guarde el elegido en el manifiesto, en
   vez de exigir una cadena fija.
3. Mencionar tambien que `taskkill` en la lista deny rompe el ciclo de build en
   repositorios Windows con ejecutables, porque el linker falla mientras corren.
4. Proponer para el cuarto: que `upgrade` trate `settings.json` como merge y no como
   siembra, y que `doctor` avise cuando un evento tiene mas de un hook con la misma
   needle. Un hook duplicado no es un hook presente.

## Done when

- Los seis puntos estan escritos donde el harness los recoge.
- La propuesta del interprete resoluble esta escrita con su porque.

## Not covered

Arreglar el harness. Esta tarea informa; el arreglo vive en su repositorio.
