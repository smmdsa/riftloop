# RiftLoop — PRD integral

**Producto:** compañero nativo de mejora para League of Legends  
**Nombre de trabajo:** RiftLoop  
**Versión del documento:** 1.0  
**Estado:** propuesta lista para validación y estimación técnica  
**Fecha de corte:** 2 de septiembre de 2026  
**Plataforma inicial:** Windows 10/11 x64  
**Idioma del PRD:** español  

> RiftLoop es un nombre provisional. Antes de usarlo públicamente se deben comprobar marca, dominio, redes sociales y posibles conflictos comerciales.

---

## 0. Decisiones ejecutivas

1. El cliente será **100 % nativo en C++20 para Windows**. No se usarán Electron, Tauri, JavaScript, Chromium embebido, WebView, inyección de DLL, lectura de memoria ni drivers.
2. El producto no intentará ser “otro overlay con estadísticas”. Su núcleo será un ciclo verificable de mejora: **evidencia propia → explicación → una misión concreta → medición en las siguientes partidas**.
3. La experiencia tendrá dos modos:
   - **Modo Escalar:** recomendaciones conservadoras, prioriza dominio real y consistencia.
   - **Modo Aprender:** permite ampliar champion pool y asumir objetivos formativos.
4. La selección automática de campeón cuando queda un segundo representa un riesgo elevado de política y sanción. Se documenta como **Approval-Gated / No-Ship**: no se habilita en producción sin aprobación escrita y específica de Riot.
5. La alternativa inicial será **Champion Guard**: avisos a 10, 5 y 3 segundos, campeón de respaldo preconfigurado y una única confirmación humana por clic o hotkey. Nunca actuará sin esa confirmación.
6. La aplicación automática de runas, hechizos y páginas de objetos será transparente, reversible y controlada mediante feature flags. Cualquier escritura al cliente queda condicionada al registro y revisión de Riot.
7. El overlay dentro de partida será mínimo y contextual: como máximo 2–3 decisiones de compra con condiciones breves. No dará órdenes tácticas en tiempo real ni reaccionará a información oculta.
8. La grabación será opt-in, por ventana del juego, con codificación H.264 por hardware. El sistema conservará clips de evidencia y eliminará el resto por defecto.
9. Los presupuestos de CPU, memoria, GPU, disco e impacto en FPS serán **release gates**, no aspiraciones.
10. El MVP empezará con reglas y detectores explicables. No se entrenará una gran “IA” antes de demostrar que los diagnósticos son correctos y útiles.

---

## 1. Resumen del producto

RiftLoop es una aplicación nativa para jugadores de League of Legends que transforma datos de draft, partida, timeline y video local en decisiones comprensibles y hábitos medibles.

Durante champion select ayuda a elegir entre tres campeones válidos para ese jugador y esa composición; prepara runas, hechizos y una estrategia de objetos; en loading convierte el plan en un mini desafío; durante la partida muestra únicamente recordatorios de compra de baja distracción; y al terminar encuentra momentos concretos del video que fundamentan el diagnóstico.

La unidad de valor no será “un reporte con muchas métricas”. Será una **intervención de mejora**:

1. Detectar un patrón relevante con evidencia.
2. Explicar por qué importa.
3. Mostrar una alternativa ejecutable.
4. Proponer una sola misión para 3–5 partidas.
5. Verificar si el comportamiento cambió.
6. Actualizar el árbol privado de habilidades del jugador.

### 1.1 Propuesta de valor

**Para jugadores que quieren mejorar sin estudiar horas de contenido genérico, RiftLoop convierte sus propias partidas en lecciones visuales cortas y comprueba si aplicaron lo aprendido.**

### 1.2 Tesis diferencial

El mercado ya ofrece builds, runas, scouting, timers, estadísticas y análisis generales. Incluso funciones como objetivos en partida o detección automática de highlights existen en competidores. La diferenciación defendible es:

- Problemas detectados en el contexto exacto del usuario.
- Clips propios que respaldan cada afirmación.
- Una recomendación acotada en vez de veinte observaciones.
- Seguimiento longitudinal de si la intervención produjo cambio.
- Confianza explícita y posibilidad de corregir al sistema.
- Un modelo de progresión saludable que premia el proceso, no el spam de partidas.

La visión resumida es: **“puzzles de League of Legends generados desde tus propias partidas”**.

---

## 2. Contexto competitivo

### 2.1 Referente principal: iTero

iTero se posiciona como una herramienta de coaching y draft basada en datos. Su propuesta pública incluye recomendaciones personalizadas de campeón, análisis de composición, runas y builds, scouting, estadísticas de cuenta, timers y asistencia macro. Dispone de versión standalone y presencia en Overwolf.

Fortalezas observadas:

- Cobertura amplia de pregame, in-game y postgame.
- Modelo de recomendación sensible al usuario y a la composición.
- Comunicación clara de “AI coaching”.
- Distribución y reconocimiento ya establecidos.

Oportunidades que RiftLoop debe explotar:

- Reducir la sobrecarga de métricas y priorizar una acción.
- Probar cada diagnóstico con evidencia visual.
- Evitar presentar como “builds distintas” simples reordenamientos del mismo conjunto.
- Medir aprendizaje real entre partidas.
- Hacer del consumo bajo de recursos una promesa verificable.
- Dar control al usuario sobre confianza, privacidad y automatización.

### 2.2 Competidores adyacentes

| Producto/categoría | Fortaleza habitual | Riesgo de copiar sin diferenciación | Respuesta de RiftLoop |
|---|---|---|---|
| iTero | Draft, composición y coaching basado en datos | Ser un clon con otra interfaz | Cierre del ciclo de aprendizaje con evidencia y verificación |
| Mobalytics | Goals, overlay, detección de highlights | Gamificación y objetivos ya existen | Misiones derivadas de patrones propios y calibradas por confianza |
| Porofessor | Pregame, análisis de match y replays de referencia | Scouting y reportes son commodities | Evitar profiling invasivo; enfocarse en el usuario y sus decisiones |
| Blitz / U.GG / OP.GG | Builds, runas, tier lists y volumen de datos | Competir sólo por “mejor win rate” | Árboles de decisión situacionales y explicación causal |
| Herramientas de clips | Captura eficiente y edición | Video sin coaching | Clips sincronizados a hipótesis de mejora |
| Coaches humanos | Contexto, interpretación y adaptación | Alto coste y baja escalabilidad | Evidencia explicable, feedback del usuario y escalamiento opcional a coach |

### 2.3 Posicionamiento

RiftLoop no competirá por tener la mayor cantidad de widgets. Competirá por responder mejor tres preguntas:

1. **¿Qué decisión mía limita más mi progreso ahora?**
2. **¿Qué evidencia concreta lo demuestra?**
3. **¿Estoy corrigiéndola en mis siguientes partidas?**

---

## 3. Principios de producto

### 3.1 Útil antes que impresionante

Una recomendación correcta y ejecutable supera a un dashboard lleno de predicciones. Si la confianza es baja, el producto debe decirlo.

### 3.2 Decisiones, no órdenes

RiftLoop ofrecerá alternativas con condiciones. No jugará por el usuario, no automatizará gameplay y no emitirá instrucciones tácticas que eliminen decisiones durante la partida.

### 3.3 Evidencia o silencio

Cada diagnóstico debe incluir fuente, instante, regla o modelo, nivel de confianza y limitaciones. Si no existe evidencia suficiente, se clasifica como hipótesis o no se muestra.

### 3.4 Una mejora a la vez

El jugador tendrá una misión primaria activa. El sistema puede registrar otros patrones, pero no los convertirá simultáneamente en tareas.

### 3.5 Rendimiento como funcionalidad

La experiencia no es aceptable si reduce de forma perceptible los FPS, genera stutter, consume memoria innecesaria o escribe continuamente al disco.

### 3.6 Privacidad por defecto

La grabación es voluntaria, el micrófono está desactivado por defecto, los clips son locales por defecto y el usuario controla retención, exportación y borrado.

### 3.7 Cumplimiento por diseño

Toda interacción con Riot se clasifica por riesgo. Las capacidades sensibles se pueden apagar remotamente sin actualizar el binario.

### 3.8 Parche y contexto siempre visibles

Toda recomendación muestra parche, rol, rango/segmento, región o conjunto de datos aplicable. Una recomendación stale no se aplica.

### 3.9 Progreso saludable

La racha se conserva también estudiando un clip o completando un puzzle. No se empuja al jugador a seguir haciendo queue después de tilt o fatiga.

---

## 4. Objetivos y no objetivos

### 4.1 Objetivos del producto

- Entregar un primer diagnóstico accionable en menos de 2 minutos después de vincular la cuenta y cargar historial suficiente.
- Mejorar un comportamiento observable en bloques de 3–5 partidas.
- Convertir champion select en una decisión informada sin eliminar la agencia.
- Preparar runas, hechizos e itemización situacional con mínima fricción.
- Fundamentar el post-match con clips propios.
- Mantener el impacto técnico bajo límites medibles.
- Construir una base longitudinal de patrones, intervenciones y resultados.
- Ser aprobable y auditable bajo las políticas de Riot.

### 4.2 Objetivos del usuario

- Elegir un campeón que realmente pueda ejecutar y que encaje en la partida.
- Entrar a la partida con un plan simple.
- Comprar con criterio ante esa composición, no copiar una lista estática.
- Entender una derrota sin recibir veinte números ni culpar a compañeros.
- Saber qué practicar a continuación.
- Ver evidencia de su mejora aunque el LP de corto plazo fluctúe.

### 4.3 No objetivos

- Jugar, apuntar, moverse, lanzar habilidades, comprar o seleccionar automáticamente durante gameplay.
- Leer memoria, inyectarse en procesos, interceptar paquetes o evitar Vanguard.
- Revelar información oculta de jugadores o de la sesión.
- Crear una puntuación pública equivalente a un MMR alternativo.
- Hacer scouting invasivo de jugadores deliberadamente anonimizados.
- Garantizar victorias, LP o una subida de rango.
- Sustituir en V1 a un coach humano para análisis micro de alta precisión.
- Soportar macOS, Linux, consolas o móviles como cliente principal.
- Convertirse en red social en el MVP.
- Mostrar publicidad dentro de propiedades de Riot, champion select, loading o partida.

---

## 5. Usuarios objetivo

### 5.1 Segmentos prioritarios

| Segmento | Necesidad | Dolor actual | Valor de RiftLoop |
|---|---|---|---|
| Ranked comprometido, Bronze–Emerald | Mejorar de forma estructurada | Reportes genéricos, exceso de consejos | Una misión medible con clips propios |
| Jugador con poco tiempo | Aprender en sesiones cortas | No puede revisar VOD completo | Resumen de 3–5 minutos y puzzles |
| Main de 2–4 campeones | Optimizar draft e itemización | Tier lists ignoran dominio personal | Recomendación ponderada por proficiency |
| Jugador que cambia de rol | Evitar errores de preparación | Runas/builds sin contexto de rol | Ajuste por rol asignado y matchup |
| Duo estable | Mejorar coordinación | Estadísticas individuales no explican sincronía | Duo Lab con objetivos mutuos y consentimiento |

### 5.2 Segmentos secundarios

- Creadores que quieren explicar momentos concretos de sus partidas.
- Coaches que necesitan triage y evidencia antes de una sesión.
- Equipos amateur que quieren revisar hábitos básicos sin infraestructura profesional.

### 5.3 Anti-personas

- Usuarios que buscan scripts, automatización de gameplay o ventajas con información oculta.
- Usuarios cuyo único interés es hacer scouting de terceros.
- Jugadores que exigen exactitud causal a partir de una sola partida.
- Equipos profesionales que requieren telemetría, scrim management y análisis táctico multi-POV; se atenderían en otro producto.

### 5.4 Jobs to be done

**Antes de jugar:** “Ayúdame a escoger una opción que domino y que resuelva lo que esta composición necesita.”  
**En loading:** “Haz que recuerde las dos condiciones importantes sin llenarme de texto.”  
**Durante partida:** “Cuando abra la tienda, recuérdame mis ramas de compra sin distraerme.”  
**Después:** “Muéstrame dónde ocurrió el patrón y qué habría sido una decisión mejor.”  
**Entre partidas:** “Dame una práctica concreta y dime si de verdad estoy progresando.”

---

## 6. Modos de uso

### 6.1 Modo Escalar

- Penaliza campeones con poca experiencia reciente.
- Prefiere opciones del champion pool estable.
- Prioriza seguridad, consistencia, rol real y matchups conocidos.
- No recomienda first-time aunque la estadística global sea favorable.
- La misión busca reducir varianza y corregir el cuello de botella actual.

### 6.2 Modo Aprender

- Permite una cola de campeones en aprendizaje.
- Explica dificultad, partidas mínimas recomendadas y riesgos.
- Separa progreso mecánico de resultado.
- Sugiere normals o modos adecuados cuando el riesgo de first-time en ranked es alto.

### 6.3 Modo Sólo análisis

- Cero escrituras al cliente.
- Sin overlay en partida.
- Puede grabar y producir revisión post-match.
- Es el fallback universal si Riot, Vanguard, LCU o la versión del cliente impiden las integraciones.

### 6.4 Modo Privacidad estricta

- Procesamiento local siempre que sea posible.
- Sin subida de video.
- Telemetría opcional desactivada.
- Historial local configurable y borrado en un clic.

---

## 7. Alcance por versión

### 7.1 MVP público

- Onboarding, Riot ID y preferencias.
- Ingesta y análisis de hasta 20 partidas recientes.
- Árbol privado de habilidades.
- Un bloque de mejora de 3–5 partidas.
- Diez detectores post-match explicables.
- Champion select read-only con Top 3.
- Champion Guard sin acción automática.
- Recomendación contextual de runas, hechizos e items.
- Miniquiz de loading con hasta tres preguntas.
- Overlay de objetos de baja distracción.
- Grabación local beta y selección de clips.
- Dashboard post-match con evidencia, confianza y feedback.
- Impacto personalizado de parche.
- Español e inglés.

### 7.2 V1

- Aplicación transparente de runas/hechizos/item sets si Riot lo aprueba.
- Mejor alineación video-evento.
- Modelos calibrados por rol y nivel.
- Misiones adicionales y detección longitudinal.
- Duo Lab con consentimiento mutuo.
- Exportación de clips y resumen para coach.
- Experiencia accesible y soporte ultrawide maduro.

### 7.3 V2

- Visión computacional local para patrones que el timeline no resuelve.
- Puzzles interactivos sobre frames propios.
- Comparación con replays de referencia licenciados o permitidos.
- Coach marketplace o workflow profesional, sujeto a validación.
- Modelos personalizados por estilo y champion mastery.

### 7.4 Fuera de alcance hasta nueva revisión

- Auto-lock sin interacción humana.
- Automatización de acciones dentro de partida.
- Callouts tácticos en tiempo real basados en inferencias dinámicas.
- Voz sintética continua.
- Captura de chat o análisis emocional del texto.
- Perfiles públicos de “malos compañeros”.

---

## 8. Flujo de experiencia de extremo a extremo

~~~mermaid
flowchart TD
    A["Onboarding y consentimiento"] --> B["Historial y perfil"]
    B --> C["Champion select"]
    C --> D["Plan y mini-quiz"]
    D --> E["Overlay mínimo y captura opcional"]
    E --> F["Análisis + clips"]
    F --> G["Misión de 3–5 partidas"]
    G --> H{"¿Cambió el comportamiento?"}
    H -->|Sí| I["Consolidar habilidad"]
    H -->|No| J["Ajustar intervención"]
    I --> C
    J --> C
~~~

### 8.1 Primera sesión

1. El usuario instala un binario firmado.
2. Ve una explicación simple de qué se lee, qué se escribe y qué nunca se hace.
3. Elige modo, roles preferidos, champion pool, tolerancia a recomendaciones nuevas y privacidad.
4. Vincula Riot ID mediante el mecanismo aprobado.
5. RiftLoop analiza el historial disponible.
6. Presenta como máximo:
   - Una fortaleza.
   - Un patrón prioritario.
   - Dos evidencias.
   - Una misión sugerida.
7. El usuario confirma o corrige la hipótesis.

### 8.2 Sesión recurrente

1. El agente liviano detecta estado del cliente.
2. En champion select muestra Top 3, razones y riesgos.
3. Tras lock, prepara runas, hechizos y árbol de items.
4. Loading muestra hasta tres preguntas relevantes.
5. Al entrar en juego, se cierra el minijuego y sólo queda el overlay mínimo.
6. La captura opt-in se ejecuta en proceso separado.
7. Al terminar, el analizador procesa timeline y video con baja prioridad.
8. El usuario recibe un resumen de 3–5 minutos.
9. Se actualiza la misión y la racha de mejora.

---

## 9. Requisitos funcionales

### 9.1 Cuenta, onboarding y consentimiento

#### RF-ONB-001 — Transparencia inicial

Antes de habilitar integraciones, la aplicación mostrará una matriz comprensible:

| Categoría | Ejemplo | Default |
|---|---|---|
| Lectura local | Estado del cliente, draft, partida visible | Activada |
| Escritura local | Página de runas o hechizos | Desactivada hasta consentimiento y aprobación |
| Grabación | Ventana de League | Desactivada |
| Audio del juego | Sonido de la aplicación | Desactivado |
| Micrófono | Voz del usuario | Siempre desactivado por defecto |
| Cloud | Métricas y modelos | Mínimo necesario |
| Telemetría de producto | Crashes, rendimiento, uso agregado | Opt-in según jurisdicción |

#### RF-ONB-002 — Perfil inicial

Campos:

- Riot ID y región/plataforma.
- Roles preferidos ordenados.
- Rol secundario y autofill aceptable.
- Champion pool por rol: main, cómodo, aprendiendo, no recomendar.
- Modo Escalar o Aprender.
- Rango y objetivo declarados.
- Preferencia de estilo sólo como input secundario.
- Hotkey de Champion Guard.
- Grabación, audio y retención.
- Idioma, tamaño de interfaz, alto contraste y daltonismo.

#### RF-ONB-003 — Cold start

Si hay menos de 10 partidas útiles:

- Mostrar baja confianza.
- Preguntar experiencia manual por campeón.
- Usar meta como prior, nunca como certeza personal.
- No emitir conclusiones fuertes sobre hábitos.
- Crear una misión de observación antes de una misión correctiva.

#### RF-ONB-004 — Desvinculación y borrado

El usuario podrá desvincular cuenta, borrar videos, borrar análisis, exportar datos y solicitar eliminación cloud desde Settings.

### 9.2 Perfil de jugador

El perfil mantendrá:

- Partidas por rol y campeón, con decaimiento temporal.
- Rendimiento contextual, sin reducirlo a win rate.
- Campeones recientes, abandonados y en aprendizaje.
- Historial de misiones e intervenciones.
- Habilidades estimadas y confianza.
- Preferencias explícitas separadas de inferencias.
- Cambios relevantes por parche.

Regla: una inferencia nunca reemplaza silenciosamente una preferencia explícita.

### 9.3 Detección de estado

Estados mínimos:

- Sin cliente.
- Cliente abierto.
- Lobby.
- Queue.
- Ready check.
- Champion select.
- Loading.
- In game.
- Post-game.
- Procesando.

Requisitos:

- Transiciones idempotentes.
- Recuperación tras reinicio del cliente.
- Timeout y fallback si LCU desaparece.
- Cero crash del juego si RiftLoop falla.
- Todos los módulos responden a una señal global de “Game Active”.

### 9.4 Champion select

#### RF-CS-001 — Inputs

Cuando sean legítimamente accesibles:

- Rol asignado y posición preferida.
- Pick order.
- Picks, hovers y bans visibles.
- Campeones poseídos/disponibles.
- Champion pool del usuario.
- Proficiency personal y recencia.
- Datos meta por parche, región, nivel y rol.
- Sinergia con aliados.
- Matchup probable.
- Rasgos de composición:
  - Mezcla de daño.
  - Frontline.
  - Engage y disengage.
  - Peel.
  - Control de masas.
  - Rango.
  - Waveclear.
  - Prioridad temprana.
  - Escalado.
  - Daño sostenido y burst.

No se usarán identidades o historiales que el producto no deba revelar.

#### RF-CS-002 — Salida Top 3

La interfaz presenta:

1. **Mejor opción personal:** balance óptimo entre dominio y encaje.
2. **Opción segura:** baja varianza y matchup ejecutable.
3. **Opción estratégica:** cubre una carencia de composición con mayor riesgo.

Cada tarjeta contiene:

- Nombre, rol y estado de disponibilidad.
- Tres razones como máximo.
- Principal riesgo.
- Nivel de experiencia del usuario.
- Confianza: alta, media o baja.
- Qué cambió desde la recomendación anterior.

No se mostrará un decimal de “probabilidad de ganar” si no está calibrado.

#### RF-CS-003 — Modo Escalar

- Excluye “no recomendar”.
- Penaliza poca experiencia y falta de partidas recientes.
- No recomienda first-time en ranked.
- Puede concluir que mantener el pick cómodo es mejor que resolver perfectamente la comp.

#### RF-CS-004 — Modo Aprender

- Permite un campeón marcado “aprendiendo”.
- Muestra dificultad, riesgo y objetivo didáctico.
- No presenta una opción experimental como “mejor pick” sin advertencia.

#### RF-CS-005 — Actualización estable

- Recalcular sólo ante evento de draft relevante.
- Debounce mínimo de 250 ms.
- Congelar presentación brevemente para evitar que las tarjetas salten.
- Mantener siempre una explicación del cambio.

#### RF-CS-006 — Champion Guard

Comportamiento permitido para MVP:

- El usuario configura hasta tres respaldos por rol.
- Alertas discretas a 10, 5 y 3 segundos.
- A 3 segundos resalta el mejor respaldo disponible.
- Una pulsación física del hotkey o un clic confirma la acción.
- Una confirmación produce como máximo una solicitud al cliente.
- Sin confirmación, no se realiza ninguna selección.
- Si rol, disponibilidad o estado son inciertos, sólo se alerta.
- La acción queda registrada localmente para auditoría.

#### RF-CS-007 — Auto-lock a 1 segundo

**Estado: Approval-Gated / No-Ship.**

Especificación deseada, únicamente para someter a Riot:

- Si queda 1 segundo, no hay campeón seleccionado, existe un respaldo válido y el usuario habilitó expresamente la función, se seleccionaría el candidato preconfigurado.
- Nunca elegiría un campeón fuera de propiedad, rol o pool permitido.
- Tendría kill switch remoto y log auditable.

Criterio de salida de No-Ship:

- Aprobación escrita de Riot que describa exactamente endpoint, acción, temporización y UX permitida.
- Revisión legal y de seguridad.
- Test en PBE y producción controlada.
- Confirmación de que no se considera scripting o programa de automatización.

Hasta cumplir todos los criterios, el código de producción no ejecutará esta acción.

### 9.5 Sistema de runas

#### RF-RUN-001 — Generación

Después de que el pick sea estable, generar:

- Página principal.
- Una alternativa situacional.
- Explicación de hasta tres decisiones no obvias.

Inputs:

- Campeón y rol real.
- Oponente de línea probable.
- Composición enemiga completa.
- Tipo y duración esperada del daño.
- Cantidad de CC relevante.
- Necesidad de sustain, tenacidad, escalado o tempo.
- Composición aliada.
- Preferencia y estilo observado del usuario.
- Parche y dataset aplicable.

#### RF-RUN-002 — Consistencia

- Validar que todos los IDs existen en el parche instalado.
- No mezclar datos de parches incompatibles.
- Si el matchup es incierto, elegir una página robusta y explicar la incertidumbre.
- Una página alternativa debe cambiar por una razón estratégica real, no para aparentar variedad.

#### RF-RUN-003 — Aplicación automática

**Estado inicial: Approval-Gated / feature flag desactivada.**

Si se aprueba:

- Administrar únicamente una página identificada como propiedad de RiftLoop.
- Mostrar diff antes o inmediatamente después de aplicar.
- Incluir Undo de un clic.
- No tocar páginas personales.
- Si el usuario edita manualmente después, no volver a sobrescribir.
- Reaplicar sólo por cambio explícito de campeón/rol o confirmación del usuario.
- Apagarse si la versión del cliente o el parche no coinciden.

### 9.6 Hechizos de invocador

#### RF-SUM-001 — Prioridad de decisión

1. Rol asignado real.
2. Restricciones duras del campeón o posición.
3. Matchup y composición.
4. Preferencia explícita del usuario.
5. Meta contextual.

Reglas:

- Jungla exige Smite cuando corresponda.
- Si el rol no es confiable, sólo recomendar.
- Cleanse, Exhaust, Barrier, Ignite, Ghost y Teleport deben justificarse por amenaza o plan.
- No cambiar hechizos después de una edición manual sin nueva confirmación.

#### RF-SUM-002 — Escritura

Comparte las mismas condiciones de aprobación, auditoría, diff, undo, feature flag y respeto a edición manual que runas.

### 9.7 Motor de itemización

#### RF-ITEM-001 — Árbol de decisión

El resultado no será una lista lineal única. Tendrá:

- Objeto inicial.
- Primera vuelta por umbrales de oro.
- Core 1 y Core 2.
- Opciones de botas.
- Rama contra curación.
- Rama contra escudos.
- Rama contra armadura o MR.
- Rama contra burst físico o mágico.
- Rama contra CC.
- Rama de DPS sostenido.
- Rama de snowball.
- Rama cuando se juega desde atrás.
- Rama de split-push o teamfight cuando aplique.

Cada rama responde “elige esto si…”.

#### RF-ITEM-002 — Inputs

- Campeón, rol y runas.
- Daño enemigo ponderado por amenazas reales.
- CC y duración.
- Curación y escudos relevantes.
- Frontline y resistencias.
- Alcance y patrón de pelea.
- Oro actual y slots.
- Estado ahead/even/behind, sólo si puede inferirse de datos visibles.
- Objetivo de la misión.

#### RF-ITEM-003 — Sesgo estadístico

El sistema no ordenará builds sólo por win rate de objetos completados. Debe corregir o advertir:

- Survivorship bias.
- Win-state bias.
- Momento de compra.
- Tamaño de muestra.
- Selección por matchup.
- Diferencias de elo, rol y parche.

Para cada recomendación se conserva:

- Dataset.
- Criterio de inclusión.
- Muestra.
- Intervalo o banda de confianza.
- Reglas expertas aplicadas.

#### RF-ITEM-004 — Organización para compra

Antes de partida se crea un plan de compra:

| Momento | Salida |
|---|---|
| Inicio | Compra inicial y excepción |
| Primera vuelta | Umbrales exactos y componente prioritario |
| Core | Dos objetos y condición de desviación |
| Botas | Tipo y amenaza que lo activa |
| Adaptación | Hasta tres ramas contra la comp enemiga |
| Slot final | Opciones por condición de victoria |

El usuario puede fijar una rama manualmente.

### 9.8 Miniquiz de loading

#### RF-QUIZ-001 — Ventana

- Ventana nativa independiente, nunca inyectada.
- No suplanta ni modifica la pantalla de carga.
- Se puede mover, minimizar o desactivar.
- Se cierra automáticamente cuando empieza la partida.
- Máximo tres preguntas, 10–15 segundos por pregunta.

#### RF-QUIZ-002 — Contenido

Preguntas basadas sólo en información legítima de la composición bloqueada:

- ¿Cuál es la principal fuente de daño enemiga?
- ¿Qué habilidad de CC debe respetar tu campeón?
- ¿Quién tiene ventaja de nivel 2 o 3 en el matchup?
- ¿Qué condición activa botas defensivas?
- ¿Tu equipo quiere engage, peel o poke?
- ¿Cuál es el primer power spike relevante?
- ¿Qué objetivo cumple tu campeón en teamfight?

Puede incluir preguntas sobre aliados, enemigos, matchup directo y plan de items; no incluirá historial oculto ni información privada de otros jugadores.

#### RF-QUIZ-003 — Adaptación

- Prioriza errores previos del usuario relacionados con esa partida.
- No repite una pregunta dominada salvo refresco espaciado.
- Registra precisión, tiempo y confianza.
- Una respuesta incorrecta muestra una explicación de una frase.
- No penaliza la racha por cerrar el quiz.

#### RF-QUIZ-004 — Recompensa

- XP privada por participación y aprendizaje.
- Bonus por explicar correctamente el “por qué”.
- Sin recompensas que requieran hacer queue de inmediato.
- Sin anuncios.

### 9.9 Overlay de objetos

#### RF-OVR-001 — Diseño

Overlay externo, transparente y click-through por defecto. Contenido máximo:

- Próximo componente recomendado.
- Hasta dos alternativas.
- Una condición corta bajo cada alternativa.
- Hotkey para expandir durante un momento seguro.

Ejemplo conceptual:

| Opción | Condición |
|---|---|
| Mercury's Treads | Si el CC/magia sigue siendo la amenaza |
| Plated Steelcaps | Si domina daño físico de autoataques |
| Bota ofensiva | Si vas ahead y no te alcanzan |

#### RF-OVR-002 — Momentos de aparición

- Visible al abrir tienda o mediante hotkey cuando la integración permitida lo detecte.
- Se oculta durante combate.
- No cubre minimapa, HUD crítico, chat ni cooldowns.
- Se recuerda la posición por resolución y monitor.

#### RF-OVR-003 — Límites

- No muestra rutas, “ve a objetivo”, cooldowns enemigos inferidos ni información oculta.
- No compra objetos.
- No emite alertas sonoras repetitivas.
- No presenta más de tres decisiones.
- Si los datos en vivo no son confiables, conserva el plan pregame.

### 9.10 Grabación local

#### RF-REC-001 — Consentimiento y fuentes

- Opt-in explícito.
- Fuente por defecto: sólo ventana de League.
- Micrófono desactivado.
- Audio del juego como opt-in separado.
- Indicador visible de grabación.
- Pausa y borrado inmediato accesibles.

#### RF-REC-002 — Perfil por defecto

- 1280 × 720.
- 30 FPS.
- H.264 por hardware cuando esté disponible.
- Bitrate objetivo aproximado: 2.5 Mbps.
- Consumo aproximado: 1.1 GB por hora antes de recorte.
- Sin webcam.

Los valores se adaptan si el hardware no sostiene el presupuesto.

#### RF-REC-003 — Segmentación

- Grabar en segmentos recuperables cortos.
- Sincronizar reloj de video, reloj de partida y eventos.
- Mantener un buffer previo a cada evento.
- Clips sugeridos: 10–20 segundos antes y después.
- Tras analizar, conservar sólo clips seleccionados.
- Borrar el resto salvo “Conservar partida completa”.
- Límite temporal por defecto: 2 GB.

#### RF-REC-004 — Degradación elegante

- Si no existe encoder de hardware, ofrecer perfil más bajo o desactivar.
- Si se supera el presupuesto de frame time, bajar FPS/bitrate antes de afectar el juego.
- Si la captura falla, continuar con timeline y visualizaciones sin video.
- Nunca reintentar en bucle agresivo dentro de partida.

### 9.11 Análisis post-match

#### RF-POST-001 — Resumen principal

El primer viewport contiene:

1. Una fortaleza observada.
2. Un problema prioritario.
3. Dos o tres evidencias.
4. Una alternativa o contrafactual.
5. La misión siguiente.
6. Confianza y limitaciones.

#### RF-POST-002 — Evidencia

Cada tarjeta incluye:

- Timestamp de juego.
- Clip o visualización de timeline.
- Evento relacionado.
- Hechos observados.
- Inferencia separada.
- Por qué importa.
- Qué probar la próxima vez.
- Confianza.

#### RF-POST-003 — Feedback

El usuario puede marcar:

- Correcto.
- Parcialmente correcto.
- Incorrecto.
- Falta contexto.

Y opcionalmente indicar el motivo. Ese feedback:

- No reentrena automáticamente modelos globales.
- Se usa primero para calibración y revisión.
- Nunca se interpreta como admisión de culpa.

#### RF-POST-004 — Priorización longitudinal

- Comparar con hasta 20 partidas recientes compatibles.
- Un patrón recurrente supera a una anomalía de una partida.
- Considerar rol, campeón, duración, parche y estado de partida.
- Separar frecuencia, severidad, confianza y controlabilidad.
- No priorizar algo que el usuario no puede practicar en su champion pool actual.

#### RF-POST-005 — Sin blame

- No etiquetar compañeros como causa de la derrota.
- No generar rankings públicos de desempeño individual.
- Describir sólo decisiones controlables por el usuario.
- Evitar lenguaje absoluto como “perdiste por”.

### 9.12 Detectores iniciales

| ID | Detector | Evidencia mínima | Salida posible |
|---|---|---|---|
| D01 | Muertes tempranas evitables | Timeline + contexto de oro/nivel | Misión de ventanas seguras |
| D02 | Muerte antes de objetivo | Muerte dentro de ventana definida | Preparación 60–90 s antes |
| D03 | Oro sin gastar antes de pelea/muerte | Oro estimado + eventos | Umbral de recall |
| D04 | Recall/compra ineficiente | Timing, oro y regreso | Plan de primera vuelta |
| D05 | Kill sin conversión | Kill seguida sin placa/objetivo/recall válido | Checklist de conversión |
| D06 | Caída de CS post-línea | Serie temporal normalizada | Ruta de ingreso segura |
| D07 | Visión tardía de objetivo | Wards y ventana preobjetivo | Timing de preparación |
| D08 | Pelea con desventaja numérica | Posiciones/eventos disponibles + confianza | Regla de conteo |
| D09 | Adaptación tardía de items | Amenaza presente + compra | Árbol antiamenaza |
| D10 | Mala conversión de ventaja | Lead y eventos posteriores | Misión de tempo |

Reglas transversales:

- Un detector no equivale a causalidad.
- Wave state, spacing e intención no siempre se derivan del Match Timeline.
- Cuando falta contexto visual, bajar confianza o solicitar confirmación del clip.
- Umbrales se versionan por parche, rol y nivel.

### 9.13 Puzzles personales

Un puzzle usa un momento real y plantea una decisión:

- Freeze frame o mini timeline.
- Contexto observable.
- 2–4 opciones.
- Respuesta recomendada con explicación.
- Qué dato cambiaría la respuesta.
- Repetición espaciada si el concepto no se consolida.

No se afirma que exista una única jugada perfecta si el contexto admite varias.

### 9.14 Misiones y bloques de mejora

Cada misión tiene:

- Nombre conductual.
- Hipótesis.
- Métrica observable.
- Contexto donde aplica.
- Contexto donde no aplica.
- Duración de 3–5 partidas.
- Objetivo base y stretch.
- Evidencia final.

Ejemplo:

**Misión:** llegar vivo a la preparación del primer dragón.  
**Métrica:** no morir entre 90 y 20 segundos antes del objetivo cuando la pelea es disputable.  
**No aplica:** objetivo cedido deliberadamente o jugada cross-map de mayor valor.  
**Verificación:** 4 oportunidades válidas en 5 partidas; 3 ejecutadas.

### 9.15 Árbol privado de habilidades

Dominios iniciales:

- Laning.
- Wave management.
- Recalls y economía.
- Visión.
- Objetivos.
- Conversión de ventajas.
- Posicionamiento.
- Teamfights.
- Juego desde atrás.
- Itemización.
- Draft y preparación.
- Mental y control de sesión, sin diagnóstico médico.

Estados:

- No evaluado.
- Introducido.
- Practicando.
- Consistente.
- Dominado.
- Necesita refresco.

El estado muestra confianza y número de oportunidades observadas.

### 9.16 Gamificación saludable

#### Racha de mejora

La racha diaria se mantiene realizando una de estas acciones:

- Completar un puzzle.
- Revisar una evidencia.
- Jugar una partida con misión activa.
- Registrar una reflexión breve.

No exige ganar ni jugar ranked. Incluye:

- Un freeze semanal configurable.
- Recuperación mediante aprendizaje, no mediante pago.
- Zona horaria local y prevención de cambios abusivos.
- Recordatorios opt-in.

#### XP

Se otorga por:

- Completar el ciclo.
- Dar feedback útil.
- Aplicar una conducta en una oportunidad válida.
- Consolidar una habilidad.

No se otorga principalmente por cantidad de partidas, kills o victorias.

#### Protección contra tilt

- Tras tres derrotas consecutivas, sugerir una pausa.
- La pausa preserva la racha.
- Ocultar CTA agresivos de “jugar otra”.
- Permitir modo revisión sin queue.

### 9.17 Impacto personalizado del parche

Para cada parche:

- Identificar campeones del pool afectados.
- Separar cambio directo, cambio de objetos/runas y cambio de matchups.
- Estimar impacto con confianza.
- Proponer qué probar primero.
- No recomendar abandonar un main sólo por el win rate de las primeras horas.
- Esperar muestra mínima o marcar “datos tempranos”.

### 9.18 Duo Lab

Condiciones:

- Consentimiento mutuo y revocable.
- Analizar sólo partidas jugadas juntos.
- Datos privados, no públicos.

Métricas y misiones:

- Sincronización de recalls.
- Ventanas de nivel 2/3.
- Muertes emparejadas y follow-up.
- Conversión de prioridad en objetivo.
- Timing de rotaciones.
- Cobertura de visión.
- Sinergias del champion pool.

No se inferirá quién “tuvo la culpa”. La unidad de análisis es la coordinación.

### 9.19 Notificaciones

- Fuera de partida solamente, salvo alertas de draft aprobadas.
- Recordatorios de parche, misión y análisis listos.
- Frecuencia configurable.
- Sin FOMO artificial.
- Ninguna notificación del sistema operativo durante combate.

### 9.20 Configuración y controles

El usuario podrá:

- Desactivar cada módulo por separado.
- Elegir modo read-only.
- Cambiar hotkeys.
- Mover y escalar overlay.
- Limitar FPS/bitrate de captura.
- Configurar retención y ubicación.
- Borrar caché.
- Ver versión de datos/parche/modelo.
- Consultar un log de acciones al cliente.
- Ejecutar diagnóstico de rendimiento.

---

## 10. Reglas de experiencia y contenido

### 10.1 Jerarquía de atención

1. Seguridad y estado del producto.
2. Decisión inmediata requerida por el usuario.
3. Misión activa.
4. Contexto secundario.
5. Estadísticas exploratorias.

### 10.2 Presupuesto cognitivo

| Fase | Máximo recomendado |
|---|---|
| Champion select | 3 opciones, 3 razones cada una |
| Loading | 3 preguntas |
| In-game | 3 alternativas de compra, una línea cada una |
| Post-match inicial | 1 fortaleza, 1 problema, 3 evidencias |
| Bloque de mejora | 1 misión primaria |

### 10.3 Voz

- Directa, específica y no acusatoria.
- Hablar de decisiones y oportunidades.
- Distinguir hechos de inferencias.
- Evitar “siempre”, “nunca” y certeza falsa.
- Usar lenguaje de League conocido por el nivel del usuario.

### 10.4 Ejemplo de diagnóstico

**Evitar:** “Mala macro; perdiste por no rotar.”  
**Usar:** “En 3 de 5 oportunidades llegaste al objetivo con menos de 20 s y sin compra reciente. En este clip tenías 1.280 de oro al minuto 18:42. Confianza media: el timeline no confirma si el objetivo se había cedido. Próxima misión: decidir recall o trade cross-map 75 s antes.”

---

## 11. Modelo de recomendación de campeón

### 11.1 Variables conceptuales

La puntuación inicial de un candidato c se compone de:

- Dominio personal.
- Recencia.
- Adecuación al rol.
- Matchup.
- Sinergia aliada.
- Encaje contra composición enemiga.
- Aporte a la composición propia.
- Meta contextual.
- Dificultad y varianza.
- Incertidumbre.

Representación conceptual:

**Score(c) = proficiency + role fit + matchup + ally synergy + enemy fit + comp completion + meta − novelty − uncertainty**

La implementación final debe usar probabilidades calibradas o rankings aprendidos cuando exista suficiente dato; no debe presentar una suma arbitraria como verdad científica.

### 11.2 Proficiency personal

No se reducirá a win rate. Incluirá:

- Número de partidas efectivo con decaimiento.
- Frecuencia reciente.
- Métricas por rol normalizadas.
- Estabilidad entre partidas.
- Familiaridad con matchups.
- Declaración manual del usuario.

### 11.3 Representación de composición

Vector de rasgos:

- Daño físico, mágico y verdadero.
- Burst y DPS.
- Front-to-back.
- Engage, counter-engage y disengage.
- Peel.
- Movilidad.
- Alcance.
- CC confiable.
- Waveclear.
- Side lane.
- Objetivos.
- Early, mid y late power.

### 11.4 Incertidumbre

Fuentes:

- Pick rival no confirmado.
- Flex picks.
- Muestra pequeña.
- Parche reciente.
- Rol atípico.
- Campeón reworkeado.
- Datos personales antiguos.

La UI presenta banda alta/media/baja y el principal motivo de incertidumbre.

### 11.5 Evaluación offline

- NDCG o ranking quality contra decisiones de expertos.
- Calibration error para probabilidades.
- Top-3 coverage.
- Tasa de recomendaciones no poseídas: objetivo 0.
- Tasa de first-time indebido en Modo Escalar: objetivo 0.
- Evaluación ciega por coaches.
- A/B de explicación, no sólo de pick.

---

## 12. Modelo de intervención y aprendizaje

### 12.1 Priorización de patrones

**Prioridad = recurrencia × severidad × controlabilidad × confianza × adecuación al objetivo**

Se aplican límites:

- No priorizar un patrón sin suficientes oportunidades.
- No confundir correlación con causa de derrota.
- No recomendar dos misiones que compitan.
- Dar más peso a conducta reciente en el mismo rol.

### 12.2 Opportunity-based metrics

El denominador debe ser una oportunidad válida, no la cantidad total de partidas. Ejemplo:

- Correcto: recalls oportunos / ventanas de recall identificadas.
- Incorrecto: recalls buenos / partidas.

Cada detector debe definir:

- Evento de oportunidad.
- Condiciones de exclusión.
- Conducta esperada.
- Ventana temporal.
- Confianza.

### 12.3 Verificación

Al cerrar el bloque:

- Mejoró de forma consistente.
- Mejoró parcialmente.
- Sin cambio observable.
- No hubo oportunidades suficientes.
- Detector probablemente incorrecto.

La misión se consolida, adapta, prolonga o descarta según ese resultado.

### 12.4 Uso de modelos generativos

Permitido:

- Convertir hechos estructurados en explicación natural.
- Resumir evidencia.
- Variar preguntas educativas.
- Traducir y adaptar tono.

No permitido sin verificación:

- Inventar timestamps.
- Inferir intención.
- Emitir causalidad.
- Recomendar un item inexistente en el parche.
- Sustituir el motor determinista de legalidad.

Toda salida generativa pasa por esquema validado y grounding estructurado.

---

## 13. Requisitos de datos

### 13.1 Fuentes

| Fuente | Uso | Naturaleza |
|---|---|---|
| Riot Account / Match / Timeline APIs | Identidad aprobada, historial y eventos | Oficial |
| Data Dragon u origen estático oficial | Campeones, runas, items, versiones | Oficial |
| League Client API | Estado local y acciones limitadas | No soportada oficialmente; requiere registro y resiliencia |
| Live Client Data API | Datos visibles de partida | Local, documentada pero sin SLA |
| Replay API | Control/captura de replay cuando aplique | Local |
| Windows.Graphics.Capture | Video de la ventana | Sistema operativo |
| Preferencias del usuario | Rol, pool, privacidad | Primera parte |
| Feedback de coaching | Calibración | Primera parte |

### 13.2 Versionado

Cada observación y recomendación guarda:

- gameVersion.
- patch family.
- data snapshot.
- ruleset version.
- model version.
- client integration version.
- timestamp UTC.

No se comparan directamente partidas incompatibles sin normalización.

### 13.3 Calidad de datos

Validaciones:

- IDs conocidos.
- Rol coherente.
- Timeline completo.
- Duración válida.
- Remake excluido o etiquetado.
- Arena/modos alternativos separados.
- Partida custom separada.
- Duplicados idempotentes.
- Parche reconocible.

### 13.4 Retención propuesta

| Dato | Default | Control |
|---|---|---|
| Video completo temporal | Hasta procesar; máximo 2 GB | Configurable |
| Clips seleccionados | 30 días local | 0/7/30/90/ilimitado |
| Match IDs y features | 12 meses | Borrado/exportación |
| Preferencias | Mientras exista cuenta | Editable |
| Logs técnicos | 14 días, minimizados | Opt-out donde aplique |
| Acciones LCU | 30 días local | Visible/borrable |

Las cifras deben validarse legalmente por región antes de lanzamiento.

---

## 14. Arquitectura nativa de Windows

### 14.1 Decisión tecnológica

**Cliente:** C++20, MSVC, CMake, C++/WinRT donde corresponda.  
**UI de escritorio:** Windows App SDK + WinUI 3.  
**Procesos de baja latencia:** Win32 nativo.  
**Gráficos:** Direct2D, DirectWrite y DirectComposition sobre D3D11.  
**Captura:** Windows.Graphics.Capture.  
**Codificación:** Media Foundation con encoder H.264 por hardware.  
**IPC:** named pipes con mensajes binarios versionados.  
**Persistencia:** SQLite cifrado o almacenamiento local protegido según threat model.  
**Secretos:** DPAPI / Windows Credential Manager.  
**Backend recomendado:** Rust para servicios de datos/modelos o C++ cuando exista razón; ningún componente del cliente depende de JS.

### 14.2 Topología de procesos

~~~mermaid
flowchart TD
    D["RiftLoop.Desktop.exe"] <--> A["RiftLoop.Agent.exe"]
    A <--> O["RiftLoop.Overlay.exe"]
    A <--> C["RiftLoop.Capture.exe"]
    A <--> P["RiftLoop.Analyzer.exe"]
    A <--> U["RiftLoop.Update.exe"]
~~~

#### RiftLoop.Desktop.exe

- Dashboard, onboarding, settings, post-match y árbol de habilidades.
- C++/WinRT + WinUI 3.
- Se suspende o reduce working set durante partida.
- No ejecuta animaciones en background.

#### RiftLoop.Agent.exe

- Proceso Win32 liviano y persistente.
- Máquina de estados.
- Comunicación con APIs locales aprobadas.
- Feature flags, consentimientos y health.
- Orquesta procesos sin contener UI pesada.

#### RiftLoop.Overlay.exe

- Ventana transparente externa.
- Direct2D/DirectWrite/DirectComposition.
- Click-through salvo modo configuración.
- Render event-driven.
- Sin inyección, hooks gráficos ni lectura de memoria.

#### RiftLoop.Capture.exe

- Sólo existe si grabación está activa.
- WGC + D3D11 + Media Foundation.
- Encoder de hardware preferido.
- Segmentos recuperables y control de cuota.
- Muere limpiamente si juego termina.

#### RiftLoop.Analyzer.exe

- Sólo post-match.
- Prioridad baja.
- Pausa inmediata si empieza otra partida.
- Ejecuta extracción, reglas, sincronización y clips.

#### RiftLoop.Update.exe

- Binario mínimo firmado.
- Actualización atómica, verificación de firma y rollback.
- Nunca actualiza mientras hay partida.

### 14.3 IPC

- Named pipes con ACL por usuario.
- Handshake de versión.
- Límite de tamaño por mensaje.
- Serialización binaria simple y documentada.
- Timeouts; ningún proceso espera indefinidamente.
- El overlay recibe un ViewModel mínimo, no bases completas.
- Capture no recibe tokens de Riot.

### 14.4 Máquina de estados

| Estado | Desktop | Overlay | Capture | Analyzer |
|---|---|---|---|---|
| Idle | Disponible | Off | Off | Puede trabajar |
| Queue | Minimizable | Off | Off | Puede trabajar |
| Champion select | UI compacta | Off | Off | Pausado |
| Loading | Quiz | Off | Preparación opcional | Pausado |
| In game | Suspendido/trim | Eventual | On si opt-in | Pausado |
| Post-game | Activo | Off | Finaliza | On baja prioridad |

### 14.5 Prohibiciones técnicas

- DLL injection.
- Lectura o escritura de memoria del juego.
- Kernel drivers.
- Hooks de DirectX en el proceso del juego.
- Packet sniffing.
- Simulación de mouse/teclado para gameplay.
- Low-level keyboard hooks para registrar actividad.
- OCR continuo cuando una API legítima resuelve el dato.
- Servicios siempre activos con privilegios de administrador.

Para hotkeys se usa RegisterHotKey o mecanismo equivalente de alcance limitado.

### 14.6 Backend conceptual

Servicios:

- Identity mapping y consentimiento.
- Match ingest.
- Data snapshots por parche.
- Feature computation.
- Recommendation/ranking.
- Mission engine.
- Model registry.
- Experiment assignment.
- Entitlements.
- Audit y feature flags.

Principios:

- API key de Riot sólo en servidor.
- Idempotencia por match ID.
- Rate limiting y backoff.
- Caché de estáticos.
- Separación de PII, gameplay y telemetría.
- Región de datos según necesidad legal.
- No subir video por defecto.

---

## 15. Presupuestos de rendimiento

Estos valores son **SLO iniciales y release gates que deben medirse**, no promesas sin benchmark.

### 15.1 Máquina base de validación

- CPU equivalente a Intel i5-8250U / Ryzen 3 1200.
- 8 GB RAM.
- SSD.
- Intel UHD 630 o GPU equivalente; además GTX 1050-class para perfil recomendado.
- Windows 10 22H2 y Windows 11 soportado.
- League en calidad competitiva común.

### 15.2 Sin grabación, durante partida

| Métrica total RiftLoop | Gate |
|---|---|
| Private working set p95 | ≤ 80 MB |
| CPU promedio | ≤ 0.75 % |
| CPU p95 | ≤ 2 % |
| GPU 3D promedio | ≤ 0.75 % |
| Red estable | ≤ 5 KB/s |
| Escrituras de disco | Aproximadamente 0, salvo log crítico acotado |
| Degradación FPS mediana | ≤ 2 % |
| Degradación 1 % low | ≤ 3 % |
| Impacto de frame-time p95 | ≤ 0.5 ms |

### 15.3 Con grabación

| Métrica total RiftLoop | Gate |
|---|---|
| Private working set p95 | ≤ 200 MB |
| CPU promedio | ≤ 3 % |
| CPU p95 | ≤ 6 % |
| GPU 3D promedio | ≤ 2 % |
| Uso de video encode | ≤ 10 % como objetivo de perfil |
| Escritura default | ≤ 4 Mbps |
| Degradación FPS mediana | ≤ 3 % |
| Degradación 1 % low | ≤ 5 % |

Si no se cumplen, la captura baja calidad o se desactiva; nunca degrada silenciosamente el juego.

### 15.4 Reglas de ahorro

- Overlay estático: render sólo al invalidar; heartbeat máximo de 1 FPS.
- Animaciones: máximo 30 FPS y duración breve.
- Nada ligado a refresco de 144/240 Hz.
- Desktop suspendido o con working set recortado.
- Network batching fuera de combate.
- Flush de grabación secuencial y acotado.
- Analyzer detenido mientras Game Active.
- Inferencia pesada post-match o cloud.
- Ningún polling a menos de 250 ms salvo necesidad aprobada; preferir eventos.

### 15.5 Otros SLO

- Inicio del agente: < 500 ms p95.
- Desktop usable: < 2 s p95 en SSD.
- Overlay visible tras evento válido: < 500 ms p95.
- Cierre total al solicitarlo: < 2 s.
- Recuperación tras crash de módulo: sin afectar League.
- Crash-free sessions del cliente: ≥ 99.8 %.

### 15.6 Telemetría de rendimiento

Opt-in o minimizada según región:

- Working set por proceso.
- CPU/GPU por fase.
- Dropped frames de captura.
- Encoder usado.
- Resolución/DPI.
- Tiempo de análisis.
- Crashes con dump redactado.

No se registran teclas, contenido de chat ni video.

---

## 16. Privacidad y seguridad

### 16.1 Threat model

Activos:

- Tokens/credenciales.
- Riot ID y match history.
- Preferencias y perfil.
- Video y audio.
- Feedback.
- Configuración de integraciones.

Amenazas:

- Robo de token.
- Lectura de clips por otro usuario local.
- Supply-chain/update compromise.
- Exposición accidental de nombres en clips.
- Subida cloud no consentida.
- Endpoint local abusado por otro proceso.
- Logs con PII.

### 16.2 Controles

- Binarios y actualizaciones firmados.
- TLS moderno y certificate validation.
- API keys sólo server-side.
- DPAPI/Credential Manager para secretos locales.
- ACL por usuario en pipes, DB y clips.
- Nonces y autenticación entre procesos cuando aplique.
- Updates atómicos con rollback.
- SBOM y escaneo de dependencias.
- Least privilege; sin administrador para operación normal.
- Sanitización de nombres de archivo y payloads.
- Cotas de memoria, mensaje y archivo.
- Redacción de dumps.

### 16.3 Privacidad de video

- Local-first.
- Indicador claro de captura.
- Vista previa de cada clip antes de compartir.
- Opción de ocultar Riot IDs/chat en exportación cuando técnicamente viable.
- URL de subida expira.
- Nada se usa para entrenamiento global sin consentimiento separado.
- Borrado del temporal después de producir clips.

### 16.4 Derechos del usuario

- Acceso.
- Exportación.
- Rectificación de preferencias.
- Eliminación.
- Revocación de consentimiento.
- Desvinculación.
- Explicación de recomendación y versión.

### 16.5 Menores y regiones

Antes de beta pública:

- Revisión de edad mínima.
- GDPR/UK GDPR, CCPA/CPRA y normativa relevante.
- Data Processing Agreements.
- Política de privacidad en lenguaje claro.
- Definición de bases legales y plazos reales.
- Flujo de parental consent si fuera necesario.

---

## 17. Cumplimiento Riot y Vanguard

### 17.1 Principios

- Registrar el producto y sus usos.
- Mantener una free tier si se monetiza, de acuerdo con la política aplicable.
- Usar RSO/Production Key donde corresponda.
- No exponer claves Riot en cliente.
- Declarar endpoints de League Client API y propósito.
- No depender de LCU como si tuviera SLA.
- No eliminar decisiones del jugador.
- No mostrar información session-specific desconocida.
- No analizar jugadores deliberadamente ocultos.
- Incorporar disclaimer y marcas según guías vigentes.

### 17.2 Matriz de funciones

| Función | Clasificación inicial | Condición |
|---|---|---|
| Análisis post-match propio | Verde probable | Registro, privacidad y fuentes aprobadas |
| Recomendación pregame Top 3 | Verde/amarillo | Alternativas, explicación y sin dictar |
| Runas recomendadas | Verde probable | Display read-only |
| Autoaplicar runas | Amarillo | Auditoría, consentimiento, feature flag |
| Autoaplicar hechizos | Amarillo | Auditoría, consentimiento, feature flag |
| Crear item set | Amarillo | Validar endpoint y política |
| Champion Guard con confirmación | Amarillo | Una acción humana, revisión específica |
| Auto-hover/auto-lock sin input | Rojo / No-Ship | Sólo con aprobación escrita explícita |
| Overlay de ramas de item | Amarillo | Datos visibles, baja distracción, revisión |
| Instrucciones “ve aquí ahora” | Rojo | No implementar |
| Memoria/inyección/hooks | Rojo | Nunca implementar |
| Grabación de ventana propia | Verde probable | Consentimiento y pruebas Vanguard |
| Scouting de identidad oculta | Rojo | No implementar |
| Miniquiz en ventana separada | Verde probable | Sin modificar propiedades Riot ni ads |

“Verde probable” no sustituye aprobación formal.

### 17.3 Kill switches

Feature flag independiente para:

- LCU read.
- Runes write.
- Summoner write.
- Item set write.
- Champion Guard write.
- Overlay live updates.
- Capture.
- Model version.

El servidor puede forzar read-only. El cliente conserva un safe default si no recibe flags.

### 17.4 Auditoría de acciones

Toda escritura registra localmente:

- Momento.
- Estado.
- Acción.
- Razón.
- Consentimiento vigente.
- Resultado.
- Versión del cliente y regla.

Sin datos secretos. El usuario puede consultar y borrar el log.

### 17.5 Proceso antes de ship

1. Registrar el concepto.
2. Presentar wireflows y endpoints.
3. Obtener feedback de Riot Developer Relations.
4. Implementar en entorno controlado.
5. Probar PBE y Vanguard.
6. Documentar comportamiento exacto.
7. Revisión de seguridad.
8. Lanzar con feature flag a cohort pequeña.
9. Monitorear y poder apagar en minutos.

---

## 18. Accesibilidad y compatibilidad

### 18.1 Matriz inicial

- Windows 10 22H2 x64.
- Windows 11 23H2 o posterior.
- x64; ARM no en MVP.
- Borderless y windowed soportados.
- Exclusive fullscreen: overlay puede no estar disponible; mostrar advertencia.
- 16:9, 16:10 y ultrawide.
- DPI 100–200 %.
- Multi-monitor.
- HDR probado, sin garantía hasta validación.

### 18.2 Accesibilidad

- Navegación completa por teclado.
- Lector de pantalla en Desktop.
- Escalado de texto.
- Contraste alto.
- Paletas aptas para daltonismo.
- No depender sólo de color.
- Animaciones reducibles.
- Subtítulos/trascripción para contenido futuro.
- Hotkeys configurables y detección de conflictos.

### 18.3 Localización

Inicial:

- Español neutro con variantes de términos es-AR/es-MX cuando aporte.
- Inglés.

Reglas:

- Nombres oficiales de campeones/items según locale.
- Glosario consistente.
- Contenido generativo validado contra idioma y parche.

---

## 19. Métricas

### 19.1 North Star

**Ciclos de mejora verificados por usuario activo semanal.**

Un ciclo verificado requiere:

1. Diagnóstico visto.
2. Misión aceptada.
3. Al menos tres oportunidades válidas.
4. Evaluación de cambio.

### 19.2 Funnel

| Etapa | Métrica |
|---|---|
| Instalación | Instalación completada / iniciada |
| Activación | Cuenta vinculada + primer diagnóstico |
| Valor inicial | Tiempo a primera evidencia útil |
| Compromiso | Misión aceptada |
| Ejecución | Bloque 3–5 partidas completado |
| Aprendizaje | Comportamiento mejorado por oportunidad |
| Retención | D7, D30 y W8 |
| Advocacy | Recomendación y exportación voluntaria |

### 19.3 Calidad de coaching

- Precisión confirmada por usuario.
- Acuerdo ciego entre coaches.
- False positive rate.
- Calibration error.
- Tasa de “falta contexto”.
- Misiones con oportunidades suficientes.
- Cambio conductual y persistencia 10 partidas después.
- Diversidad real de ramas de items.

### 19.4 Guardrails

- Impacto FPS/1% low.
- CPU/RAM/GPU.
- Crashes.
- Fallos LCU.
- Acciones automáticas no esperadas: objetivo 0.
- Incidentes de privacidad.
- Quejas por distracción.
- Sesiones excesivas y señales de tilt.
- Compliance incidents: objetivo 0.

### 19.5 Métricas que no serán North Star

- Win rate de corto plazo.
- LP por semana.
- Horas dentro de la aplicación.
- Número bruto de partidas.
- Cantidad de notificaciones abiertas.

El resultado de una partida es ruidoso y no demuestra por sí solo aprendizaje.

### 19.6 Experimento principal

Comparar:

- Control: reporte estadístico genérico.
- Variante: evidencia + una misión + verificación.

Outcome:

- Cambio en conducta por oportunidad válida.
- Retención del aprendizaje.
- Confianza/valor percibido.

No evaluar sólo CTR.

---

## 20. Monetización

### 20.1 Principios

- Free tier útil, no una demo rota.
- Sin anuncios in-game, en loading o sobre propiedades Riot.
- No vender datos de jugadores.
- No cobrar por recuperar una racha.
- La recomendación básica no debe manipularse para conversión.

### 20.2 Propuesta

| Plan | Contenido |
|---|---|
| Free | Análisis semanal limitado, una misión activa, draft básico, mini-quiz, patch impact |
| Pro, USD 4.99–6.99/mes sujeto a research | Análisis ilimitado razonable, clips, ramas avanzadas, historial extendido, Duo Lab |
| Coach, futuro | Exportación, cohortes consentidas, anotaciones y workflow |

Precios deben validarse por región, willingness-to-pay y política Riot vigente.

### 20.3 Cost control

- Reglas locales primero.
- Procesamiento de video local.
- LLM pequeño/estructurado sólo donde agrega valor.
- Caché por parche.
- Batch post-match.
- Límites fair-use transparentes.

---

## 21. Operación y observabilidad

### 21.1 Health

- Estado por módulo.
- Versión de APIs y estáticos.
- Cola de análisis.
- Uso de almacenamiento.
- Encoder.
- Feature flags activos.

### 21.2 Logs

- Estructurados.
- Niveles y rate limits.
- Sin chat, teclas, video ni token.
- Correlation ID sin identidad directa.
- Rotación y límite de tamaño.

### 21.3 Incidentes

Severidades:

- SEV0: posible ban, seguridad, privacidad o acción no consentida.
- SEV1: impacto de FPS importante, crash masivo, integración rota.
- SEV2: recomendación/modelo degradado.
- SEV3: UI o contenido menor.

Para SEV0/SEV1:

- Kill switch.
- Comunicado transparente.
- Preservación mínima de evidencia técnica.
- Postmortem.

### 21.4 Patch day

- Congelar automatización si IDs no coinciden.
- Validar schema y assets.
- Marcar datos tempranos.
- Ejecutar smoke tests.
- Habilitar gradualmente.

---

## 22. QA y estrategia de pruebas

### 22.1 Capas

- Unit tests de reglas.
- Property tests de árboles de items y legalidad.
- Fixtures de LCU por estado.
- Contract tests de APIs.
- Golden matches/timelines.
- Golden video alignment.
- Model evaluation offline.
- UI automation fuera de partida.
- Soak tests de agente.
- Crash recovery.
- Security testing.
- Performance lab.

### 22.2 Matriz de entorno

- Windows 10/11.
- Intel/AMD CPU.
- NVIDIA/AMD/Intel GPU.
- 8/16/32 GB RAM.
- SSD y disco lento.
- 1080p/1440p/4K.
- Ultrawide.
- 100/125/150/200 % DPI.
- Uno y varios monitores.
- Borderless/windowed.
- Distintas tasas de refresco.
- HDR on/off.
- Encoder disponible/no disponible.

### 22.3 Escenarios críticos

- League se cierra durante draft.
- Dodge.
- Swap de rol.
- Flex pick cambia de lane.
- Cambio manual de runas tras autoaplicar.
- Parche sin datos.
- LCU endpoint cambia.
- Internet cae.
- Riot API rate limit.
- Capture pierde ventana.
- Disco lleno.
- Encoder resetea.
- Usuario inicia nueva partida durante análisis.
- Overlay en monitor secundario.
- Alt-tab repetido.
- Update pendiente durante partida.

### 22.4 Gates de release

- Cero violaciones conocidas de matriz roja.
- Aprobación/comunicación Riot documentada para función sensible.
- Performance gates cumplidos en máquina base.
- Crash-free target.
- Detector principal con precisión aceptada por coaches y usuarios.
- Borrado/exportación probados.
- Firma y rollback probados.
- Accesibilidad crítica sin bloqueos.

---

## 23. Roadmap

### Fase 0 — Viabilidad y cumplimiento, 3 semanas

Entregables:

- Registro inicial y consulta a Riot.
- Inventario de endpoints y matriz legal.
- PoC C++ de detección de estado read-only.
- PoC de captura WGC + H.264 hardware.
- Benchmark base.
- Wireflows de champion select, quiz, overlay y post-match.
- Cinco entrevistas con jugadores y dos coaches como mínimo.

Go/no-go:

- Captura cumple impacto aceptable.
- Datos permiten al menos tres detectores confiables.
- No existe bloqueo de política al núcleo read-only.

### Fase 1 — Vertical slice, 6 semanas

- Onboarding local.
- Perfil y 20 partidas.
- Tres detectores.
- Una misión.
- Champion select read-only.
- Quiz estático contextual.
- Overlay mock/data pregame.
- Captura y un clip sincronizado.
- Telemetría de rendimiento.

Prueba con equipo interno y 20–30 testers.

### Fase 2 — MVP privado, 8–10 semanas

- Diez detectores.
- Top 3 calibrado inicial.
- Runas, spells y árbol de items.
- Champion Guard confirmable.
- Quiz adaptativo.
- Clips automáticos.
- Skill tree/racha.
- Privacidad, update, crash reporting.
- Español/inglés.

Alpha: 100–300 usuarios.

### Fase 3 — Beta pública, 6–8 semanas

- Escala backend.
- Performance matrix completa.
- A/B del ciclo de aprendizaje.
- Billing/entitlements.
- Soporte.
- Patch-day automation.
- Riot audit de escrituras.
- Duo Lab beta si el núcleo ya retiene.

### Fase 4 — V1, 4–6 semanas

- Hardening.
- Accesibilidad.
- Onboarding refinado.
- Model calibration.
- Exportación de clips.
- Lanzamiento gradual.

### Estimación global

- Equipo pequeño completo: **6–8 meses** para una V1 robusta.
- Fundador solo: **9–15 meses**, reduciendo alcance.
- Un prototipo demostrable: **8–10 semanas** si se limita a read-only, tres detectores, un clip y una misión.

Las estimaciones dependen de la respuesta de Riot y del nivel de precisión exigido.

---

## 24. Equipo

### Núcleo recomendado

- 1 ingeniero C++/Windows senior.
- 1 ingeniero backend/data.
- 1 data scientist o ML engineer con experiencia en evaluación.
- 1 product designer/researcher, inicialmente parcial.
- 1 QA/performance engineer, inicialmente parcial.
- 1 coach high-Elo como experto de dominio.
- Asesoría legal/privacidad puntual.

### Si comienza una sola persona

Prioridad:

1. Cliente C++ read-only.
2. Tres detectores.
3. Un flujo post-match excelente.
4. Captura y clips.
5. Champion select.

Postergar:

- Autoescrituras.
- Duo Lab.
- CV.
- Coach plan.
- Billing complejo.

---

## 25. Riesgos y mitigaciones

| Riesgo | Prob. | Impacto | Mitigación |
|---|---:|---:|---|
| Riot no aprueba auto-lock | Alta | Alta | No-Ship; Champion Guard con confirmación |
| Cambios de LCU | Alta | Media/alta | Adaptador versionado, read-only fallback, kill switch |
| Vanguard o parche rompe overlay/captura | Media | Alta | API externas, PBE, rollout gradual, no inyección |
| Impacto de FPS | Media | Alta | Procesos mínimos, hardware encode, gates y degradación |
| Diagnósticos falsos | Alta inicial | Alta | Confianza, feedback, reglas explicables, revisión coach |
| Patch churn | Alta | Media | Snapshots, freeze por incompatibilidad, smoke tests |
| Cold start | Alta | Media | Preferencias manuales y lenguaje de baja confianza |
| Coste cloud/LLM | Media | Media | Local-first, batch, reglas, caché |
| Video expone PII | Media | Alta | Local default, preview, redacción y borrado |
| UI distrae | Media | Alta | Presupuesto cognitivo y ocultar en combate |
| Gamificación fomenta tilt | Media | Alta | Racha por estudio, pausas, sin recompensa por spam |
| Competidores copian features | Alta | Media | Dataset intervención→cambio, confianza y workflow |
| Recomendación por WR sesgado | Alta | Alta | Modelar timing, contexto, muestra y causalidad limitada |
| Dependencia de un fundador | Media | Alta | Docs, tests, modularidad, runbooks |
| Nombre en conflicto | Media | Media | Búsqueda de marca antes de branding |

---

## 26. Criterios de aceptación del MVP

### Producto

- Un usuario nuevo obtiene una hipótesis comprensible en < 2 minutos tras ingesta.
- Champion select presenta tres opciones válidas o explica por qué no puede.
- Ningún campeón no poseído se recomienda como seleccionable.
- Runas/items corresponden al parche instalado.
- Loading quiz se cierra antes del control activo.
- Overlay nunca muestra más de tres opciones.
- Post-match contiene evidencia y confianza.
- El usuario puede completar un bloque y ver evaluación.

### Rendimiento

- Cumple gates de sección 15 en máquina base.
- Analyzer permanece pausado durante partida.
- No existen escrituras sostenidas sin captura.
- Capture se degrada o detiene antes de superar límite.

### Seguridad y privacidad

- Grabación off por defecto.
- Micrófono off por defecto.
- Borrado/exportación funcionan.
- Tokens no aparecen en logs.
- IPC restringido al usuario.
- Update firmado y rollback probado.

### Cumplimiento

- Funciones rojas ausentes o compiladas fuera de producción.
- Escrituras desactivadas hasta aprobación.
- Logs de acción disponibles.
- Read-only fallback probado.
- Disclaimers y registro completos.

### Calidad

- Detectores MVP alcanzan umbral definido individualmente.
- Expert review sin defectos críticos.
- Feedback “incorrecto/falta contexto” bajo umbral acordado.
- Ninguna explicación generativa inventa eventos en golden set.

---

## 27. Decisiones pendientes

| Decisión | Owner sugerido | Fecha límite | Criterio |
|---|---|---|---|
| Nombre definitivo | Founder/Product | Antes de alpha pública | Marca y dominio |
| Backend Rust vs C++ | Tech lead | Fin Fase 0 | Talento, costo y operación |
| Base local y cifrado | Security/Windows | Fin Fase 0 | Threat model y rendimiento |
| Umbrales de detectores | Data + coach | Fase 1 | Precision/recall |
| Retención por región | Legal/Product | Antes de alpha | Regulación y UX |
| Precio | Product | Beta | Research y costes |
| Auto-runas/spells | Riot + Product | Antes de beta | Aprobación escrita |
| Champion Guard write | Riot + Product | Fase 2 | Acción humana aceptable |
| Auto-lock | Riot | Indefinido | Aprobación explícita; si no, no existe |
| Upload de video | Product/Privacy | V1/V2 | Demanda real y consentimiento |

---

## 28. Historias de usuario prioritarias

### Epic: preparación

- Como jugador autofill, quiero que el rol asignado tenga prioridad sobre mi preferencia para no entrar con hechizos incorrectos.
- Como main, quiero que mi dominio pese más que una tier list para no recibir picks teóricamente fuertes que no sé usar.
- Como jugador, quiero tres opciones explicadas para conservar la decisión.
- Como jugador distraído, quiero alertas antes de que termine el timer y confirmar mi respaldo.
- Como jugador, quiero que una edición manual quede intacta.

### Epic: partida

- Como jugador, quiero ver sólo la próxima rama de compra al abrir tienda.
- Como jugador de low-spec, quiero desactivar captura y mantener el coaching.
- Como usuario de ultrawide, quiero que el overlay no cubra HUD.
- Como jugador en combate, no quiero notificaciones ni animaciones.

### Epic: revisión

- Como jugador, quiero ver el clip exacto que sustenta el consejo.
- Como jugador, quiero marcar una inferencia como incompleta.
- Como jugador con poco tiempo, quiero terminar la revisión en cinco minutos.
- Como jugador que perdió, quiero una acción controlable, no una lista de culpas.

### Epic: progreso

- Como usuario, quiero mantener mi racha revisando un puzzle aunque no juegue.
- Como usuario, quiero saber si una habilidad está realmente consolidada.
- Como duo, queremos una misión compartida sin exponernos públicamente.

---

## 29. Definiciones de eventos analíticos

| Evento | Propósito | Propiedades mínimas |
|---|---|---|
| onboarding_completed | Activación | mode, privacy_profile |
| diagnosis_viewed | Valor | detector_id, confidence_band |
| diagnosis_feedback | Calidad | label, reason_category |
| mission_accepted | Funnel | mission_type, block_size |
| opportunity_observed | Denominador | mission_type, valid, exclusion |
| mission_evaluated | North Star | result, valid_opportunities |
| draft_recommendation_seen | Utilidad pregame | top3_count, confidence |
| draft_choice_confirmed | Relevancia | rank_of_choice, manual |
| integration_write | Auditoría | type, consent, result |
| quiz_answered | Aprendizaje | concept, correct, latency_bucket |
| overlay_expanded | UX | phase, via_hotkey |
| clip_reviewed | Evidencia | detector_id, watch_bucket |
| performance_sample | Guardrail | state, cpu, ram, gpu |

Nunca incluir:

- Texto de chat.
- Teclas presionadas.
- Frame/video.
- Token.
- Identidad de terceros no necesaria.

---

## 30. Contratos de explicación

Toda recomendación relevante expone un objeto lógico equivalente a:

- recommendation_id.
- type.
- patch.
- inputs_used.
- options.
- reasons.
- risk.
- confidence.
- uncertainty_reason.
- model_or_ruleset_version.
- expires_at.
- policy_mode.

Toda evidencia expone:

- evidence_id.
- match_id pseudonimizado.
- game_timestamp.
- source.
- observed_facts.
- inference.
- confidence.
- exclusions_checked.
- clip_ref local opcional.

Esto permite auditar, probar, traducir y evitar que la UI invente explicaciones.

---

## 31. Backlog priorizado

### P0 — Necesario para probar la tesis

- Máquina de estados.
- Match/timeline ingest.
- Perfil básico.
- Tres detectores de alta confianza.
- Evidencia timeline.
- Misión y verificación.
- Top 3 read-only.
- Quiz.
- Overlay estático de items.
- Captura beta.
- Performance harness.
- Consentimiento, borrado y kill flags.

### P1 — Necesario para MVP competitivo

- Diez detectores.
- Clips automáticos.
- Skill tree.
- Racha.
- Patch impact.
- Item tree completo.
- Runas/spells contextuales.
- Champion Guard confirmable.
- Update firmado.
- Billing básico.

### P2 — Después de product-market signal

- Duo Lab.
- CV local.
- Coach workflow.
- Replays de referencia.
- Modelos personalizados avanzados.
- Compartir clips con redacción.

---

## 32. Fuentes y fundamentos

### Producto y mercado

- [Sitio oficial de iTero](https://www.itero.gg/)
- [iTero en Overwolf](https://www.overwolf.com/app/itero_gaming-itero_drafting_coach)
- [GIANTX adquiere iTero Gaming](https://giantx.gg/en/blogs/news/giantx-acquires-league-of-legends-ai-coaching-start-up-itero-gaming)
- [Comparación de companion apps publicada por iTero](https://www.itero.gg/articles/what-is-the-best-league-of-legends-companion-app-in-2025) — fuente afiliada; se usa como autodescripción, no como evaluación independiente.
- [Mobalytics app](https://mobalytics.gg/lol/glp/app-download)
- [Porofessor download](https://porofessor.gg/download)

### Riot, API y cumplimiento

- [Riot Developer Portal — League of Legends](https://developer.riotgames.com/docs/lol)
- [Riot Games API — General Policies](https://developer.riotgames.com/policies/general)
- [Riot Games Community Pact](https://www.riotgames.com/en/community-pact)
- [Riot Games Terms of Service](https://www.riotgames.com/en/terms-of-service)
- [Vanguard developer FAQ](https://www.riotgames.com/en/DevRel/vanguard-faq)
- [Anonymizing Your Riot ID](https://support.riotgames.com/en-us/riot/account/anonymizing-your-riot-id)

### Windows nativo

- [Windows App SDK](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/)
- [WinUI 3](https://learn.microsoft.com/en-us/windows/apps/winui/winui3/)
- [Screen capture con Windows.Graphics.Capture](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
- [CreateForWindow para capturar HWND](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)
- [Media Foundation H.264 video encoder](https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-encoder)
- [Media Foundation MP4 encoding tutorial](https://learn.microsoft.com/en-us/windows/win32/medfound/tutorial--encoding-an-mp4-file-)
- [DirectComposition concepts](https://learn.microsoft.com/en-us/windows/win32/directcomp/basic-concepts)
- [Requisitos actuales de League of Legends](https://support.riotgames.com/en-us/league-of-legends/performance/minimum-and-recommended-system-requirements-league-of-legends)

### Referencias técnicas exploratorias

- [analyze-lol-match: análisis de timeline y timestamps](https://github.com/jinayoon/analyze-lol-match)
- [LoLytics: exploración de impacto de eventos](https://github.com/fqhd/LoLytics)

Las fuentes enlazadas deben revisarse nuevamente en cada release: políticas, APIs, cliente y requisitos cambian.

---

## 33. Anexo A — Guion de revisión de cinco minutos

**0:00–0:20 — Resultado sin juicio**  
“Esta partida terminó a X minutos. Tu misión activa tuvo N oportunidades válidas.”

**0:20–1:00 — Fortaleza**  
Mostrar una conducta que conviene conservar.

**1:00–3:00 — Patrón principal**  
Dos clips, hechos observados, inferencia y confianza.

**3:00–4:00 — Contrafactual**  
Dos alternativas plausibles y qué información cambiaría la elección.

**4:00–5:00 — Próximo intento**  
Misión, métrica y una pregunta de recuperación.

---

## 34. Anexo B — Ejemplo de plan de draft

**Contexto:** usuario support, Modo Escalar; aliado con carry inmóvil; rival con engage y burst mágico.

| Orden | Candidato | Razón | Riesgo |
|---|---|---|---|
| Mejor personal | Campeón A | Alto dominio, peel confiable, buena respuesta | Menor presión de línea |
| Seguro | Campeón B | Lane estable y disengage | Menos engage propio |
| Estratégico | Campeón C | Completa frontline y engage | Poca experiencia reciente |

**Runas:** principal contra burst/CC; alternativa si cambia el matchup.  
**Hechizos:** Exhaust si la amenaza de all-in permanece; Ignite sólo si el plan de línea lo justifica.  
**Items:** primera vuelta por umbral; botas MR si la amenaza confirma; rama de peel si el carry aliado concentra recursos.  
**Quiz:** identificar la habilidad rival que inicia el all-in y el timing del primer power spike.

---

## 35. Anexo C — Checklist para una recomendación de item

- ¿Existe en el parche?
- ¿Es legal para ese campeón/modo?
- ¿El jugador puede pagarlo o qué componente compra?
- ¿Responde a una amenaza concreta?
- ¿Se consideró el timing, no sólo el win rate final?
- ¿Se comparó con al menos una alternativa?
- ¿Se explicó cuándo no comprarlo?
- ¿La muestra corresponde a rol/rango/parche?
- ¿Se respetó una elección manual?
- ¿La UI cabe en una línea y no distrae?

---

## 36. Anexo D — Definition of Done de una función

Una función no está terminada hasta que:

- Tiene historia y criterio de aceptación.
- Tiene clasificación de política.
- Tiene estado offline/read-only.
- Tiene tests unitarios e integración.
- Cumple performance budget.
- Tiene telemetría mínima y respetuosa.
- Tiene accesibilidad.
- Tiene localización.
- Tiene kill switch si interactúa con Riot.
- Tiene manejo de error.
- Tiene documentación de privacidad.
- Fue probada en patch actual y PBE cuando corresponda.

---

## 37. Recomendación de inicio

Construir primero un **vertical slice extremadamente honesto**:

1. Leer 20 partidas.
2. Detectar un único patrón de alta confianza.
3. Mostrar dos evidencias de timeline.
4. Crear una misión de tres partidas.
5. Grabar una partida y producir un clip alineado.
6. Verificar cambio.

En paralelo, demostrar el presupuesto C++ nativo con Agent, Overlay y Capture. Sólo después conviene ampliar la lista de features o invertir en modelos complejos.

La prueba decisiva no es que RiftLoop pueda describir una partida. Es que un jugador vea la evidencia, cambie una decisión repetible y el producto pueda demostrarlo sin degradar el juego ni cruzar los límites de Riot.
