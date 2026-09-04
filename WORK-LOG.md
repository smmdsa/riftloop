# WORK-LOG — RiftLoop

Registro de desarrollo. La entrada más reciente va arriba. Las "sesiones" 1 a 13
son bloques de trabajo, no días distintos: 1 es la iteración inicial y 2 a 13
ocurrieron en una sola sesión el 3 y 4 de septiembre de 2026.

---

## Sesión 14 — 2026-09-04 (versión visible, barra fantasma, y el desfase de relojes)

Tres cosas que el usuario vio en el último test, más los commits de cierre.

**1. No había versión en ningún sitio**, así que no se podía saber si el binario en marcha
era el recién compilado. `kAppVersion` + marca de compilación del compilador, en el
sidebar (`v0.2.0 · Sep 4 2026 20:04:50`), en Ajustes y en la cabecera del CLI.

**2. La barra de reproducción se quedaba visible** en otras páginas y cuando no había
clips. Es una ventana **hermana** del vídeo, no hija — hubo que hacerla así porque el
swapchain DXGI tapa a los hijos —, y `switchPage` sólo oculta los controles registrados de
cada página. Ahora se muestra y se oculta a mano según la página y según haya clips, y el
propio vídeo se esconde cuando no hay nada que reproducir.

**3. El botón ofrecía regenerar desde el replay** en una partida que ya tenía sus clips.
Era coherente (el raw se borró al generarlos, así que no hay grabación) pero confuso.
Ahora dice que ya están hechos y que se elija uno de la lista.

### El bug de verdad: los clips caían 153 s antes de su momento

La evidencia decía `[20:00]` y el primer fotograma del clip marcaba `17:12`.

Primera hipótesis, **falsa**: keyframes muy espaciados. Se añadió `--probe` para medirlo y
salieron a **1,0 s exacto**, tanto en la grabación como en los clips. El corte no tenía la
culpa.

La causa real: **`gameTime` de la Live Client Data API y los timestamps del timeline de
Riot no comparten origen**. La API cuenta desde que arranca el proceso del juego; el
timeline, desde que el reloj de partida marca 0:00. La diferencia es la pantalla de carga
más la espera en la fuente: los 153 s medidos. Todo el cálculo mezclaba las dos escalas.

Arreglo: Capture guarda también `game_start_offset_sec`, el `EventTime` del evento
`GameStart` de `/liveclientdata/eventdata`, que es el 0:00 del reloj visible en la escala
de la API. `videoPositionSec` mueve el timestamp del timeline a esa escala antes de
restar dónde empezó la grabación. Una grabación sin ese dato **ya no se corta**: se
conserva con un mensaje, porque cortar desplazado es peor que no cortar.

**Sin verificar:** el arreglo necesita una partida grabada de principio a fin con el
binario nuevo. Las grabaciones anteriores no llevan el campo y quedan descartadas por
diseño. Es lo primero que hay que comprobar en la próxima partida.

Esto además le da sentido a la idea del OCR del reloj que el usuario propuso: sirve para
**calibrar el offset desde el propio vídeo** cuando el sidecar no lo trae, que es
exactamente el caso de las grabaciones viejas.

### Commits

Ocho commits temáticos, en este orden: muestra local, draft al Desktop, runas,
clips de vídeo, UI del post-match, CLI y versión, tests, documentación.

**Aviso para quien haga bisect:** los commits intermedios **no compilan por separado**.
Los archivos grandes (`desktop_main.cpp`, `tests_main.cpp`, `config.*`, `planner.cpp`)
tocan varios temas a la vez y separarlos habría exigido partir archivos a mano. Sólo el
conjunto compila.

**Estado:** build limpio, 3073/3073 checks, árbol de trabajo limpio.

**Siguiente sesión: performance harness (PRD §15).** Es un gate de release y lleva
pendiente desde la primera sesión, mientras el cliente ha ido sumando un reproductor de
vídeo, corte con Media Foundation y un refresco diario.

---

## Cierre de sesión — 2026-09-03 / 04

Resumen para leer primero. Debajo, en orden inverso, está el detalle de cada bloque.

**Punto de partida:** iteración 1 (MVP local, 146 checks). **Punto de llegada:**
3070 checks, cero fallos, build limpio.

### Qué se construyó

| Bloque | Estado |
|---|---|
| Draft Lab espejo del champion select (hovers, bans, rol) + sticky IPC | Verificado |
| Meta local desde el propio historial: tabla `builds`, 410 filas | Verificado con datos reales |
| Runas al cerrar el draft (RF-RUN-001), alternativa con razón real | Verificado |
| Escritura de la página de runas al cliente (RF-RUN-003) | Verificado, 4 caminos |
| Auto-corte de la grabación en clips de evidencia (RF-REC-003) | Verificado end-to-end |
| Playlist con miniaturas + reproductor embebido con barra de controles | Verificado en pantalla |
| Carpeta de grabaciones y cuota de 2 GB en Ajustes | Implementado |

### Decisiones que conviene no volver a discutir

1. **Nada de scraping de terceros.** El historial del cliente ya trae las 10 páginas de
   runas y las 10 builds de cada partida. Raspar u.gg u op.gg viola sus ToS y es mala
   posición frente a Riot (PRD 17.5). La ruta con volumen es el backend con API key de
   producción (PRD 14.6), no el scraping.
2. **Frecuencia, nunca win rate**, para ordenar builds y páginas: la muestra es pequeña y
   ordenar por victorias sobre 6 filas es ruido (RF-ITEM-003).
3. **El matchup manda sobre la muestra** en los slots secundarios de runas. Si la razón
   escrita dice "tenacidad", la página lleva la runa de tenacidad; hay un test que falla
   si eso deja de cumplirse.
4. **La escritura al cliente necesita un clic humano siempre.** Es lo que separa el
   amarillo del rojo en PRD 17.2.
5. **El raw de video sólo se borra si todos los cortes salieron.** Un resultado parcial no
   puede costarle al usuario el material del que salió el resto.
6. **Un clip que no muestra lo que dice mostrar es peor que no tener clip.** De ahí
   `seekAndConfirm` y el descarte de momentos fuera de rango.
7. **OCR del reloj: no como fuente primaria.** La Live Client Data API da el dato con error
   menor a un segundo (comprobado: predicción 12:21.9 contra 12:22 leído en el vídeo).
   Queda pendiente como *fallback* para grabaciones sin sidecar.

### Números medidos, no estimados

- Corte de un clip de 20 s desde una grabación de 889 s: **94 ms**, sin recodificar.
- Partida completa a clips: **543 MB -> 22.7 MB en 2 s** (96 % menos).
- Desfase real entre inicio de grabación y reloj de partida: **0.03 s, 437.26 s, 441.92 s**.
  Sin alinear, dos de cada tres grabaciones cortarían siete minutos desplazadas.
- Muestra local: 410 builds de 41 partidas, 41 propias, 210 de normales y 200 de ranked.
  Por campeón quedan 7 a 11 filas.

### Bugs que sólo aparecieron al probar con datos y pantalla reales

Ninguno de estos lo habría encontrado un test sintético. Vale la pena recordarlo:

- El `--meta` no pasaba Data Dragon al rebuild: 410 filas con contexto de matchup en cero.
- Umbrales fijos de amenaza que no separaban nada (235 filas en un grupo, 5 en otro).
- Páginas de runas con dos runas de la misma fila, imposibles de seleccionar.
- Una grabación truncada e ilegible que además era la que el selector elegía.
- Fotogramas inclinados por asumir que el pitch es `ancho * 4`.
- Botón de pantalla completa muerto porque `GetDlgItem` no encontraba la ventana.
- La barra de controles invisible bajo el swapchain DXGI del vídeo.

### Deuda declarada

- **Champion Guard** (RF-CS-006) sin implementar.
- **Performance harness** (PRD 15): los presupuestos siguen sin medirse. Es un gate de
  release y lleva pendiente desde la sesión 1.
- **Desktop en Win32**, no WinUI 3 (PRD 14.1).
- El **filtro de matchup de runas no se activa** todavía: con 7 a 11 filas por campeón,
  partir la muestra deja mitades que no sostienen una página. Necesita volumen.
- El **layout usa posiciones fijas**: maximizar la ventana no reordena nada.
- Estado "sin clips" del panel (botón centrado) implementado pero no visto en pantalla.
- El **Top 3 se calla** cuando el pool no da candidatos, en vez de proponer campeones
  disponibles del draft marcados como fuera de pool y con confianza baja.
- **Escalera de misiones**: niveles por detector que suban al consolidar, objetivo de la
  partida visible antes de jugar y encadenamiento automático al cerrar el bloque. Diseñado
  y discutido, sin implementar.

### Gotchas de tooling

- El wrapper de heredoc de Bash **colapsa las barras invertidas** antes de que lleguen a
  Python: `\\n` acaba siendo un salto real y rompe los literales de C. Construir los
  escapes con `chr(92)` o usar el tool de edición. Mordió cinco veces en esta sesión.
- Los `.exe` quedan bloqueados mientras corren: cerrar Agent, Desktop, Overlay y Capture
  antes de compilar, o el link falla con LNK1168.
- `Set-Content -Encoding utf8` de PowerShell 5.1 añade BOM y el LCU rechaza ese JSON con
  400. Usar `[IO.File]::WriteAllText` con `UTF8Encoding $false`.
- Para ver la UI: `PrintWindow` con el flag 2 sobre el HWND de la ventana. La captura de
  pantalla completa toma lo que esté delante, que rara vez es lo que interesa.

---

## Sesión 13 — 2026-09-04 (barra de controles del reproductor)

Los atajos estaban bien pero nadie los descubre. El reproductor necesita lo que un usuario
espera encontrar: play, pausa, stop, tiempo, barra de progreso arrastrable y un boton para
entrar y salir de pantalla completa.

**Cambios:**

- `VideoPlayer` gana `seek()`, `ready()` y un `stop()` de verdad (vuelve al principio y
  pausa). Lo que antes se llamaba `stop` apagaba el motor y dejaba el control muerto; eso
  pasa a llamarse `shutdown()` y solo se usa al destruir.
- Barra de controles propia dibujada con GDI: play/pausa, stop, `0:00 / 0:24`, barra de
  progreso con tirador arrastrable y boton de pantalla completa cuyo icono cambia entre
  entrar y salir. Se refresca cada 250 ms, el minimo que permite PRD 15.4.
- Los dos botones sueltos del panel ("Reproducir / Pausa" y "Ampliar") desaparecen: la
  barra los sustituye y el panel gana sitio.

**El detalle que costo:** la barra empezo como ventana **hija** del reproductor y no se
veia. El Media Engine presenta por un swapchain DXGI que cubre a los hijos del HWND, asi
que cualquier cosa pintada dentro queda debajo del video. La barra pasa a ser una ventana
**hermana**: en ventana se coloca justo bajo el video dentro del mismo padre, y en pantalla
completa se convierte en popup topmost sobre el borde inferior del monitor.

**Verificado en pantalla, los dos modos:** en ventana la barra aparece bajo el clip con
sus controles y el tiempo real del clip (24 s); en pantalla completa ocupa el ancho de
1920 px con el icono de salir. Esc y F11 siguen funcionando.

**Estado:** build limpio, 3070/3070 checks.

---

## Sesión 12 — 2026-09-04 (reproductor a pantalla completa)

A 430 px el clip se ve, pero no se lee el juego, que es justo para lo que esta.

**Cambios:**

- El reproductor pasa a pantalla completa con **doble clic, F11 o el boton "Ampliar"**, y
  vuelve con doble clic, Esc o F11. La barra espaciadora reproduce y pausa.
- No se recrea nada: la ventana hija se guarda su sitio, se convierte en popup sin bordes
  sobre el monitor donde esta y luego se restaura. El Media Engine sigue presentando en el
  mismo HWND, asi que el clip no se recarga ni pierde la posicion.
- La clase de ventana necesita `CS_DBLCLKS` o nunca ve el doble clic.
- Mientras esta en ventana, la esquina inferior muestra "doble clic o F11: pantalla
  completa": un video de 430 px parece una miniatura y nada indicaba que se pudiera abrir.

**Bug encontrado al probarlo:** el boton no hacia nada. La ventana del reproductor se crea
con `CreateWindowW` y no con el ayudante `mk()`, y su id no se localizaba de forma fiable
con `GetDlgItem`: `GetDlgCtrlID` devolvia 0. Ahora el HWND se guarda al crearlo
(`g->playerWnd`) y no hay indireccion que pueda fallar.

**Verificado en pantalla:** 430x241 -> 1920x1080 y de vuelta. En la captura a pantalla
completa se leen el reloj (23:30, quince segundos antes del momento de 23:45), el oro, el
chat y el minimapa.

**Estado:** build limpio, 3070/3070 checks.

---

## Sesión 11 — 2026-09-04 (post-match en dos columnas, playlist y reproductor verificados)

La ventana era de 1180x780 y el reproductor caia **encima** del texto: el report ocupaba
908 px de ancho y el panel de clips empezaba en +600. Rediseno de la pagina.

**Cambios:**

- Ventana 1560x900. La pagina post-match pasa a dos columnas que no se solapan:
  diagnostico a la izquierda, clips a la derecha (`kClipPanelW = 430`).
- La playlist deja de ser un ListView y pasa a ser el ReportView con un tipo nuevo,
  `RVKind::ClipCard`: miniatura de 96x54, titulo, hechos observados debajo, 84 px de alto
  y barra de acento en la tarjeta que suena. Reutiliza el scroll, el clic y el cursor de
  mano que el ReportView ya tenia.
- Panel derecho: reproductor arriba (16:9), boton de reproduccion, estado, barra de
  progreso y la lista debajo.
- **Sin clips, el boton "Generar clips" se centra en el panel**; con clips se coloca bajo
  la lista (`layoutClipPanel`).
- Al entrar en la pagina se carga la ultima partida analizada (antes aparecia vacia) y el
  primer clip queda cargado en pausa, para que el panel no muestre un rectangulo negro.
- Una tarjeta por clip: `std::set` sobre `clipFile`, porque una evidencia repetida
  producia tarjetas duplicadas.

**Dos defectos de layout que solo se ven mirando la pantalla:**

1. El boton del panel salia **cortado por el borde inferior**. La causa: se posicionaba
   contra `kWinH`, que incluye la barra de titulo y los bordes. Ahora el panel se calcula
   contra el area cliente real (`GetClientRect`).
2. Los titulos de las tarjetas se truncaban. Se quita el id del detector del titulo (es
   jerga y robaba sitio) y el subtitulo se limita a dos lineas con elipsis.

**Verificado en pantalla**, capturando la ventana con `PrintWindow`: dos columnas sin
solapes, dos tarjetas con su miniatura real, la activa marcada, el **reproductor embebido
mostrando el clip** y el boton completo. El reproductor era lo unico que quedaba sin
comprobar desde la sesion anterior.

**Limitacion conocida:** el layout usa posiciones fijas. Maximizar la ventana no
reordena los controles.

---

## Sesión 10 — 2026-09-04 (el boton de clips usaba el camino equivocado, playlist y reproductor)

El usuario reporto que "Generar clips de evidencia" abria el replay del cliente y no
generaba nada. Cierto: el boton llamaba a `--clips` (abrir el .rofl y regrabar, minutos)
en vez de `--autoclip` (cortar la grabacion, segundos). Al arreglarlo salieron dos bugs
mas y una verificacion que faltaba desde hace tres sesiones.

**Cambios:**

- `core/clipmaker`: la orquestacion sale del Analyzer y pasa a core, con callback de
  progreso, para que el CLI y el Desktop corran el mismo codigo.
- El boton **corta la grabacion** cuando existe (segundos, sin abrir nada) y solo ofrece
  el camino del replay cuando no hay grabacion, avisando de que tarda y ocupa la pantalla.
- Post-match: aviso de estado, barra de progreso paso a paso, **playlist con miniatura**
  de cada clip y **reproductor embebido** (Media Foundation Media Engine sobre un HWND
  hijo, sin WebView: PRD 14.5). Seleccionar un clip lo reproduce junto al texto que
  explica ese momento.
- `core/videoframe`: extrae un fotograma a bitmap y lo guarda como PNG.
- `--frame <mp4> <seg> <png>` para comprobar la sincronia sin abrir la UI.

**Bug: grabaciones ilegibles.** `20260904_155750.mp4` (100 MB) no se podia leer: duracion
-1. Un MP4 cuyo proceso muere sin finalizar no tiene indice. Peor aun, `pickRecordingFor`
elegia **esa** por ser la mas temprana e ignoraba la buena que habia al lado. Ahora
`recordingsFor` devuelve todas las candidatas ordenadas y el clipmaker prueba hasta
encontrar una legible, con mensaje propio cuando todas fallan. Capture ademas atiende
`SetConsoleCtrlHandler` para cerrar el MP4 cuando lo cierran o apagan la sesion.

**Bug: fotogramas con el pitch mal.** La extraccion mezclaba `Lock` y `Lock2D` y asumia
`ancho * 4`. Un fotograma decodificado va alineado al hardware, no al ancho, asi que salia
la imagen inclinada en diagonal. Se resuelve dejando que el Source Reader escale
(`MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING` y `MF_MT_FRAME_SIZE` de destino), con
lo que el pitch de salida es predecible.

**La verificacion que faltaba: la sincronia es exacta.** Fotograma del segundo 300 de
`20260904_160544.mp4`, cuyo sidecar dice `start_game_time_sec = 441.9`:

```
prediccion  441.9 + 300 = 741.9 s = 12:21.9
reloj visible en el fotograma      12:22      ✓
```

Y el clip de la evidencia de las **19:36** muestra el reloj en **19:37**, con "SE ACABO",
"REAPARICION EN 39" y el chat diciendo `[19:37] ... acabo con <mi invocador> (Jinx)`. El clip
ensena exactamente el momento que dice ensenar.

**Sobre leer el reloj por vision (OCR):** el reloj se lee sin problema en el fotograma, asi
que es viable. No se implementa como fuente primaria porque la Live Client Data API da el
dato con error menor a un segundo y PRD 14.5 desaconseja OCR cuando una API legitima
resuelve el dato. Queda como **fallback** para grabaciones sin sidecar, que es donde hoy
no hay nada.

**Desfases reales medidos**, que justifican todo el mecanismo: `0.03 s`, `437.26 s` y
`441.92 s`. Sin alinear, dos de tres grabaciones cortarian siete minutos desplazadas.

**Estado:** build limpio, 3070/3070 checks. Sin verificar: la playlist y el reproductor
embebido compilan pero no se han visto funcionando en pantalla.

---

## Sesión 9 — 2026-09-04 (bug: paginas de runas imposibles de seleccionar)

El usuario reporto con captura que la pagina sugeria **Segundo Aire (8444)** y **Coraza
Osea (8473)**. Las dos estan en la **fila 2** de Valor: el cliente no deja elegir dos
runas de la misma fila, asi que la pagina era imposible. Al mirarlo a fondo aparecieron
tres bugs, no uno.

**Bug 1, el visible.** Pares fijos invalidos en el generador:
`page.perks.push_back(8444); push_back(vsCc ? 8242 : 8473);`. Con CC salia 8444+8242
(filas 2 y 3, correcto); sin CC salia 8444+8473, las dos de fila 2. La alternativa
situacional "vs burst" tenia el mismo par. Ahora los candidatos pasan por `pickSecondary`,
que descarta cualquiera que repita fila y rellena desde una fila libre.

**Bug 2, la raiz.** `validatePage` comprobaba que cada perk existiera en *alguna* fila del
subestilo, no que las dos secundarias fueran de filas **distintas**. Por eso el bug paso
sin que nada lo detectara. Reescrita como `validateRunePage` con las reglas reales:
keystone de fila 0, una primaria por fila, dos secundarias de filas distintas, un
fragmento por fila. Repara lo que puede en vez de solo mirar.

**Bug 3, latente y mas sutil.** `metaRunes` tomaba el perk mas comun **de cada slot por
separado**. Los slots primarios son independientes (cada uno es su fila), pero los dos
secundarios no lo son: elegirlos por separado puede fabricar un par que nadie jugo nunca
y que ademas sea ilegal. Ahora se cuenta el **par** mas comun.

**Verificado contra el parche y contra el cliente, nada de memoria:**

- Filas leidas de `runesReforged.json` 16.17.1: Valor fila 2 = `8429, 8444, 8473`,
  fila 3 = `8451, 8453, 8242`. Confirma el choque.
- Los 410 pares secundarios realmente jugados en la muestra local estan **todos** en filas
  distintas (`8473+8242` x17, `8451+8444` x16, `8473+8453` x13...). Confirmacion empirica.
- La tabla curada de fragmentos coincide **exactamente** con `/lol-perks/v1/styles` del
  cliente: `kStatMod` = `5008,5005,5007` / `5008,5010,5001` / `5011,5013,5001`. Por eso
  `5008,5001,5001` es una combinacion legitima: 5001 esta en las filas 2 y 3.
- `allowedSubStyles` permite cualquier par salvo primaria igual a secundaria, que el
  generador ya evitaba. Ahi no habia bug.
- El historial del cliente (v4) solo trae `perk0..perk5`: **los fragmentos nunca llegan en
  la muestra**, salen siempre del codigo. Por eso necesitan tabla y validacion propias.

**Test:** recorre 5 tipos de composicion rival x 6 campeones x 5 roles y comprueba cada
pagina generada, principal y alternativa: keystone en fila 0, una primaria por fila, dos
secundarias de filas distintas, un fragmento por fila. Ademas repara una pagina rota
con el par exacto de la captura. La suite pasa de 357 a **3070 checks**.

**Resultado en el caso reportado** (Jinx BOTTOM contra comp de burst):
antes `8444 + 8473` (imposible), ahora `8473 Coraza Osea + 8242 Inquebrantable`
(filas 2 y 3).

---

## Sesión 8 — 2026-09-04 (carpeta de grabaciones y cuota real)

Peticion del usuario: un boton en Ajustes que abra la carpeta de las grabaciones. Al
mirarlo aparecio un defecto: **la cuota de 2 GB de PRD 13.4 se aplicaba por archivo en
Capture, no a la carpeta**. El usuario ya tenia 6 grabaciones y 2.16 GB.

**Cambios en Ajustes:**

- Linea con el numero de grabaciones, el espacio que ocupan y el limite, en color de aviso
  cuando se pasa. Cuenta aparte los clips de evidencia.
- Boton "Abrir grabaciones".
- Boton "Liberar espacio": borra las grabaciones completas mas antiguas hasta bajar de
  2 GB, con la lista exacta de lo que va a borrar y confirmacion. **No borra nada solo.**
  Los clips de evidencia nunca se tocan: son la prueba detras de un diagnostico.

**Decision:** PRD 9.10 RF-REC-003 dice "borrar el resto salvo conservar partida completa",
pero borrar video del usuario sin preguntar no se implementa mientras no exista la
seleccion automatica de clips. Hoy se avisa y se ofrece; el borrado lo decide el usuario.

Las grabaciones completas se distinguen de los clips de evidencia por el nombre:
`YYYYMMDD_HHMMSS.mp4` frente a `MATCHID_EVIDENCIA.mp4`.

## Auto-corte: grabar completo, quedarse solo con los momentos (RF-REC-003)

Peticion del usuario, y es literalmente lo que pide RF-REC-003: grabar la partida entera,
al terminar cortar los momentos que importan con margen, conservar solo esos clips y
borrar el raw. Resuelve la cuota de raiz: sin raw no hay gigabytes acumulados.

**La pieza que faltaba era la sincronizacion.** La grabacion empieza cuando el Agent
detecta InGame, no en el minuto 0. Sin ese desfase un corte cae en el segundo equivocado.
`liveGameTimeSec()` lee el reloj de partida de la Live Client Data API y Capture lo
escribe en un sidecar `<archivo>.mp4.json` al empezar. Sin sidecar no se corta nada.

**`core/videocut`:** corta con Media Foundation copiando las muestras comprimidas tal
cual, sin recodificar. El corte arranca en el keyframe anterior al segundo pedido, y el
margen de 15 s lo absorbe. `readRecordingSidecar`, `listRecordings`, `pickRecordingFor`
y `videoPositionSec` son puros y estan testeados.

**Orquestacion (`--autoclip`, y automatico tras `--auto`):** empareja grabacion y partida
por ventana temporal, convierte cada `gameTimestampMs` a posicion de video, corta, enlaza
cada clip a su evidencia y borra el raw. **El raw solo se borra si todos los cortes
salieron**: un resultado parcial no le cuesta al usuario el material del que salio el
resto. Casilla "Conservar la partida completa" en Ajustes para quedarse con ambos.

**Verificado con una grabacion real del usuario:**

```
--cut-test 20260904_011134.mp4 600   ->  clip de 20.0 s en 94 ms (origen 889 s)
--autoclip LA2_1621795698            ->  3 clips de 25.0 s en 2 s
                                         543 MB  ->  22.7 MB  (96% menos)
```

Los tres clips quedaron enlazados a sus evidencias (D01 226.6 s, D05 280.6 s,
D01 323.1 s) y son clicables en el post-match.

**Pendiente de medir:** el desfase real. La prueba uso un sidecar sintetico con
`start_game_time_sec = 0`. Las grabaciones futuras traen el valor real de la Live Client
Data API; falta comprobar con una partida grabada de principio a fin que el clip cae
donde dice.

**Estado:** build limpio, 357/357 checks.

---

## Sesión 7 — 2026-09-04 (muestra limpia y keystone sensible al matchup)

Pregunta del usuario: las runas sugeridas, son por el match o por su uso? La respuesta
era "mezcla, con reparto desigual": keystone y primarias salian de **frecuencia** pura y
solo las secundarias y el fragmento respondian al matchup. La parte mas importante de la
pagina era ciega a la partida.

**Tres defectos que los datos reales destaparon antes de tocar nada:**

1. Se mezclaban colas: 190 filas de normales con 50 de ranked en la misma muestra
   (PRD 13.3 pide separar modos).
2. 20 filas con rol vacio contaminaban las consultas.
3. La tabla no marcaba que fila jugo el usuario, asi que "por uso del player" era
   imposible de responder.

**Cambios:**

- `builds` gana `is_user`, `enemy_burst`, `enemy_cc`, `enemy_magic_share`, con migracion
  aditiva por ALTER TABLE para bases ya creadas. El contexto es la composicion contra la
  que se jugo esa fila: es lo que hace respondible "la pagina mas jugada **contra esta
  comp**" en vez de "la mas jugada".
- `Db::builds` filtra por familia de cola y descarta filas sin rol. `queueFamilyOf`
  separa ranked, normales y modos alternativos; ARAM y Arena no entran nunca.
- `meta`: la muestra se amplia en orden (ranked del parche -> ranked de cualquier parche
  -> normales), y la nota dice de cual salio y cuantas filas son del usuario.
- `metaRunes` acepta `ThreatContext` y estrecha la muestra a partidas contra una comp
  parecida antes de elegir la pagina. Asi el keystone responde al matchup con datos, no
  con una tabla curada.

**Dos hallazgos de la verificacion con datos reales:**

- `--meta` llamaba a `rebuildBuilds()` **sin** pasar Data Dragon, asi que el contexto de
  matchup quedaba en cero en las 410 filas. Corregido; ahora 410 de 410 lo tienen.
- Los umbrales fijos (burst 1.8, cc 2.0) **no separan nada**: sobre la muestra real
  dejaban 235 filas en un grupo y 5 en otro. El corte pasa a ser la **mediana de la
  propia muestra**, que siempre parte en dos mitades comparables y se adapta a la escala
  de los rasgos.

**Estado real de la muestra (41 partidas del usuario):** 410 builds, 41 suyas,
410 con contexto, 210 normales y 200 ranked. Por campeon quedan 7 a 11 filas, asi que el
filtro por matchup **todavia no se activa**: partir 11 filas deja dos mitades que no
sostienen una pagina. El mecanismo esta probado; le falta volumen. Con el refresco diario
a 60 partidas ira subiendo.

**Estado:** build limpio, 348/348 checks.

---

## Sesión 6 — 2026-09-03 (escritura de runas al cliente, RF-RUN-003)

Primera escritura al cliente de League del proyecto. Hasta hoy no existia ninguna.

**Clasificacion:** RF-RUN-003, PRD 17.2 la marca **amarilla**, no roja. Lo rojo es el
auto-lock sin input (RF-CS-007). La linea que separa una de otra es una accion humana
explicita, y aqui la hay: cada escritura necesita un clic en RiftLoop. No hay ninguna
ruta de codigo que aplique runas sola.

**Garantias implementadas, no prometidas:**

- `Config::runeWriteEnabled`, apagada por defecto, kill switch propio (PRD 17.3).
- RiftLoop gestiona **una sola pagina**, la que empieza por "RiftLoop". Ninguna funcion
  lee, edita ni borra una pagina personal. Si no hay espacio para crear la nuestra, la
  operacion se bloquea con un mensaje que dice que el usuario borre una a mano: liberar
  espacio borrando una pagina suya no es una opcion del codigo.
- Diff antes de escribir, slot por slot, con los nombres del parche instalado.
- Undo de un clic: restaura el contenido anterior de nuestra pagina (o la elimina si la
  creamos) y vuelve a seleccionar la pagina que estaba activa.
- Si el usuario edita a mano la pagina de RiftLoop, se detecta por firma de contenido y
  **no se sobrescribe** sin una confirmacion aparte.
- Auditoria local de cada escritura y de cada undo (PRD 17.4).

**Endpoints verificados en el cliente real** antes de escribir una linea:
`GET /lol-perks/v1/pages`, `GET /lol-perks/v1/inventory`, `POST /lol-perks/v1/pages`,
`PUT /lol-perks/v1/pages/{id}`, `DELETE /lol-perks/v1/pages/{id}`,
`PUT /lol-perks/v1/currentpage`. El inventario del usuario responde
`{"canAddCustomPage":true,"customPageCount":1,"ownedPageCount":2}`.

**Cambios:** `http::request` con verbo libre; `Lcu::requestRaw`; `core/perkpages`
(parsers puros, `planPageWrite`, `applyRunePage`, `undoRunePage`); `Config::runeWriteEnabled`;
Desktop con casilla en Ajustes y botones "Aplicar runas" y "Deshacer" en Draft Lab.

**Tests (345 checks en total):** crear con hueco, bloqueo sin hueco comprobando que el
mensaje no propone borrar paginas del usuario, sobrescritura con diff de un solo slot,
deteccion de edicion manual, pagina no editable, pagina generada incompleta, y el cuerpo
que se manda al cliente.

**Verificado contra el cliente real, los cuatro caminos.** Estado inicial: una pagina
`dianubichardardasarda` (id 710440726), seleccionada, `customPageCount: 1 de 2`.

| Caso | Resultado |
|---|---|
| Crear | Pagina `RiftLoop: Sylas MIDDLE` creada (id 946544112) y seleccionada. La pagina del usuario intacta. |
| Reaplicar lo mismo | "ya aplicada", cero escrituras |
| Sobrescribir la nuestra | Diff de 3 slots, PUT correcto, la pagina del usuario sin tocar |
| **Edicion manual** | Se edito la pagina de RiftLoop por fuera (PUT directo al LCU) y el siguiente apply **se bloqueo**: "Editaste esa pagina a mano en el cliente" |
| Undo | Restauro el contenido previo a la ultima escritura, exacto |

Limpieza posterior: se borro la pagina de RiftLoop y se devolvio la seleccion. El estado
final es identico al inicial, perks incluidos:
`8008,9101,9104,8017,8444,8242,5008,5008,5001`, `currentpage 710440726`,
`customPageCount: 1`. La flag quedo apagada.

Notas del sondeo: el LCU acepta `PUT /lol-perks/v1/pages/{id}` sin problema desde el
codigo. Un JSON con BOM lo rechaza con 400 "Invalid value at offset 0" - importa si
alguna vez se escribe el cuerpo desde un archivo.

**Alcance del undo:** es de un paso. Restaura el estado previo a la ultima escritura, no
al estado anterior a la primera. Es lo que la UI promete.

---

## Sesión 5 — 2026-09-03 (iteración 5: clips de evidencia desde el replay)

Un clip ya no necesita que la partida se grabara en vivo: el cliente conserva el .rofl.
RiftLoop pide la descarga, abre el replay, salta al instante de cada evidencia con la
Replay API y graba esos segundos con Capture.

**Cumplimiento:** todo son acciones locales sobre la partida del propio usuario. Van tras
`Config::clipsEnabled`, apagado por defecto, con confirmacion en la UI y auditoria. No se
automatiza el boton de highlight del cliente: eso exigiria simular teclado, prohibido por
PRD 14.5.

**Cambios:**

- `http`: `request(method, ...)` con cuerpo; `get` y `post` son envoltorios.
- `lcu`: `getRaw` y `postRaw` publicos para los endpoints de replays.
- `core/replays`: configuracion, metadata, descarga, watch y carpeta de replays por LCU;
  `playback` y `seek` contra la Replay API (127.0.0.1:2999). Los parsers son puros y
  estan testeados. `planClips` decide que grabar: 15 s de adelanto, 25 s de duracion,
  maximo 3 clips (PRD 10.2), une evidencias a menos de 20 s y saltea detectores sin
  fallos. Los nombres de archivo se sanean (PRD 16.2).
- `models`: `Evidence::clipFile` (PRD 30: clip_ref local opcional).
- `capture`: `--out` para nombrar el clip.
- `analyzer`: `--clips <matchId|last>` orquesta descarga, apertura, seek, grabacion y
  vinculacion del clip a su evidencia. Aborta si hay una partida en curso (PRD 14.4).
- `desktop`: casilla "Clips de evidencia desde el replay" en Ajustes, boton "Generar clips
  de evidencia" en post-match con confirmacion previa, y cada evidencia con clip muestra
  una fila clicable que abre el video. El ReportView acepta filas con accion
  (`RVItem::action`, `WM_RV_ACTION`, cursor de mano).

**Verificado contra el cliente real:**

- `GET /lol-replays/v1/metadata/1621721063` antes: `{"downloadProgress":4170411667,
  "state":"download"}`. `POST .../rofls/1621721063/download` devuelve 204 y en 3 segundos
  el estado pasa a `{"downloadProgress":100,"state":"watch"}`.
- Ese sondeo encontro un defecto: `downloadProgress` trae un numero sin relacion cuando el
  archivo no esta. El parser ya no lo muestra como porcentaje fuera de 0..100, con los dos
  payloads reales como test.
- `--clips last` con la funcion apagada responde el mensaje de opt-in y no toca nada.

**NO verificado todavia (honestidad, PRD 9):** el tramo final del flujo. Falta abrir un
replay de verdad para comprobar (a) que la Replay API responde en 127.0.0.1:2999 con este
cliente, (b) el desfase entre `gameTimestampMs` del timeline y el `time` del replay, y
(c) que Capture graba la ventana del replay. El adelanto de 15 s absorbe desfases pequenos,
pero el offset hay que medirlo. Esa prueba abre el juego y ocupa la pantalla varios
minutos, asi que se hace con el usuario presente.

**Endurecimiento del tramo que no se puede probar todavia:**

Como el desfase entre el reloj del timeline y el del replay es desconocido, el orquestador
no confia en que el seek funcione. `seekAndConfirm` salta, vuelve a leer la posicion y
compara: si el replay no llego al instante pedido (tolerancia 5 s), **el clip se omite en
vez de grabar otro momento**. Un clip que no muestra lo que dice mostrar es peor que no
tenerlo (PRD 3.3). El desfase medido se guarda en kv `replay_seek_drift_ms`: es el numero
que dira si los dos relojes comparten origen en este cliente. Ademas: un instante fuera de
la duracion del replay se omite, la ventana del juego se comprueba antes de grabar, y al
terminar el replay queda en pausa en vez de seguir corriendo.

`--replay-check` diagnostica sin abrir nada ni descargar nada. Salida real:

```
Replays habilitados: si | desde historial: si | partida en curso: no
Carpeta de replays: %USERPROFILE%/OneDrive/.../Replays
Desfase de seek medido: sin medir (no se ha generado ningun clip)
Replay API (127.0.0.1:2999): no responde (no hay replay abierto)
  LA2_1621721063     listo          3 clips planificados
  LA2_1621710462     por descargar  1 clips planificados
```

**Estado:** build limpio, 308/308 checks.

---

## Sesión 4 — 2026-09-03 (iteración 4: runas al cerrar el draft, RF-RUN-001)

**Cambios:**

- `models`: `DraftContext::closed()` (4 aliados + 5 rivales; el usuario no esta en la
  lista), `knownPicks()`, `missingPicks()`. `RunePlan` gana `draftClosed`, `missingPicks`
  y `noAlternativeReason`.
- `planner`: la pagina dice si es definitiva o provisional y nunca sube a confianza alta
  con el draft abierto. La alternativa situacional pasa de una sola condicion a tres
  candidatas con su amenaza y su condicion (CC en cadena, burst, DPS sostenido); si
  ninguna aplica, no se inventa una y se explica por que (RF-RUN-002). La tercera
  decision no obvia explicada es el fragmento del segundo slot, atado a la amenaza.
- `agent`: el contrato del plan lleva `draft_closed` y `known_picks`, y la razon de
  incertidumbre distingue draft cerrado de draft incompleto.
- `desktop`: la vista del plan marca definitivo/provisional, muestra la condicion de la
  alternativa y, cuando no hay, la razon.
- `analyzer`: `--plan <Campeon> <ROL> [rivales...]` acepta la comp rival e imprime el
  estado del draft y la alternativa.

**Dos defectos que encontro la prueba manual, no los tests:**

1. La pagina decia "el burst rival castiga los intercambios largos" con cero rivales
   conocidos. Una afirmacion sin evidencia (PRD 3.3). Ahora, cuando la secundaria viene
   de la muestra y no de una amenaza, la razon lo dice.
2. La pagina decia "Rama Valor con tenacidad" mientras la muestra le quitaba la runa de
   tenacidad: el texto no correspondia a los perks. La regla ahora es explicita: el
   ajuste por matchup manda sobre la frecuencia en los slots secundarios. Hay un test
   que falla si una pagina afirma "tenacidad" sin llevar 8242.

**Verificado:** 211/211 checks. `--plan Sylas MIDDLE Leona Thresh Sejuani Morgana Ashe`
produce 8242 Inquebrantable con la razon correspondiente; sin rivales no promete nada.

---

## Sesión 3 — 2026-09-03

**Objetivo:** runas y builds con datos reales en vez de plantillas, y dejar el refresco
organizado. El usuario pidio un "scraper diario" de mejores builds y runas.

**Decision: no hay scraper de terceros.** Raspar u.gg, op.gg o lolalytics viola sus ToS,
se rompe con cada cambio de HTML y es mala posicion para pedir aprobacion a Riot
(PRD 17.5). La fuente esta ya en la maquina: cada partida del historial trae las 10
paginas de runas y las 10 builds de esa partida. La ruta con volumen real sigue siendo
el backend con API key de produccion (PRD 14.6), no el scraping.

**Cambios:**

- `models`/`ingest`/`lcu_history`: `Participant` guarda `perkPrimaryStyle`, `perkSubStyle`
  y `perks`. La conversion v4 a v5 reconstruye el bloque `perks` desde `stats.perk0..perk5`,
  asi que el ingest tiene un solo parser.
- `db`: tabla `builds`, una fila por participante de cada partida importada. Sin puuid y
  sin nombre: campeon, rol, parche, victoria, runas, items y hechizos. `upsertBuilds`
  (idempotente por match+participante, descarta remakes), `rebuildBuilds`, `builds()`
  con indice por campeon/rol/parche.
- `core/meta`: `metaRunes` y `metaItems`. Reglas explicitas contra el sesgo (RF-ITEM-003):
  se ordena por frecuencia y nunca por win rate; muestra minima de 5 filas; una pagina de
  runas solo se ofrece si domina la muestra (>= 3 filas y >= 40 %); si el parche pedido no
  tiene muestra se cae a toda la muestra y se marca `fromOlderPatch` con confianza baja;
  `MetaSample::note()` no da porcentaje de victorias por debajo de 10 filas.
- `planner`: `planRunes` toma `Db&` y usa la pagina de la muestra como base, manteniendo
  encima el ajuste por matchup (burst/CC). Si no hay muestra lo dice en la propia pagina.
  `planItems` deja de reparsear 40 JSON de partida en cada llamada y consulta la tabla
  `builds`, que ademas mira a los 10 jugadores y no solo al usuario.
- `analyzer`: `--meta [N]` refresca la muestra (import + backfill + rebuild) y
  `--plan <Campeon> <ROL>` imprime el plan para comprobarlo sin abrir la UI.
- `agent`: dispara `--meta` una vez al dia en estado calmado, nunca durante partida.
- `desktop`: Ajustes muestra el tamano de la muestra y la fecha del ultimo refresco, con
  boton "Actualizar muestra".
- `lcu_history`: `refetchRunePages`. Las partidas importadas antes de este cambio tienen
  su JSON v5 guardado sin `perks`; `rebuildBuilds` no las puede recuperar. El backfill
  vuelve a pedirlas al cliente y reemplaza el JSON.

**Verificado con datos reales del usuario, no solo en test:**

- `--meta 20`: 10 partidas nuevas, 210 builds. Tras el backfill, 210 de 210 filas con
  pagina de runas completa (antes 100 de 210: ese fue el fallo que descubrio la prueba).
- `--plan Sylas MIDDLE`: runas desde la muestra ("Base: pagina mas jugada en tus
  partidas. Muestra local: 6 builds"), con el ajuste por matchup encima.
- `--plan Jinx BOTTOM`: items desde datos (Runaan + Lord Dominik, 9 builds); runas caen a
  plantilla porque ninguna pagina domina esa muestra. Correcto por diseno.

**Estado al cierre:** build limpio, 193/193 checks. Agent y Desktop relanzados.

**Pendiente de esta linea:** la muestra por campeon es de 5 a 10 filas con 21 partidas.
Con `metaImportCount` en 60 sube a unas 25 por campeon habitual. Falta medir si el
cliente sirve tanto historial.

---

## Sesión 2 — 2026-09-03

**Objetivo:** el Draft Lab no se rellenaba solo durante champion select (reporte del
usuario con captura: rol, campeones y baneos vacíos mientras el cliente estaba en draft).

**Causa raíz:** el Agent leía el champ select y calculaba el Top 3, pero el mensaje IPC
solo llevaba el contrato de recomendación. El draft nunca viajaba al Desktop, así que los
desplegables no tenían de dónde llenarse. Dos causas secundarias en el mismo camino:
los hovers no se leían (`championId` es 0 hasta el lock) y el broadcast no se retenía,
por lo que un Desktop abierto a mitad del draft no recibía nada.

**Cambios:**

- `core/lcu`: `parseChampSelect(sessionJson, pickableJson)` como función pura (testeable).
  Lee `championPickIntent` para aliados y para el propio jugador antes del lock; toma los
  bans de `actions[]` completadas además de `bans.myTeamBans/theirTeamBans`, sin duplicados.
  `ChampSelectView` gana `localHoverChampionId`.
- `core/ipc`: `Server::broadcast(text, stickyKey)` retiene el último mensaje por clave y
  lo reenvía a cada cliente que conecta después. `clearSticky(key)` lo descarta. El Agent
  marca `state` y `draft` como sticky.
- `agent`: `draftView()` serializa rol, tu campeón (locked o hover), aliados, rivales,
  bans y set pickable dentro de los mensajes `top3` y `plan`. La firma de recálculo
  (RF-CS-005) ahora incluye bans y hover propio. Al salir de champ select borra el sticky.
- `desktop`: `applyDraftView()` rellena rol, "Tu campeón", los 4 combos de aliados y los 5
  de rivales, y muestra una línea de baneos nueva (`IDC_DRAFT_BANS`). `draftContextFromUi()`
  pasa los bans al recomendador y usa el set pickable solo mientras el estado es ChampSelect.
- `tests`: 8 checks nuevos sobre `parseChampSelect` (hover de aliado, hover propio, celda
  enemiga vacía, ban duplicado, ban sin completar, bans solo en el resumen, json inválido).

### Hallazgos verificados contra el cliente (sondeo read-only del LCU)

Base para el roadmap de builds/runas y de clips. Comprobado en el cliente 16.17.810
del usuario, no supuesto:

- El historial (`/lol-match-history/v1/games/{id}`) trae por participante
  `perk0..perk5`, `perkPrimaryStyle`, `perkSubStyle`, `item0..item6`, `spell1Id`,
  `spell2Id`. Son 10 paginas de runas y 10 builds reales por partida importada.
  `Participant` en `models.h` guarda hoy solo `finalItems`; el resto se tira.
  Esta es la fuente legal de meta local: no hace falta raspar u.gg ni op.gg
  (sus ToS lo prohiben y es mala posicion frente a Riot, PRD 17.5).
- Replays habilitados: `/lol-replays/v1/configuration` responde
  `isReplaysEnabled: true`, `isReplaysForMatchHistoryEnabled: true`.
  Endpoints presentes: `POST /lol-replays/v1/rofls/{gameId}/download`,
  `POST /lol-replays/v1/rofls/{gameId}/watch`,
  `GET /lol-replays/v1/metadata/{gameId}`, `GET /lol-replays/v1/rofls/path`.
  Permite producir clips de una partida que no se grabo en vivo: descargar el
  rofl, abrirlo, saltar al timestamp de la evidencia con la Replay API
  (127.0.0.1:2999, pendiente de comprobar con un replay abierto) y grabar con
  Capture. Automatizar el boton de highlight del cliente exigiria simular
  teclado: prohibido (PRD 14.5).

**Estado al cierre:** build limpio, 164/164 checks. Verificado en test, **sin verificar en
un champion select real**. Agent y Desktop relanzados desde `build\x64-debug`.

---

## Sesión 1 — 2026-09-02 / 2026-09-03

**Objetivo de la sesión:** analizar el PRD y generar la primera iteración local (MVP1):
sin servidores, sin cuentas, SQLite como única persistencia, todo probable en local.

**Resultado:** MVP1 funcional, probado por el usuario con su cuenta real
(League es_MX, Riot ID detectado del cliente). 7 commits, 146 checks de test en verde.

### Commits de la sesión

| Commit | Contenido |
|---|---|
| `dece245` | Núcleo: modelos, SQLite, Data Dragon, ingesta, detectores D01–D10, misiones, Top 3, planner, quiz, LCU read-only, IPC, tests |
| `06879d6` | Ejecutables: Agent, Desktop, Overlay, Analyzer + README |
| `b3bf594` | Grabación local beta (WGC + Media Foundation H.264, opt-in) |
| `a6f6a33` | Iteración 1.1: sin API key (historial vía LCU), idioma del cliente, nombres de runas, iconos, overlay interactivo |
| `9ef934b` | Docs de la iteración 1.1 |
| `262a15c` | Iteración 1.2: UI oscura con iconografía Data Dragon, parche 26.x, configuración sin escribir |
| `2817d37` | Fix Alt+F4: InGame pegajoso, heartbeat de historial, análisis en un clic |

### Qué quedó construido

- **`riftloop_core`** (lib estática C++20): SQLite (perfil único local, partidas,
  análisis, misiones, oportunidades, skills, racha/XP, contratos §30, auditoría, kv),
  Data Dragon localizado con caché por versión+locale, cliente LCU read-only
  (lockfile), historial del cliente v4→v5 (`lcu_history.cpp`), 10 detectores
  explicables con métricas por oportunidad, motor de misiones con evaluación de
  bloque, Top 3, runas/hechizos/árbol de items validados contra el parche, quiz,
  impacto de parche, IPC por named pipe, caché de imágenes.
- **`RiftLoop.Agent.exe`**: tray, máquina de estados 1 Hz sobre LCU, heartbeat de
  estado a clientes IPC, heartbeat de historial cada 90 s (hilo aparte), refresco de
  Data Dragon por parche/locale, Top 3 y plan en vivo en champ select, prefetch de
  iconos, orquesta Overlay/Capture/Analyzer.
- **`RiftLoop.Desktop.exe`**: UI oscura propia (`ui.h/cpp`: tema, IconStore
  asíncrono, control ReportView con scroll), sidebar, 7 páginas, retratos e iconos
  de Data Dragon en listas y tarjetas, pool por desplegables + "Sugerir pool desde
  mi historial", Draft Lab con desplegables por slot, quiz centrado sobre la
  ventana de League con corrección en línea, feedback de diagnóstico, borrado total.
- **`RiftLoop.Overlay.exe`**: D2D, cuerpo click-through, cabecera interactiva
  (arrastrar/expandir/ocultar), iconos de items, render por evento.
- **`RiftLoop.Analyzer.exe`**: CLI (`--fetch-lcu`, `--auto`, `--analyze`, `--ingest`,
  `--report`, `--show`, `--wipe`); prioridad baja; API key solo como fallback CLI.
- **`RiftLoop.Capture.exe`**: grabación beta de la ventana del juego (30 FPS,
  ~2.5 Mbps, encoder hardware, cuota 2 GB), opt-in apagada por defecto.
- **Tests**: `riftloop_tests.exe`, 146 checks (detectores, misiones, recomendador,
  planner, quiz, contratos, convertidor LCU, locale, IPC, DB). Fixtures Data Dragon
  16.17.1 vendorizados + 3 partidas demo en `tests/fixtures/sample/`.

### Decisiones tomadas (y por qué)

- **Historial desde el propio cliente** (LCU `lol-match-history`) en vez de Riot API:
  los usuarios no van a poner una API key (feedback directo). La API queda como
  fallback de CLI. El endpoint no tiene SLA: todo degrada con mensaje.
- **Parche mostrado como 26.x** (`Ddragon::displayPatch`, major ddragon + 10): la
  numeración de Data Dragon (16.17.1) confundía frente al cliente (26.17).
- **Idioma de datos = locale del cliente** (`/riotclient/region-locale`), recordado
  en kv, fallback en_US. Los textos de la app siguen en español fijo.
- **Desktop Win32 con UI custom** en vez de WinUI 3 (deuda declarada frente a PRD
  §14.1): iteración local rápida; WinUI 3 queda para más adelante.
- **Cero escrituras al cliente de League**: correcto por diseño (Approval-Gated,
  PRD §17). No existen en el binario.
- **Estado InGame pegajoso**: la fase LCU sigue "InProgress" tras Alt+F4 sin proceso
  de juego; nunca degradar InGame→Loading. El post-match tiene 3 capas: transición a
  PostGame, heartbeat de historial (90 s) y botón manual "⟳ Buscar partidas nuevas".

### Gotcha de tooling (para futuras sesiones)

El wrapper del heredoc de Bash colapsa `\\n` → `\n` antes de llegar a Python: los
scripts de edición con escapes de C deben construir la barra con `chr(92)` o usar
el tool Edit. Ya causó dos ediciones fallidas silenciosas en esta sesión.

### Estado al cierre

- Build: `build.cmd` → limpio, sin warnings. Tests: 146/146.
- Verificado con capturas reales: UI oscura, parche 26.17, es_MX, Riot ID y pool
  del usuario detectados del cliente; página Misión con misión sugerida real (D01)
  derivada de sus partidas.
- Los binarios en `build\x64-debug\` quedaron recién compilados; los procesos del
  usuario fueron cerrados para re-enlazar — relanzar Agent + Desktop.

### Pendiente / siguiente sesión (candidatos)

1. Probar en vivo el flujo Alt+F4 → heartbeat → post-match automático (código
   listo, sin verificar con una partida real).
2. Clips: recorte del MP4 por timestamps de evidencia (hoy la grabación es la
   partida completa, sin alineación).
3. Data Dragon con retraso post-parche: detectar `gameVersion` de partida más nuevo
   que los datos y marcarlo (§21.4 congela automatización, falta la señal en UI).
4. Iconos también en listas del quiz de práctica y en combos del Draft Lab.
5. Performance harness (§15): los presupuestos siguen sin medirse formalmente.
6. Dark theme para headers de ListView (hoy quedan claros) y pulido de layout.
7. Localizar los textos de la app (quiz/condiciones) al idioma del cliente, no solo
   los datos.
8. Champion Guard read-only (alertas 10/5/3 s sin escritura) — P1 del PRD.
