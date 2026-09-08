# Front board — pitero

The live fronts of the team. One row per front. Humans write the rows.
`python3 -m harness session open` reads them and numbers them for the brief. The
numbers are per brief. Name a front by its text in a later session.

Relations:

- `docs/sessions/*.md` — history, one document per session, append only.
- `docs/session-log.md` — one row per session.
- `work/` — the committed work. The tree computes its state.
- this file — the fronts that are hot now, and who owns each one.

States: `active` · `paused` · `blocked` · `closed` (then move the row to the Archive).

## Fronts

| front | owner | state | touched | next step |
|---|---|---|---|---|
| Performance harness (PRD 15) | agent | active | 2026-09-06 (aa8ccf4) | TASK-0001 sin empezar. El usuario leyo menos de 10 MB a ojo, pero `tasklist` dio 30, 40 y 39 MB: hace falta el medidor |
| Sincronia de clips con el reloj de partida | user | active | 2026-09-06 (d53b810) | tres clips cortados y verificados a 1 s de error con el offset a mano. Falta grabar una partida con el binario nuevo y ver que Capture escribe el offset solo |
| El draft se lee de un vistazo (EP-02) | agent | active | 2026-09-07 (662df4e) | fuera los nueve desplegables, la tira de retratos dibuja picks y baneos por equipo. TASK-0010 y TASK-0011 esperan el ojo del usuario; el color no significa el lado del mapa hasta que TASK-0017 lo mida |
| La partida se ve entera (EP-03) | agent | active | 2026-09-08 (sesion) | mapa, curvas, rofl, podio y la tabla de los diez con rango real: 9 de 13 tareas cerradas. La cadena de Riot es account-v1 -> league-v4/by-puuid; el puuid del cliente no vale. Esperan el ojo TASK-0020, 0021, 0023 y 0024; TASK-0027 y TASK-0031 esperan una decision |
| El proyecto vive fuera de esta maquina (EP-04) | agent | active | 2026-09-07 (4a07f76) | publicado en <https://github.com/smmdsa/riftloop>, 39 commits y ni un dato personal en el historial (TASK-0028 cerrada por el usuario). Falta proteger `main` en GitHub: hoy la regla del PR es una convencion y el push directo sigue pasando |
| Muestra local de runas e items | agent | paused | 2026-09-04 (a3d6751) | el filtro por matchup no se activa: 7 a 11 filas por campeon, falta volumen |
| Escritura de runas al cliente | agent | paused | 2026-09-04 (771502d) | verificada en los cuatro caminos; apagada por defecto |
| Top 3 sin champion pool | agent | paused | 2026-09-04 (sesion) | hoy se calla; deberia proponer campeones del draft marcados fuera de pool |
| Escalera de misiones | agent | paused | 2026-09-04 (sesion) | disenada y discutida, sin implementar |

## Archive

| front | owner | state | touched | outcome |
|---|---|---|---|---|
| Harness instalado en el repo | agent | closed | 2026-09-07 (b4a8ecd) | cerrado por el usuario: el harness gobierna board, sesiones, indice en 8510 y ceremonias, y se usa en todas ellas. Queda TASK-0007, reportar los seis defectos de portabilidad al upstream, que ya no bloquea el uso |
