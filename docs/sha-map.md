# Mapa de SHA: antes y despues de publicar

El repositorio se publico el 2026-09-07. Antes del primer `push` se
reescribio el historial de `main` para quitar el nick de invocador y dos
rutas locales del propietario. La reescritura cambio los 40 SHA.

Los documentos de `docs/` y `work/` citan los SHA viejos. Esta tabla los
traduce. La columna `nuevo` vacia marca el commit de limpieza, que quedo
sin diferencias y se elimino.

| viejo | nuevo | asunto |
|---|---|---|
| `dece245` | `dece245` | Núcleo de RiftLoop: modelos, SQLite, Data Dragon, ingesta, detectores D01-D10, misiones, Top 3, planner, quiz, LCU read-only, IPC y tests |
| `06879d6` | `06879d6` | Ejecutables de la iteración 1: Agent, Desktop, Overlay, Analyzer + README |
| `b3bf594` | `b3bf594` | Grabación local beta: RiftLoop.Capture con WGC + Media Foundation H.264 |
| `a6f6a33` | `a6f6a33` | Iteración 1.1: sin API key, idioma del cliente, nombres de runas e iconos, overlay interactivo |
| `9ef934b` | `9ef934b` | Docs: flujo sin API key, idioma del cliente y overlay interactivo |
| `262a15c` | `262a15c` | Iteración 1.2: UI oscura con iconografía Data Dragon, parche 26.x, cero escritura manual |
| `2817d37` | `2817d37` | Fix Alt+F4: InGame pegajoso, heartbeat de historial y análisis en un clic |
| `bb2493c` | `bb2493c` | WORK-LOG de la sesión 1 (2026-09-02/03) |
| `a3d6751` | `a3d6751` | core: muestra local de builds y runas desde el propio historial |
| `31f5586` | `31f5586` | core: el draft completo del champion select llega al Desktop |
| `771502d` | `771502d` | core: paginas de runas validas y escritura al cliente con un clic |
| `181e5af` | `181e5af` | core: la grabacion se convierte en clips de evidencia y el raw se borra |
| `9427d14` | `9427d14` | desktop: post-match en dos columnas con playlist y reproductor |
| `6867de4` | `6867de4` | analyzer: comandos para probar cada pieza sin abrir la UI, y version |
| `eccaa53` | `eccaa53` | tests: de 146 a 3073 checks |
| `33981cc` | `ab95fd8` | docs: WORK-LOG de la sesion y CLAUDE.md al dia |
| `5f0bd8c` | `94b2856` | docs: sesion 14 en el WORK-LOG |
| `62053ef` | `e046d17` | harness: instalar claudia-rag-pipeline en el repo |
| `0519ad2` | `914cb64` | harness: mantener LF en los scripts de shell |
| `122ab20` | `a44eb1f` | docs: sesion 15, la instalacion del harness y sus tres defectos en Windows |
| `007a7a3` | `2a5c3a5` | session: cierre de 2026-09-05-1356, el primero con el harness |
| `07e18b4` | `c204449` | docs: como actualizar el harness sin poder pushearle |
| `8945980` | `4808330` | harness: subir a 0.2.0 y volver a poner los parches de Windows |
| `ca2737a` | `1164b83` | session: cierre de 2026-09-05-1839, la actualizacion a 0.2.0 |
| `2987a69` | `57efa73` | rag: indice propio de este repositorio, en WSL y en los puertos 8510 |
| `0bed36b` | `6149d39` | task: TASK-0006 con lo medido del indice, a la espera del ojo |
| `62d893d` | `aa8ccf4` | task: TASK-0001 recoge la observacion a ojo del usuario |
| `5518d16` | `21ae25e` | task: TASK-0005 con la causa medida de por que no salen clips |
| `5fc8e14` | `81918be` | clips: Capture guarda el origen del reloj, y el boton dice por que no corta |
| `1e26669` | `d53b810` | clips: una carpeta por partida, y limpiar pregunta que borrar |
| `14abf59` | `669bd0b` | harness: traer los arreglos de hooks, rag y sesion del upstream |
| `9dbdc4f` | `662df4e` | draft: el champion select llega con equipo, ranura y estado |
| `b7cf92f` | `f5af78e` | board: dos epicas nuevas y el cierre de la sesion anterior |
| `e25ce14` | `b4a8ecd` | session: cierre de 2026-09-07-1435, la tira de draft y la epica EP-03 |
| `0b1841a` | `61e0a4b` | ep-03: la partida se ve entera, no solo el marcador |
| `5efc361` | `899cf8c` | board: EP-03 avanza y la epica del harness se cierra |
| `12d8c93` | `5b552c2` | build: buildt.cmd compila solo los objetivos que se nombran |
| `b167a61` | `—` | privacidad: quitar el nick de invocador y las rutas locales |
| `3d6cf5e` | `f7b7da9` | docs: CLAUDE.md documenta la CLI de EP-03 y buildt.cmd |
| `2dd224e` | `df74642` | historial: el podio de los diez, con el desglose en un tooltip |

Total: 40 commits antes, 39 despues. 1 commit(s) sin equivalente.
