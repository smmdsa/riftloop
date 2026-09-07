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
| Performance harness (PRD 15) | agent | active | 2026-09-06 (62d893d) | TASK-0001 sin empezar. El usuario leyo menos de 10 MB a ojo, pero `tasklist` dio 30, 40 y 39 MB: hace falta el medidor |
| Sincronia de clips con el reloj de partida | user | active | 2026-09-06 (1e26669) | tres clips cortados y verificados a 1 s de error con el offset a mano. Falta grabar una partida con el binario nuevo y ver que Capture escribe el offset solo |
| El draft se lee de un vistazo (EP-02) | agent | active | 2026-09-07 (9dbdc4f) | fuera los nueve desplegables, la tira de retratos dibuja picks y baneos por equipo. TASK-0010 y TASK-0011 esperan el ojo del usuario; el color no significa el lado del mapa hasta que TASK-0017 lo mida |
| La partida se ve entera (EP-03) | agent | active | 2026-09-07 (sesion) | TASK-0018, 0019 y 0022 cerradas. Mapa de calor, curvas, detector D11 y rango propio implementados y esperando el ojo del usuario (TASK-0020, 0021, 0023, 0024). TASK-0025 no empieza hasta que el usuario decida si gasta cuota de su clave y si guarda el rango ajeno |
| Muestra local de runas e items | agent | paused | 2026-09-04 (a3d6751) | el filtro por matchup no se activa: 7 a 11 filas por campeon, falta volumen |
| Escritura de runas al cliente | agent | paused | 2026-09-04 (771502d) | verificada en los cuatro caminos; apagada por defecto |
| Top 3 sin champion pool | agent | paused | 2026-09-04 (sesion) | hoy se calla; deberia proponer campeones del draft marcados fuera de pool |
| Escalera de misiones | agent | paused | 2026-09-04 (sesion) | disenada y discutida, sin implementar |

## Archive

| front | owner | state | touched | outcome |
|---|---|---|---|---|
| Harness instalado en el repo | agent | closed | 2026-09-07 (e25ce14) | cerrado por el usuario: el harness gobierna board, sesiones, indice en 8510 y ceremonias, y se usa en todas ellas. Queda TASK-0007, reportar los seis defectos de portabilidad al upstream, que ya no bloquea el uso |
