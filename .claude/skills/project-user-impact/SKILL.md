---
name: project-user-impact
description: States what the end user loses when a change fails. Use before a task with eye RUN or GLANCE closes, and in every review.
---

# Project user impact

Purpose: **Convierte las partidas propias de League of Legends en una unica mision de mejora medible, con la evidencia en video que la sostiene**. End user: **Un jugador de League que quiere mejorar sin estudiar horas de contenido generico; si el producto falla pierde el diagnostico y los clips que lo respaldan, y en el peor caso ve caer los FPS de su partida**.

1. Run the `end-user-impact` skill with the task id.
2. Write one sentence: what the user cannot do if this change is wrong.
3. Name the check that a person must run to see the change. A green build is not a working feature.
4. Put that sentence in the task file under `## Done when`.
