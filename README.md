# RiftLoop — iteración 1 (MVP local)

Compañero nativo de mejora para League of Legends. Esta iteración es **100 % local**:
sin servidores, sin cuentas, sin sesión de usuario. Toda la persistencia vive en SQLite
en `%LOCALAPPDATA%\RiftLoop\`. El PRD completo está en [RiftLoop_PRD_v1.0.md](RiftLoop_PRD_v1.0.md).

## Qué hace este build

| Función | Estado |
|---|---|
| Detección de estado del cliente (LCU read-only, lockfile) | ✅ |
| Ingesta de partidas **desde el propio cliente de League** (sin API key) | ✅ |
| Ingesta alternativa: archivos JSON o Riot API con key local opcional (CLI) | ✅ |
| Datos y nombres en el **idioma del cliente** (runas, items, hechizos, campeones) | ✅ |
| Parche mostrado con la numeración real del cliente (26.x) | ✅ |
| UI oscura moderna con iconografía de Data Dragon (retratos, runas, hechizos, items) | ✅ |
| Configuración sin escribir: desplegables + "Sugerir pool desde mi historial" | ✅ |
| Quiz de loading centrado sobre la ventana de League, con explicación en línea | ✅ |
| Caché local de imágenes de Data Dragon (iconos de items en el overlay) | ✅ |
| 10 detectores explicables (D01–D10) con evidencia, confianza y exclusiones | ✅ |
| Misiones de 3–5 partidas con métricas por oportunidad y evaluación | ✅ |
| Árbol privado de habilidades, racha de mejora y XP saludable | ✅ |
| Top 3 de champion select (pool + proficiency + composición) | ✅ |
| Runas, hechizos y árbol de items validados contra el parche instalado | ✅ |
| Miniquiz de loading (ventana propia, máx. 3 preguntas) | ✅ |
| Overlay de items: cabecera interactiva (mover/expandir/ocultar), cuerpo click-through, iconos | ✅ |
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

## Probar con tus partidas reales (sin API key)

1. Abre el cliente de League y ejecuta `RiftLoop.Desktop.exe`.
2. En Perfil pulsa **"Descargar del cliente (League)"**: tu Riot ID, tu historial y los
   timelines se leen del propio cliente. Después, "Analizar pendientes".
3. Por CLI: `RiftLoop.Analyzer.exe --fetch-lcu 20 && RiftLoop.Analyzer.exe --analyze`.
4. (Opcional, solo CLI) La Riot API sigue disponible con una key local:
   `--set-key RGAPI-...` + `--fetch`. La clave se cifra con DPAPI.

## Probar con el cliente de League abierto

1. Ejecuta `RiftLoop.Agent.exe` (icono en la bandeja del sistema).
2. Abre League. El Agent detecta el estado por el lockfile (solo lectura).
3. En champion select, la pestaña Draft Lab del Desktop se rellena sola con el Top 3;
   al bloquear campeón aparece el plan de runas/hechizos/items.
4. En loading se abre el miniquiz (cerrarlo nunca penaliza).
5. Al entrar en partida se lanza el overlay: arrastra su barra superior para moverlo,
   [+] expande, [x] lo oculta el resto de la partida (Ctrl+Shift+O también expande).
   Si activaste la grabación en Ajustes, Capture graba la ventana del juego a MP4.
6. Al terminar, el Agent importa la partida desde el cliente y la analiza en prioridad
   baja; el Desktop muestra el resultado solo.

Los datos estáticos (nombres de runas, items, hechizos, campeones) se descargan en el
idioma del cliente (p. ej. es_AR) y se refrescan al cambiar el parche.

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
