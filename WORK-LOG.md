# WORK-LOG — RiftLoop

Registro de sesiones de desarrollo. La entrada más reciente va arriba.

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
