# RiftLoop — iteración 1 (MVP local)

Compañero nativo de mejora para League of Legends. Esta iteración es **100 % local**:
sin servidores, sin cuentas, sin sesión de usuario. Toda la persistencia vive en SQLite
en `%LOCALAPPDATA%\RiftLoop\`. El PRD completo está en [RiftLoop_PRD_v1.0.md](RiftLoop_PRD_v1.0.md).

## Qué hace este build

| Función | Estado |
|---|---|
| Detección de estado del cliente (LCU read-only, lockfile) | ✅ |
| Ingesta de partidas: archivos JSON o Riot API con key local opcional | ✅ |
| 10 detectores explicables (D01–D10) con evidencia, confianza y exclusiones | ✅ |
| Misiones de 3–5 partidas con métricas por oportunidad y evaluación | ✅ |
| Árbol privado de habilidades, racha de mejora y XP saludable | ✅ |
| Top 3 de champion select (pool + proficiency + composición) | ✅ |
| Runas, hechizos y árbol de items validados contra el parche instalado | ✅ |
| Miniquiz de loading (ventana propia, máx. 3 preguntas) | ✅ |
| Overlay de items externo, click-through, máx. 3 decisiones | ✅ |
| Impacto personalizado de parche (diff de snapshots Data Dragon) | ✅ |
| Contratos de explicación (PRD §30) y log de auditoría | ✅ |
| Grabación local beta (WGC + H.264, opt-in, solo ventana del juego) | ✅ |
| Escrituras al cliente (runas/hechizos/Champion Guard) | ⛔ no existen en este build (Approval-Gated, PRD §17) |

## Compilar

Requiere Visual Studio 2022 (MSVC + Windows SDK). CMake y Ninja se usan los de VS.

```bat
build.cmd            :: debug
build.cmd release
build.cmd test       :: build + ctest
```

Los binarios quedan en `build\x64-debug\`.

## Probar en 5 minutos (sin cliente de League)

```bat
cd build\x64-debug
RiftLoop.Analyzer.exe --set-riot-id "Demo#LAS"
RiftLoop.Analyzer.exe --ingest ..\..\tests\fixtures\sample\demo_1.json ..\..\tests\fixtures\sample\demo_2.json ..\..\tests\fixtures\sample\demo_3.json
RiftLoop.Analyzer.exe --analyze
RiftLoop.Analyzer.exe --report
```

Después abre `RiftLoop.Desktop.exe`: pestañas Perfil, Partidas, Post-match, Misión,
Draft Lab, Parche y Ajustes. En Misión pulsa "Sugerir misión" y acéptala.

## Probar con tus partidas reales

1. Consigue una API key de desarrollo en https://developer.riotgames.com (gratis, caduca cada 24 h).
2. En Desktop → Perfil: escribe tu Riot ID (`Nombre#TAG`), guarda la clave y pulsa
   "Descargar partidas (API)". O por CLI:
   `RiftLoop.Analyzer.exe --set-key RGAPI-... && RiftLoop.Analyzer.exe --fetch 20 && RiftLoop.Analyzer.exe --analyze`
3. La clave se guarda cifrada con DPAPI y solo sale de tu equipo hacia la API de Riot.

## Probar con el cliente de League abierto

1. Ejecuta `RiftLoop.Agent.exe` (icono en la bandeja del sistema).
2. Abre League. El Agent detecta el estado por el lockfile (solo lectura).
3. En champion select, la pestaña Draft Lab del Desktop se rellena sola con el Top 3;
   al bloquear campeón aparece el plan de runas/hechizos/items.
4. En loading se abre el miniquiz (cerrarlo nunca penaliza).
5. Al entrar en partida se lanza el overlay (Ctrl+Shift+O expande, Ctrl+Shift+M lo mueve).
   Si activaste la grabación en Ajustes, Capture graba la ventana del juego a MP4.
6. Al terminar, el Agent lanza el análisis en prioridad baja y avisa al Desktop.

## Procesos

- `RiftLoop.Agent.exe` — máquina de estados, LCU read-only, IPC, tray. Persistente.
- `RiftLoop.Desktop.exe` — panel Win32 (perfil, análisis, misiones, draft lab).
- `RiftLoop.Overlay.exe` — ventana externa transparente; solo visible en partida.
- `RiftLoop.Analyzer.exe` — ingesta y análisis por CLI; prioridad baja.
- `RiftLoop.Capture.exe` — grabación opt-in de la ventana del juego (beta).
  Prueba manual: `RiftLoop.Capture.exe --window "RiftLoop" --seconds 5` graba la ventana
  del Desktop. Límites beta: sin recorte de clips, sin audio, resolución nativa de ventana.

## Datos y privacidad

- Todo local: `%LOCALAPPDATA%\RiftLoop\` (`riftloop.db`, `config.json`, caché Data Dragon).
- API key cifrada con DPAPI (`riot_key.bin`); nunca en logs ni en la base.
- "Borrar TODOS los datos" en Ajustes elimina perfil, partidas, análisis y progreso.
- Este build no escribe nada en el cliente de League y no envía datos a servidores propios.

## Tests

```bat
build.cmd test
```

125 checks: detectores, misiones, recomendador, planner, quiz, contratos, IPC y DB.
Los fixtures de Data Dragon (16.17.1) están vendorizados en `tests/fixtures/ddragon/`.
