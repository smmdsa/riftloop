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
| Performance harness (PRD 15) | agent | active | 2026-09-05 (sprint-001) | TASK-0001: muestrear working set y CPU |
| Sincronia de clips con el reloj de partida | user | blocked | 2026-09-04 (5f0bd8c) | TASK-0005: hace falta una partida grabada entera con el binario nuevo |
| Muestra local de runas e items | agent | paused | 2026-09-04 (a3d6751) | el filtro por matchup no se activa: 7 a 11 filas por campeon, falta volumen |
| Escritura de runas al cliente | agent | paused | 2026-09-04 (771502d) | verificada en los cuatro caminos; apagada por defecto |
| Top 3 sin champion pool | agent | paused | 2026-09-04 (sesion) | hoy se calla; deberia proponer campeones del draft marcados fuera de pool |
| Escalera de misiones | agent | paused | 2026-09-04 (sesion) | disenada y discutida, sin implementar |

## Archive

| front | owner | state | touched | outcome |
|---|---|---|---|---|
