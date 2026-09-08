# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Estado del repositorio

MVP local sin servidores ni cuentas. C++20 + CMake + SQLite. Estructura: `src/core/`
(lib estática con toda la lógica), `src/agent|desktop|overlay|analyzer|capture/` (un exe
cada uno), `tests/` (suite única con fixtures sintéticos + Data Dragon 16.17.1 vendorizado
en `tests/fixtures/ddragon/`). Lee [WORK-LOG.md](WORK-LOG.md) antes de tocar nada: tiene el
detalle de cada decisión y de los bugs que ya se cazaron.

**Iconos que Data Dragon no trae.** `assets/icons/` guarda 20 PNG (101 KB): los cinco
iconos de línea, cuatro marcas de columna y los once emblemas de rango. Medido el
2026-09-07: Data Dragon sirve `champion`, `item`, `spell`, `passive`, `profileicon`, `map`,
`sprite` y `perk-images`, y responde 403 a todo `img/ui/*`; el cliente responde 400 a las
rutas de posición. Salen de `github.com/noxelisdev/LoL_DDragon`, carpeta `extras/`, cuyo
propio README dice que no forman parte de Data Dragon. **Se copian al repositorio, no se
descargan en ejecución**: ese repositorio pesa 10.5 GB, no declara licencia y lo mantiene
una persona. `assets/icons/SOURCE.md` guarda la procedencia y `rlui::localIcon` devuelve
nullptr si la carpeta no está, así que cada vista tiene su respaldo dibujado o de texto.

**Fuentes de datos.** La ingesta primaria es el historial del propio cliente (LCU
`lol-match-history`, v4 convertido a v5 en `src/core/lcu_history.cpp`); la Riot API con key
es fallback de CLI. Cada partida importada aporta las 10 páginas de runas y las 10 builds
de esa partida a la tabla `builds`: esa es la muestra local que alimenta runas e items
(`src/core/meta.cpp`). **No hay ni debe haber scraping de sitios de terceros.** Data Dragon
se descarga en el locale del cliente (`resolveDataLocale`) con iconos en `cache/img/`.

**Escrituras al cliente.** Existe una y sólo una: la página de runas
(`src/core/perkpages.cpp`, RF-RUN-003). Va tras `Config::runeWriteEnabled`, apagada por
defecto, con diff previo, Undo de un paso y auditoría. Gestiona **una** página, la que
empieza por "RiftLoop", y nunca lee, edita ni borra una página personal; si no hay hueco
libre, se bloquea en vez de hacer sitio. Cada aplicación necesita un clic humano.

**Video.** Capture graba la ventana del juego (opt-in) y escribe un sidecar
`<archivo>.mp4.json` con el reloj de partida del momento en que empezó
(`liveGameTimeSec`): sin ese dato no se puede alinear nada y no se corta. Al analizar,
`src/core/clipmaker.cpp` corta los momentos de la evidencia con Media Foundation sin
recodificar y borra el raw, salvo que falle algún corte o `keepFullRecording` esté activo.
El post-match muestra la playlist con miniaturas y un reproductor embebido con su barra de
controles (`src/desktop/player.cpp`).

**UI.** Desktop Win32 con UI oscura propia: `src/desktop/ui.h` define el tema, el almacén
asíncrono de iconos (`WM_APP_ICONS`) y el control `ReportView` (filas clicables vía
`RVItem::action` + `WM_RV_ACTION`, tarjetas de clip vía `RVKind::ClipCard`). El parche se
muestra con la numeración del cliente vía `Ddragon::displayPatch` (ddragon 16.x -> cliente
26.x). [README.md](README.md) documenta el uso.

### CLI del Analyzer (la vía rápida para probar sin UI)

```
--fetch-lcu [N]   --meta [N]          --plan <Campeon> <ROL> [rivales...]
--analyze         --autoclip <id>     --clips <id>        --replay-check
--apply-runes <Campeon> <ROL>         --undo-runes
--cut-test <mp4> <seg>                --frame <mp4> <seg> <png>
--report          --show <id>         --wipe
--export-fixture <id|last> <archivo>  --heatmap [N] [ROL]
--curves <id|last>                    --rofl-stats <id|last>
--ranks <id|last> [refresh|force] [participantId]
```

`--repair-positions` arregla TOP y UTILITY en las partidas ya importadas. El cliente
etiqueta al top como `JUNGLE` y daba el rol del support como `SUPPORT`, no `DUO_SUPPORT`:
medido el 2026-09-07, 59 de las 67 partidas del usuario tenían dos JUNGLE y dos BOTTOM por
equipo. `repairPositionsV5` (`src/core/lcu_history.cpp`) los separa por Castigo y por
farmeo, sobre el JSON ya guardado y sin pedirle nada a Riot.

`--frame` vuelca un fotograma a PNG: es la forma de comprobar que un clip cae donde dice
(el reloj del juego se lee en la esquina superior derecha).

`--heatmap`, `--curves`, `--rofl-stats` y `--ranks` son las vistas de EP-03 en texto: cada
una imprime los mismos numeros que dibuja el Desktop, para poder comprobarlos sin abrir una
ventana. `--export-fixture` saca una partida de la base ya anonimizada, que es la unica
forma de meter datos reales en `tests/fixtures/`.

`--ranks ... refresh` gasta cuota de la API de Riot: dos llamadas por jugador, espaciadas
1300 ms para caber en la cuota de una clave de desarrollo. Solo funciona sobre una partida
ya guardada; un id que no este en la tabla `matches` se rechaza antes de mirar la clave, y
esa es la barrera contra el scouting de champion select (PRD 17.2).

### Actualizar el harness

El paquete `harness/` esta vendorizado: son archivos normales, no un submodulo. Un
submodulo no serviria, porque git no monta subdirectorios y habria que traer el repo
entero, que ademas no se clona en Windows sin `core.longpaths=true`.

El remoto vive en `.git/config` y no viaja con los commits. En una maquina nueva:

```bash
git remote add harness-upstream https://github.com/smmdsa/claudia-rag-pipeline.git
git remote set-url --push harness-upstream DISABLED-no-push   # el push falla en seco
```

Para actualizar:

```bash
git fetch harness-upstream main
git log --oneline HEAD..harness-upstream/main -- harness/     # que cambio en el paquete
git checkout harness-upstream/main -- harness/                # solo el paquete
python -m harness upgrade                                     # resiembra las plantillas
python -m harness doctor                                      # exit 0
```

**`upgrade` no respeta del todo `.claude/settings.json`, aunque este adoptado.** Medido al
subir de 0.1.0 a 0.2.0:

- **Duplica los hooks.** Anade el juego de la plantilla junto al que ya estaba, asi que
  cada evento dispara dos veces: el nuestro con el shim y el de la plantilla, que en
  Windows llama al stub de la Store. `doctor` sale sano igual, porque solo busca que la
  cadena exista.
- **Restaura la lista `deny` de la plantilla**, y con ella `taskkill`, que aqui rompe el
  ciclo de build.

Asi que despues de cada `upgrade` hay que revisar `.claude/settings.json` a mano:

1. Dejar un solo hook por evento, el que lleva `PATH="$CLAUDE_PROJECT_DIR/tools/bin:$PATH"`.
2. Quitar `Bash(taskkill*)` del `deny`.
3. `python -m harness adopt .claude/settings.json` y `python -m harness doctor`.
4. Ejecutar el comando del hook tal cual, en Git Bash y no en WSL: son shells distintos con
   rutas distintas, y en WSL el shim no esta en el PATH.

El resto de plantillas (`.claude/rules/harness.md`, las skills, `infra/rag/up.sh`,
`work/README.md`) se actualizan solas y sin sorpresas.

**Cuidado con `init` despues de un upgrade:** 0.2.0 trae una plantilla
`work/backlog/TASK-0001-start-the-harness-in-this-repository.md`. Este repositorio ya tiene
una TASK-0001 en `sprint-001`, asi que sembrarla crearia dos con el mismo id. `upgrade` no
la siembra; `init` si lo haria.

### El indice de busqueda en WSL, y por que 8510

El motor de Docker de esta maquina corre **dentro de `Ubuntu-22.04`**, no en Docker
Desktop: el `docker` de Windows apunta a `npipe:////./pipe/dockerDesktopLinuxEngine` y esa
tuberia no existe. Por eso `python -m harness stack ...` falla siempre aqui. Los
contenedores se levantan desde WSL a mano:

```bash
wsl -d Ubuntu-22.04
cd /mnt/e/pitero
docker compose --env-file .harness/env.wsl -f infra/rag/docker-compose.yml   up -d --no-build
docker compose --env-file .harness/env.wsl -f infra/board/docker-compose.yml up -d --no-build
```

`.harness/env.wsl` existe porque `harness env` escribe rutas de Windows en
`.harness/env.local` y compose necesita las de WSL (`/mnt/e/pitero`). Git lo ignora, igual
que a `env.local`. Ese archivo tambien corrige `HARNESS_MEMORY_DIR`: el harness lo calcula
mal en Windows y apunta a `E:\pitero\memory`, que no existe; la memoria real esta en
`C:\Users\<user>\.claude\projects\e--pitero\memory` (TASK-0007, defecto 5).

**Puertos 8510, 8511 y 8512.** Hay otro repositorio con el mismo harness en esta maquina, y
su stack toma 8410 a 8412 por defecto. Este proyecto se mueve a 8510 para que los dos
convivan. Los tres sitios que llevan el numero son `.harness/env.wsl`, `.harness/env.local`
y `.mcp.json`. Todo lo demas se nombra por proyecto: contenedores `pitero-rag`,
`pitero-rag-agent` y `pitero-board`, imagenes `pitero-rag:cpu` y `pitero-board:latest`,
volumenes `pitero-rag_*` y `pitero-board_*`.

`harness ports` **no sirve de prueba aqui**: dice `free` sobre un puerto que un contenedor
de WSL esta sirviendo (TASK-0007, defecto 6). La comprobacion real es
`curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8510/health`.

Si se cambia el puerto de `.mcp.json`, Claude Code no lo lee hasta reiniciar: abre la
conexion MCP una vez, al arrancar el proceso. Mientras tanto se puede consultar el indice
por HTTP contra `8510/mcp`, o parar y buscar con grep.

RiftLoop es un compañero nativo de mejora para League of Legends en Windows 10/11 x64.
El PRD [RiftLoop_PRD_v1.0.md](RiftLoop_PRD_v1.0.md) es la fuente de verdad. Antes de
implementar una función, lee su sección `RF-*`.

Índice rápido de las secciones que se consultan más:

| Tema | Sección |
|---|---|
| Decisiones ejecutivas cerradas | §0 |
| Requisitos funcionales (`RF-ONB`, `RF-CS`, `RF-RUN`, `RF-SUM`, `RF-ITEM`, `RF-QUIZ`, `RF-OVR`, `RF-REC`, `RF-POST`) | §9 |
| Detectores D01–D10 | §9.12 |
| Arquitectura de procesos, IPC, máquina de estados | §14 |
| Presupuestos de rendimiento (release gates) | §15 |
| Cumplimiento Riot, matriz roja, kill switches | §17 |
| Capas de test y escenarios críticos | §22 |
| Contratos de explicación y evidencia | §30 |
| Backlog P0/P1/P2 | §31 |
| Definition of Done por función | §36 |

## Build y pruebas

CMake no está en el PATH del sistema: usa `build.cmd`, que carga `VsDevCmd` (VS 2022
Community) y el CMake/Ninja embebidos de VS.

```bat
build.cmd            :: configura + compila debug (build\x64-debug)
build.cmd release
build.cmd test       :: compila + ctest
buildt.cmd <target>... :: compila solo esos targets
build\x64-debug\riftloop_tests.exe   :: ejecuta la suite directamente
```

`buildt.cmd` existe porque `build.cmd` falla con `LNK1168` cuando el usuario tiene el
Desktop, el Agent o el Overlay abiertos: el enlazador no puede escribir un exe en uso.
Con `buildt.cmd RiftLoop.Analyzer riftloop_tests` se compila lo que hace falta sin
cerrarle la aplicacion al usuario.

Los presets están en `CMakePresets.json` (`x64-debug`, `x64-release`). La suite es un solo
ejecutable con macros `CHECK`; no hay gtest. Deuda conocida frente al PRD §14.1: el Desktop
es Win32 puro (WinUI 3 pendiente) y no hay C++/WinRT todavía.

## Git: el trabajo entra por rama y PR

El repositorio es publico: <https://github.com/smmdsa/riftloop>. **`main` no acepta un
push directo.** Todo cambio entra por una rama y un pull request, y lo aprueba el usuario.

```bash
git checkout -b <area>/<lo-que-hace>     # ep-03/mapa-de-calor, fix/clip-desalineado
# ... trabajo, y la suite en verde antes de commitear ...
git push -u origin <rama>
gh pr create --base main --title "..." --body "..."
```

El nombre de la rama empieza por el area (`ep-03`, `draft`, `clips`, `docs`, `fix`, `flujo`)
y sigue con lo que hace, en minusculas y con guiones. Un commit por cambio logico, y el
cuerpo explica por que, no que.

**Antes de abrir el PR, la suite pasa entera.** El numero de checks va en el cuerpo del PR.
Un PR que no dice cuantos checks corrio no se mira.

**El repositorio es publico, asi que nada personal entra en un commit.** Ni el nick de
invocador, ni una ruta con el nombre de usuario de Windows, ni una clave de la API de Riot,
ni un puuid ajeno. Un fixture de una partida real se exporta con `--export-fixture`, que
anonimiza al escribir. `docs/sha-map.md` guarda el mapa de SHA de la unica reescritura del
historial, la del 2026-09-07, que quito esos datos antes de publicar.


## Arquitectura: seis procesos, un agente que orquesta

El cliente **nunca** es un proceso único. §14.2 define la topología. Cada proceso existe para aislar
un presupuesto de rendimiento o un riesgo de cumplimiento distinto:

- **`RiftLoop.Agent.exe`** — Win32 liviano y persistente. Es el centro: máquina de estados, APIs
  locales aprobadas, feature flags, consentimientos, health. Orquesta a los demás. Sin UI pesada.
- **`RiftLoop.Desktop.exe`** — WinUI 3. Onboarding, dashboard, post-match, árbol de habilidades.
  Se suspende o recorta su working set durante la partida.
- **`RiftLoop.Overlay.exe`** — ventana externa transparente con Direct2D/DirectWrite/DirectComposition.
  Click-through por defecto. Render por evento, nunca por bucle.
- **`RiftLoop.Capture.exe`** — solo existe si la grabación está activa. WGC + D3D11 + Media Foundation.
- **`RiftLoop.Analyzer.exe`** — solo post-match, prioridad baja. Se pausa en cuanto empieza otra partida.
- **`RiftLoop.Update.exe`** — binario mínimo firmado. Actualización atómica con rollback.

**IPC:** named pipes con ACL por usuario, handshake de versión, mensajes binarios versionados con
límite de tamaño y timeout. Ningún proceso espera de forma indefinida. El overlay recibe un ViewModel
mínimo, no bases completas. **Capture nunca recibe tokens de Riot.**

**Señal global `Game Active`:** todos los módulos la respetan (§9.3). Rige la tabla de §14.4: durante
`In game`, Desktop se suspende y Analyzer está pausado. Si RiftLoop falla, League no se cae.

**Backend:** Rust o C++ (§27, decisión abierta). La API key de Riot vive solo en servidor. Ingesta
idempotente por match ID. Ningún componente del cliente depende de JavaScript.

## Prohibiciones técnicas — no negociables

§14.5 y la matriz roja de §17.2 no admiten excepción. No escribas código que haga esto, aunque se
pida como prueba o prototipo:

- Inyección de DLL, hooks de DirectX en el proceso del juego, lectura o escritura de memoria del juego.
- Kernel drivers, packet sniffing, low-level keyboard hooks.
- Simulación de mouse o teclado para gameplay.
- Electron, Tauri, Chromium embebido, WebView o cualquier runtime JS en el cliente.
- OCR continuo cuando una API legítima da el dato.
- Servicios siempre activos con privilegios de administrador.
- Instrucciones tácticas en tiempo real, información oculta de la sesión, scouting de identidades anonimizadas.

Para hotkeys usa `RegisterHotKey` o un mecanismo equivalente de alcance limitado.

### Funciones bloqueadas por aprobación

`RF-CS-007` (auto-lock a 1 segundo) es **Approval-Gated / No-Ship**. El código de producción no
ejecuta esa acción. Si se implementa la especificación, se compila fuera de producción (§26).
La alternativa que sí se envía es **Champion Guard** (`RF-CS-006`): avisos a 10, 5 y 3 segundos,
respaldo preconfigurado y **una confirmación humana** por clic o hotkey. Sin confirmación, cero acción.
Champion Guard todavía no está implementado.

La escritura de runas (`RF-RUN-003`, §17.2 **amarillo**) sí está implementada con todas sus
condiciones. La diferencia entre amarillo y rojo es una acción humana explícita: si algún día
se añade una ruta que aplique algo sin un clic, deja de ser amarillo.

Toda escritura al cliente de League (runas, hechizos, item sets, Champion Guard) llega detrás de un
feature flag independiente, apagada por defecto, con diff, Undo de un clic y registro de auditoría
local (§17.3, §17.4). Si el usuario edita a mano después, no se sobrescribe. El servidor puede forzar
read-only; el cliente conserva un safe default si no recibe flags.

## Presupuestos de rendimiento: son gates, no metas

§15 define los límites en la máquina base (i5-8250U / Ryzen 3 1200, 8 GB, UHD 630). Una función que
los supera no se envía. Los más restrictivos, durante partida y sin grabación:

- Working set p95 total ≤ 80 MB. Con grabación ≤ 200 MB.
- CPU promedio ≤ 0.75 %, p95 ≤ 2 %.
- Degradación de FPS mediana ≤ 2 %, 1 % low ≤ 3 %, frame-time p95 ≤ 0.5 ms.
- Escrituras de disco ≈ 0 sin captura activa.

Reglas de diseño que salen de ahí: overlay con heartbeat máximo de 1 FPS y render solo al invalidar;
nada atado a refresco de 144/240 Hz; ningún polling por debajo de 250 ms sin aprobación — prefiere
eventos; inferencia pesada solo post-match o en cloud. Si la captura no sostiene el presupuesto, baja
calidad o se apaga: **nunca degrada el juego en silencio**.

## Reglas de producto que condicionan el código

- **Presupuesto cognitivo (§10.2).** Champion select: 3 opciones con 3 razones. Loading: 3 preguntas.
  In-game: 3 alternativas de compra de una línea. Post-match inicial: 1 fortaleza, 1 problema,
  3 evidencias. Son máximos duros en la UI, no sugerencias.
- **Una misión primaria activa (§3.4).** El sistema registra otros patrones pero no los convierte en
  tareas simultáneas.
- **Evidencia o silencio (§3.3).** Un diagnóstico sin fuente, instante, regla, confianza y
  limitaciones no se muestra.
- **Contratos de explicación (§30).** Toda recomendación y toda evidencia se serializan con los campos
  listados ahí (`recommendation_id`, `patch`, `inputs_used`, `confidence`, `uncertainty_reason`,
  `model_or_ruleset_version`, `expires_at`…). La UI consume ese objeto; no compone explicaciones por
  su cuenta.
- **Opportunity-based metrics (§12.2).** El denominador de una métrica es una oportunidad válida, no
  la cantidad de partidas. Cada detector define evento de oportunidad, exclusiones, conducta esperada,
  ventana y confianza.
- **Versionado de datos (§13.2).** Cada observación guarda `gameVersion`, patch family, data snapshot,
  ruleset version, model version, client integration version y timestamp UTC. No se comparan partidas
  incompatibles sin normalizar. Si los IDs no coinciden con el parche instalado, la automatización se
  congela (§21.4).
- **Modelos generativos (§12.4).** Solo convierten hechos estructurados en lenguaje. No inventan
  timestamps, no infieren intención, no emiten causalidad y no sustituyen al motor determinista de
  legalidad. Toda salida pasa por esquema validado y grounding.
- **Sin blame (§9.11).** No se etiqueta a compañeros como causa. Solo decisiones que el usuario controla.
  El podio del historial (`src/core/podium.cpp`) sale de esta regla: reparte títulos por lo que
  cada uno **hizo** (`EL SMURFER`, `MVP-CARRY`, `EL VERDUGO`…) y ninguno es negativo. Los últimos
  puestos llevan su número y nada más.
- **Puntuar una partida, nunca a un jugador (§7).** `buildPodium` da 0 a 100 con cinco factores de
  peso escrito, y la UI enseña el desglose completo en un tooltip. Ese número **no se guarda en la
  base y no se compara entre partidas**: guardarlo lo convertiría en la puntuación equivalente a un
  MMR alternativo que §7 prohíbe.

## Privacidad por defecto

Grabación opt-in y apagada por defecto. Micrófono siempre apagado por defecto. Audio del juego es un
opt-in aparte. Clips locales por defecto, con borrado y exportación en un clic. Secretos en DPAPI o
Credential Manager. Los logs no llevan chat, teclas, video ni tokens (§16, §21.2, §29).

**Modo Sólo análisis** (§6.3) es el fallback universal: cero escrituras al cliente, sin overlay. Toda
función debe tener un estado offline/read-only que funcione.

## Orden de trabajo

El PRD ya fija la prioridad. §37 pide un vertical slice honesto antes de ampliar features: leer 20
partidas → detectar un patrón de alta confianza → dos evidencias de timeline → una misión de tres
partidas → grabar una partida y producir un clip alineado → comprobar el cambio. En paralelo,
demostrar el presupuesto nativo con Agent, Overlay y Capture.

No implementes funciones P1 o P2 (§31) mientras P0 esté incompleto: máquina de estados, ingesta de
match/timeline, perfil básico, tres detectores, evidencia, misión y verificación, Top 3 read-only,
quiz, overlay estático, captura beta, performance harness, consentimiento y kill flags.

## Definition of Done (§36)

Una función no está terminada hasta que tiene: criterio de aceptación, clasificación de política
(verde/amarillo/rojo de §17.2), estado offline/read-only, tests unitarios y de integración,
cumplimiento del performance budget medido, telemetría mínima, accesibilidad, localización es/en,
kill switch si toca a Riot, manejo de error, documentación de privacidad y prueba en el parche actual.
