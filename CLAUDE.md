# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Estado del repositorio

Iteración 1 implementada: MVP local sin servidores ni cuentas. C++20 + CMake + SQLite.
Estructura: `src/core/` (lib estática con toda la lógica), `src/agent|desktop|overlay|analyzer/`
(un exe cada uno), `tests/` (suite única con fixtures sintéticos + Data Dragon 16.17.1
vendorizado en `tests/fixtures/ddragon/`). Capture existe como beta opt-in (WGC + MF
H.264, sin recorte de clips). Las escrituras al cliente no existen en este build.
[README.md](README.md) documenta el uso.

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
build\x64-debug\riftloop_tests.exe   :: ejecuta la suite directamente
```

Los presets están en `CMakePresets.json` (`x64-debug`, `x64-release`). La suite es un solo
ejecutable con macros `CHECK`; no hay gtest. Deuda conocida frente al PRD §14.1: el Desktop
es Win32 puro (WinUI 3 pendiente) y no hay C++/WinRT todavía.

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
