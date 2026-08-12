# TODO: paridad completa con el sistema voxel de Pokemon Rojo

## Estado

- Rama de trabajo: `feature/diorama-red-parity`.
- Base historica inspeccionada: `da18b4436`; sus decisiones geometricas no son autoridad.
- Referencia Rojo congelada: `b21fd46ea789a0b8cb99d2c7e0add5a007568a54`.
- Especificacion: `docs/diorama_red_parity.md`.
- No se anadiran editores interactivos nuevos durante este TODO; si se permite un compilador offline
  que produzca modelos `.vox` inspeccionables y mallas generadas.
- Fase **R5 pausada por decision del usuario** (`implementation`). No continuar R5 hasta que el
  usuario lo indique expresamente.
- Ultima fase aprobada manualmente: **R4**.
- El stash anterior permanece aislado y no se mezcla aqui.

## Protocolo obligatorio de fases

Cada fase tiene cinco estados: `pending`, `implementation`, `automatic-passed`,
`manual-pending` y `approved`.

R0 creara tres autoridades versionadas:

- `data/diorama/red_parity_gates.json`: estado, hashes de binarios,
  comandos automaticos, escenarios manuales, tester, fecha UTC y aprobacion textual;
- `data/diorama/red_parity_scenarios.json`: mapa/layout, coordenadas, facing, pitch,
  resolucion, setup y resultado esperado de cada escenario;
- `docs/diorama_red_parity_traceability.md`: funcion/regla de la referencia, adaptacion
  Emerald, divergencia, fixture y resultado esperado independiente.

`check_red_parity_gate.py --phase Rn --require-approved-previous` bloqueara una fase si
la anterior no esta aprobada. Cada ciclo se entrega en un unico commit con implementacion,
validacion y metadata de aprobacion; los hashes deben corresponder a los binarios probados.

Reglas:

- [ ] Las pruebas manuales las ejecuta exclusivamente el usuario. El agente prepara el
      procedimiento, termina la validacion automatica y al cerrar cada ciclo indica en
      espanol los pasos manuales vigentes; nunca infiere aprobacion por tests o capturas.
- [ ] El procedimiento manual comun vive en
      `docs/diorama_red_parity_manual_testing.md`; los escenarios concretos siguen siendo
      autoridad machine-readable en `red_parity_scenarios.json`.
- [ ] Solo puede existir una fase en `implementation` o `manual-pending`.
- [ ] No se empieza codigo de la fase siguiente hasta que el usuario escriba una
      aprobacion explicita de la prueba manual de la fase actual.
- [ ] Un test automatico no sustituye el gate manual.
- [ ] Si la prueba manual falla, la fase vuelve a `implementation`.
- [ ] Durante `implementation`, cada correccion visual ejecuta solo tests focalizados, un
      build Diorama y una comprobacion manual reducida del objeto afectado.
- [ ] La suite completa, paridad global, build classic, hashes y smoke amplio se ejecutan
      una sola vez cuando el usuario confirma que existe un candidato visual correcto.
- [ ] Si ese cierre descubre una regresion, se vuelve a `implementation`, se invalidan
      hashes/evidencia y se recupera el ciclo focalizado; no se repite todo tras cada intento.
- [ ] Tests, paridad, masks golden y builds prueban contratos tecnicos, nunca correccion
      visual. Una mask golden puede congelar un error y no cuenta como aprobacion.
- [ ] Cada fase entrega un procedimiento reproducible; no se acepta "se ve bien en mi
      partida" sin mapa, posicion, direccion, pitch y resultado esperado.
- [ ] Los resultados manuales aprobados se registran en el gate con fecha, commit,
      build, tester y aprobacion textual.
- [ ] Al sustituir una implementacion se eliminan en el mismo commit su codigo, flags,
      datos, tests, generated records y documentacion obsoleta.
- [ ] No se permite codigo muerto, `#if 0`, aliases sin consumidor, dos resolvers o dos
      compositores para la misma responsabilidad.
- [ ] El fallback 2D/plano, el renderer classic y los guards contra arte esperado
      copiado en snapshots nunca se
      consideran residuo.
- [ ] No se toca `map_editor/` durante estas fases.
- [ ] No se recupera ni mezcla `stash@{0}` mientras este TODO este activo.
- [ ] Cambiar codigo, reglas, assets fuente o un escenario invalida sus aprobaciones.
- [ ] Antes de iniciar cada fase, sus ejemplos generales se convierten en scenario IDs
      exactos; el checker rechaza gates con mapas, objetos o edificios sin coordenadas.

Verificacion automatica de cierre comun a todas las fases de codigo, solo despues de que
el usuario acepte el candidato visual reducido:

```bash
make -f Makefile_pc test-diorama-red-parity \
  PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig
make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1 \
  PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig \
  -j"$(nproc)"
make -f Makefile_pc NATIVE_LINUX=1 \
  PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig \
  -j"$(nproc)"
```

El target `test-diorama-red-parity` se crea en R0 y excluye completamente
`map_editor/test_server.py`. Hasta entonces R0 ejecutara directamente las pruebas
headless de reglas, compositor, occupancy, terrain, runtime y survey. El editor queda
congelado e incompatible con schemas intermedios hasta una fase posterior a R10.

Durante iteracion no se ejecuta este bloque. Se usa el test unitario directamente
relacionado con el cambio y el build Diorama incremental. El objetivo de ese ciclo es
obtener evidencia visual rapidamente, no producir hashes ni declarar el gate listo.

Desde R2, toda fase que publique geometria runtime debe ademas garantizar:

- jobs con copias owned o generaciones de snapshot explicitamente retenidas;
- workers solo CPU y sin acceso a globals mutables;
- creacion, upload y destruccion GL solo en el thread SDL propietario del contexto;
- cancelacion de publicaciones stale por generacion de mapa/reglas/atlas;
- atlas y paleta publicados como valores inmutables o propiedad del render thread;
- IR compilado y consumido por runtime en esa misma fase, con paridad Python/C;
- smoke de classic, input, save/load, audio, NVDA ausente, menus, combate y fallback.

## R0: congelar autoridad y retirar residuos incompatibles

**Estado:** `approved`

**Objetivo:** empezar desde una baseline inequivoca y neutral: conservar infraestructura,
atlas, snapshots, compositor, meshing y fallback, pero retirar toda decision geometrica
del intento anterior que competiria con el port de Rojo.

Implementacion:

- [x] Inventariar toda ruta actual que decide shape, height, plateau o volume.
- [x] Clasificar cada ruta como `keep`, `replace in Rn` o `delete now` con consumidor.
- [x] Eliminar la implementacion runtime antigua conservada bajo `#if 0`.
- [x] Eliminar el resolver de topologia/constraints del intento anterior.
- [x] Retirar de produccion el detector residual que solo mide IDs norte-sur, junto con
      sus flags, records y tests exclusivos; R5 introducira un detector nuevo.
- [x] Eliminar flags y schema usados exclusivamente por ese detector.
- [x] Regenerar C sin records automaticos residuales.
- [x] Eliminar pins, anchors, perfiles y reglas geometricas del intento anterior.
- [x] Eliminar datos experimentales sin validacion.
- [x] Confirmar que los 518 mapas carecen de perfiles experimentales heredados.
- [x] Crear un informe machine-readable de resolvers y precedencia; debe existir uno por
      responsabilidad.
- [x] Crear los manifests de gates, escenarios y trazabilidad definidos arriba.
- [x] Crear el target `test-diorama-red-parity` sin dependencias de `map_editor/`.
- [x] Crear instrucciones manuales directas para los escenarios R0.
- [x] Fijar una denylist de simbolos, flags, campos, files y generated records retirados.
- [x] Sustituir el test de confirmacion heredado por validacion de estados y registros de
      aprobacion; retirar el manifest y audit anteriores.
- [x] Eliminar el roadmap y documentacion por fases del intento anterior.

Validacion automatica especifica:

- [x] Buscar y rechazar `#if 0` dentro de modulos Diorama retirados.
- [x] Rechazar campos de schema sin consumidor.
- [x] `compile_rules.py --check` produce cero records de terreno heredados.
- [x] Builds classic y Diorama pasan.

**Prueba manual obligatoria R0:**

1. Ejecutar los escenarios versionados `R0-LITTLEROOT`, `R0-ROUTE101`, `R0-ROUTE104`,
   `R0-ROUTE115` y `R0-MT-CHIMNEY` con sus coordenadas/facing/pitch declarados.
2. Confirmar que no aparecen columnas nuevas dependientes de camara o viewport.
3. Ejecutar los escenarios de ida/vuelta para confirmar determinismo.
4. Ejecutar `R0-CLASSIC-SMOKE` para movimiento, menus, combate y carga de save.
5. Confirmar que el contenido no clasificado queda plano/2D, nunca como torre inventada.

**Gate R0:** el usuario aprueba explicitamente la baseline conservadora y el informe no
contiene ninguna ruta residual sin owner. Solo entonces empieza R1.

## R1: corpus extraido y survey reproducible equivalente a Rojo

**Estado:** `approved`

**Objetivo:** disponer del equivalente Emerald de los datos extraidos y de
`voxel_survey.lua` antes de escribir detectores.

Implementacion:

- [x] Generar para los 441 layouts el grid completo de metatile, collision, elevation,
      behavior, layer type, tileset y source coordinates.
- [x] Enlazar cada celda con composiciones `base`, `foreground`, `full` y provenance.
- [x] Generar sets fiables por behavior: water, ledge, stairs, bridge, hole y movement.
- [x] Generar candidatos walkable/blocked sin convertirlos todavia en geometria.
- [x] Registrar map type, conexiones, eventos, warps, puertas y mutaciones conocidas.
- [x] Construir `tools/diorama_survey/` sin depender del editor.
- [x] Capturar por spot `flat`, `v15`, `v35`, `v50` y `v75` con postprocesado desactivado.
- [x] Guardar manifest JSON con commit, mapa, layout, coordenada, facing, pitch y hash.
- [x] Fijar viewport/FBO, stable frame, interpolacion, weather/fade, RNG, animaciones,
      config, save y generaciones de assets.
- [x] Separar hash semantico determinista de snapshot/IR/camara y comparacion de imagen
      con tolerancia declarada por driver.
- [x] Generar una suite global declarativa que cubra los 518 mapas, los 441 layouts y los
      75 tilesets; ningun mapa concreto limita el alcance de implementacion.
- [x] Probar que dos ejecuciones producen el mismo inventario y nombres de captura.

Validacion automatica especifica:

- [x] 518 mapas, 441 layouts y 75 tilesets presentes.
- [x] Los 18.318 metatiles logicos componen igual en Python y C.
- [x] Ninguna captura o catalogo generado entra en Git.

**Prueba manual obligatoria R1:**

1. Ejecutar el survey completo de Littleroot; el usuario dispensa Route 115 y la segunda
   pasada para este gate.
2. Comprobar que cada spot genera exactamente cinco vistas.
3. Comparar `flat` con el renderer classic en la misma posicion.
4. Verificar que facing, mapa y pitch escritos en el manifest son correctos.
5. Validar que las cinco vistas comparten el mismo snapshot y no derivan.

**Gate R1:** el usuario aprueba la autoridad 2D y la reproducibilidad del survey. Solo
entonces empieza R2.

## R2: clasificador `TileShape` y perfil manual equivalente

**Estado:** `approved`

**Objetivo:** portar la primera etapa de Rojo con precedencia formal y una unica fuente
manual, todavia sin detector de estructuras.

Implementacion:

- [x] Versionar los escenarios exactos del gate R2 para Route 101, Route 115,
      Granite Cave B1F, la revision global de blast radius y el smoke de regresion.
- [x] Preparar checker, tests, target Make, guia manual y trazabilidad del contrato R2 sin
      presentar esta preparacion como evidencia automatica o manual.
- [x] Crear un clasificador headless unico equivalente a `TileShape.lua`.
- [x] Definir salida `class`, `height`, `artMode`, `pool`, `authored`, `source` y
      confidence/evidence.
- [x] Aplicar behaviors visualmente fiables antes del fallback conservador.
- [x] Mantener collision como evidencia, nunca como sinonimo de wall.
- [x] Implementar pins por tileset/metatile.
- [x] Implementar condiciones contextuales equivalentes a `when_above` y vecinos
      cardinales sin depender del orden JSON.
- [x] Implementar alturas nominales por clase.
- [x] Implementar pools semanticos.
- [x] Implementar `propGround` manual y su forma automatica pendiente.
- [x] Derivar grass, flowers, water y animaciones cuando la metadata sea fiable.
- [x] Dejar arte ambiguo plano y emitir un registro de ambiguedad.
- [x] Eliminar cualquier clasificador anterior sustituido y sus tests.
- [x] Compilar la decision final a C; runtime no reinterpreta precedencia.
- [x] Publicar overlay runtime `class/source/evidence` desde datos compilados y snapshot,
      sin leer globals mutables.

Spike IA local completo opcional, no autoritativo:

- [x] Registrar decision `not-run` o `run` sin bloquear R2. Decision actual: `not-run`,
      no authoritative y sin dependencia de build/runtime.
- [ ] Si se ejecuta, definir antes benchmark, precision minima y ahorro humano minimo.
- [ ] Evaluar solo offline/local embeddings, segmentacion y opcionalmente un VLM.
- [ ] Registrar licencia, modelo, hash, seed, prompt, parametros, recursos y tiempos.
- [ ] Excluir pesos y outputs de Git; no crear dependencia de build/runtime.
- [ ] Conservar solo una herramienta que supere los umbrales predeclarados.
- [ ] Si no mejora el flujo, eliminar codigo, dependencias, pesos y documentacion del
      spike en esta misma fase.
- [ ] Ninguna sugerencia IA entra en JSON authoritative sin revision humana.

Validacion automatica especifica:

- [x] Fixtures de precedencia: context > pin general > behavior fiable > fallback.
- [x] Fixtures de tile reutilizado con dos contextos.
- [x] Cero celda clasificada wall solamente por collision.
- [x] Paridad Python/C de todas las salidas del clasificador.

**Prueba manual obligatoria R2:**

1. Ejecutar survey en Route 101, Route 115 y Granite Cave B1F.
2. Activar overlay runtime de `class/source` sin usar el editor.
3. Confirmar ground, water, ledges y stairs conocidos.
4. Confirmar que arboles, edificios y roca ambigua permanecen planos si no tienen pin.
5. Seleccionar y commitear un pin realmente authoritative, verificar todas sus
   apariciones y su blast radius; los pins temporales solo existen en fixtures headless.

**Gate R2:** el usuario aprueba precedencia, fallback y formato de correccion manual.
Solo entonces empieza R3.

Resultado del usuario: **APROBADO** (2026-08-01). La validacion detecto que los cuatro
behaviors diagonales `MB_JUMP_*` faltaban en el conjunto fiable de ledges; se corrigieron
y se comprobo en Ruta 101 que la esquina `(6,7)` resuelve como `LEDGE/BEHAVIOR`.

## R3: reclamacion, conectividad y orden `Structures`

**Estado:** `approved`

**Objetivo:** portar el esqueleto de `Structures.lua` sobre layouts completos, sin
voxelizar props ni medir alturas todavia.

Implementacion:

- [x] Crear IR de candidatos con owner, cells, pixels, bbox, class y evidence.
- [x] Implementar aqui el matcher minimo de matrices exactas necesario para que
      edificios/estructuras queden reclamados antes de props y volumenes; R7 construira
      su shell completo.
- [x] Implementar claim masks y `first claim wins`.
- [x] Implementar prioridad determinista entre template, authored special, prop
      candidate y generic volume.
- [x] Detectar tiles void por fuente completamente transparente/negra, como Rojo, y
      combinarlo con metadata Emerald; tratar el ring exterior como problema separado.
- [x] Implementar folding de puertas solo bajo reglas probadas.
- [x] Flood-fill de regiones visuales no reclamadas.
- [x] Separar conectividad de mapa y conectividad real de pixels.
- [x] Impedir fusion entre pools distintos.
- [x] Emitir overlays headless de region/claim/source.
- [x] Eliminar region builders anteriores sustituidos.
- [x] Compilar claims/owners al IR runtime y renderizar diagnostico con el mesher actual.

Validacion automatica especifica:

- [x] Claims independientes del orden de iteracion.
- [x] Ninguna celda reclamada llega a un detector posterior.
- [x] Componentes iguales con distinto pool permanecen separados.
- [x] Conectividad no cruza layouts o void no conectado.

**Prueba manual obligatoria R3:**

1. Survey de Littleroot, Route 115, Fortree y un Pokemon Center.
2. Revisar overlays de claims desde los mismos spots.
3. Confirmar que edificio, vegetacion, props y suelo forman candidatos separados.
4. Confirmar que puertas walkable no rompen fachadas ni desaparecen sin reemplazo.
5. Mover la camara y confirmar que los componentes no cambian.

**Gate R3:** el usuario aprueba boundaries y orden de claims. Solo entonces empieza R4.

Resultado del usuario: **APROBADO** (2026-08-02). La primera inspeccion en Villa Raiz
detecto que `mapEditGeneration` global anulaba todos los owners tras cualquier mutacion
anterior. Se corrigio para invalidar solo candidatos que contienen dirty cells, se repitio
el gate automatico completo y el usuario valido `OWN:TEMPLATE`/`CLM` en `(5,9)` mirando
al norte. El usuario dispenso los escenarios restantes y F5/F6 para este gate.

## R4: props automaticos y cutouts por pixel

**Estado:** `approved`

**Objetivo:** reproducir `extractObjects`, billboards forzados, reliefs y soporte sobre
objetos sin convertir arte ambiguo en cajas.

Implementacion:

- [x] Componer region con apron y provenance.
- [x] Estimar fondo desde suelo vecino compatible.
- [x] Flood-fill de fondo y separar componentes foreground.
- [x] Implementar thresholds configurables y justificados por fixtures Emerald.
- [x] Rechazar regiones altas/repetitivas que no sean props.
- [x] Implementar los modos de outline/clase de Rojo cuando el flood sea ambiguo; una
      prop edge-to-edge debe cambiar de clase o quedar sin soportar, no usar mask extra.
- [x] Generar un prisma por pixel con texel original.
- [ ] Apoyar componentes en sus pies y sobre soportes authored con resultado visual correcto.
- [x] Implementar `propGround` por votacion y override manual.
- [x] Implementar `relief` horizontal perceptible y pools separados.
- [x] No modificar sprites de personajes, que siguen como billboards.
- [x] Compilar cutouts/reliefs al IR runtime y demostrar paridad Python/C en esta fase.

Validacion automatica especifica:

- [x] Masks golden pixel a pixel.
- [x] Cada voxel visible tiene provenance valida.
- [x] Ningun foreground perdido o fondo incluido en la comprobacion visual.
- [x] Prop sobre mesa conserva altura y no pinta suelo a traves del soporte.

**Prueba manual obligatoria R4:**

1. Survey multi-pitch de signs, plants, TVs, stools, cut tree y boulder.
2. Verificar silueta pixel a pixel contra `flat`.
3. Verificar objetos blancos/grises encerrados por outline.
4. Verificar un caso edge-to-edge que debe rechazarse y quedar plano/perfilado.
5. Probar otro mapa que comparta cada tileset modificado.

**Gate R4:** el usuario aprueba masks, soporte y ground replacement. Solo entonces R5.

La segunda prueba visual confirma los carteles 3D de Villa Raiz, pero la TV solo muestra
perspectiva en la franja superior trasera: pantalla y pie siguen planos. Plantas,
taburetes y relief tambien siguen planos. R4 vuelve a `implementation`; la evidencia y
los hashes del candidato anterior quedan invalidados. Las siguientes correcciones usan
solo test focalizado, build Diorama y escenario visual reducido hasta que el usuario
confirme un candidato correcto.

Candidato visual actual: `console` opaco conserva los 256 pixels de pantalla, cuerpo y
pie en vez de los 3 pixels supervivientes del flood anterior; su profundidad es 8 Q32.
Plantas/taburetes layered usan 6 Q32 y el relief 12 Q32. Solo se ejecutaron los tests
focalizados de extractor/terrain y el build Diorama incremental, sin cierre global.

La revision siguiente aprobo visualmente las dos plantas cubiertas de Oldale House1 y la
mesa de Dewford Hall; la tercera planta queda aceptada para cobertura posterior. En la TV,
el bloque principal ya es correcto, pero soporte y pieza trasera estaban a altura cero.
Elevar solo las tres filas de `578` a 25 Q16 fallo: quedaron separadas una celda por
detras y la base siguio plana. La composicion fuente confirma que `513` es suelo, `2` es
el bloque principal y `578` contiene la extension superior. El candidato actual colapsa
ambas celdas en un plano: `2` ocupa altura 0..1 y `578` ocupa 1..1.1875 tras desplazarse
una celda en profundidad. Falta su comprobacion visual reducida.

La siguiente revision confirmo la union superior, pero la base azul seguia plana y la
profundidad era insuficiente. El mapa revelo la tercera celda omitida: `586` en la casa de
Brendan y `691` en la de May. El candidato actual extrae solo sus ocho filas de objeto,
excluye el overlay de suelo y apila base/cuerpo/extension a 0..0.5, 0.5..1.5 y
1.5..1.6875 sobre un plano comun, con profundidad 0.5 para las tres piezas.

La TV completa queda aprobada visualmente en la siguiente revision. Se detectan dos
regresiones: la sustitucion de la base elimina el borde azul de la alfombra y los sprites
ganan la oclusion aunque esten detras de TV, carteles, sillas o mesas. El candidato actual
usa `574`/`696` como residuales exactos del suelo con alfombra y elimina el bias artificial
del shader de sprites; falta comprobar visualmente suelo y oclusion.

El residual `574` se mostro negro en runtime. El candidato siguiente usa `513`, el mismo
suelo visible en `(5,4)`, bajo la base de ambas TVs; geometria y oclusion no cambian.

Sin bias de sprites, una silla de profundidad 0.5 tapa al NPC sentado en la misma celda.
El candidato siguiente usa `0.00025`, aproximadamente media celda de prioridad visual,
en vez del `0.0008` original que atravesaba objetos separados por una celda completa.

La coordenada de suelo correcta es `(5,5)`: metatile `558`; para May se usa el equivalente
`689`. El bias `0.00025` solo libero la parte inferior del NPC y respaldo/TV siguieron
tapando sprites de la misma celda. El candidato con `0.0005` corrige las sillas, pero con
perspectiva baja el personaje aun atraviesa visualmente la TV al compartir su celda; con
perspectiva alta se ordena bien. El candidato actual conserva `0.0005` como bias general y
usa `0.0008` solo cuando el sprite ocupa una celda reclamada por un pixel object. Esto cubre
el grosor frontal de la TV a bajo angulo sin dar esa prioridad a sprites situados en otra
celda detras del mueble.

La comprobacion siguiente invalida ese criterio: la TV sigue cruzando el sprite al bajar
la perspectiva y personajes realmente detras de carteles pasan a dibujarse por encima.
Se retira el `0.0008` condicionado y se restaura `0.0005` global mientras se separa la
necesidad especifica de los taburetes del orden correcto de TV y carteles.

El criterio confirmado es el orden de la cuadricula: un personaje situado detras debe
quedar oculto por los pixels solapados de TV o cartel independientemente del pitch. El
nuevo candidato elimina el bias global y lo convierte en `spriteDepthBias` authored;
solo los dos patrones de taburete usan `0.0005` para conservar al NPC sentado. TV,
carteles y el resto de pixel objects usan cero y dependen del depth buffer compartido.

La comprobacion posterior confirma el orden trasero, pero delante la TV y los carteles
ocultan la cabeza del jugador. El depth continuo no puede representar el orden discreto
de la cuadricula para un billboard alto a todos los pitches. El candidato actual marca
solo los pixels visibles de pixel objects en el bit 2 del stencil y repinta sobre esa
mascara los sprites cuya celda esta inmediatamente al sur. El suelo y otros volumenes no
entran en la mascara; los sprites al norte no reciben el segundo pase.

El usuario aprueba finalmente TV, taburetes, carteles de Villa Raiz y el cartel compartido
de Pueblo Escaso, y dispensa pruebas o cierre automatico adicionales para este commit.

## R5: volumenes genericos y alturas repeat-aware

**Estado:** `implementation`

**Objetivo:** portar el detector automatico que falta actualmente: medir el dibujo
ensamblado y no repetir IDs de metatile como sustituto del arte.

Implementacion:

- [x] Medir runs norte-sur sobre componentes estructurales completos, igual que Rojo.
- [x] Detectar repeticion con una firma Emerald equivalente al tile ID de Rojo: tile,
      capa, paleta, flips y provenance. Comparar pixels solo como evidencia adicional.
- [x] Distinguir extent dibujado de secuencia repetida.
- [x] Aplicar limite fisico equivalente a seis filas de 8 px, adaptado y justificado a
      bandas de arte Emerald, no copiado como tres metatiles por defecto.
- [x] Implementar consenso de region y registrar conflictos.
- [x] Analizar ambos ejes cuando el arte Emerald lo requiera.
- [x] Conservar una banda source distinta por tramo de cara.
- [x] Detectar top rows diferentes como candidato de roof solo en exterior.
- [x] No procesar celdas reclamadas, passable decorativas ni clases authored.
- [x] Emitir confidence y dejar plano cualquier candidato contradictorio.
- [x] Introducir el detector nuevo en un unico modulo; el detector viejo ya fue retirado
      en R0 y no puede reaparecer como fallback oculto.
- [x] Compilar volumenes al IR runtime y renderizarlos en esta fase.

Validacion automatica especifica:

- [x] Bosque largo produce arboles repetidos, no monolito.
- [x] Fachada de varias bandas conserva altura dibujada y source por banda.
- [x] Firmas de fuente equivalentes forman repeticion aunque procedan de metatiles
      distintos.
- [x] Un metatile ID igual con capa/paleta/provenance distinta no se fuerza a repetir.
- [x] Resultado independiente de viewport y orden.

**Prueba manual obligatoria R5:**

1. Survey de Route 115, Mt. Chimney, Jagged Pass, Fortree y Granite Cave B1F.
2. Comparar cada cliff/forest/wall con su extent en `flat`.
3. Confirmar que no existen torres por bordes repetidos.
4. Confirmar que muebles y edificios reclamados no entran en volumen generico.
5. Probar pitches 15/35/50/75 y volver a cada mapa tras una transicion.

**Gate R5:** el usuario aprueba la deteccion automatica general de alturas. Este gate es
el cierre real de la deteccion automatica de terreno. Solo entonces empieza R6.

Primer resultado visual: R4 permanece correcto, pero el candidato R5 convierte demasiadas
clases en gables genericos, recorta los ledges dejando negro bajo la lamina y eleva paredes
de interiores domesticos con source incorrecto. R5 sigue en `implementation`; roofs quedan
como candidatos para una fase de edificios, el eje X requiere recorte/rotacion propios y el
fallback de occupancy debe conservar soporte solido bajo ledges.

La segunda revision confirma ledges e interiores corregidos. El bosque repetido aun usa
celdas cuadradas, repite la textura y presenta runs mas altas dentro de una sola plantacion;
la primera occupancy derivada del alpha foreground no produjo ningun cambio visible porque
los metatiles principales son completamente opacos. El candidato actual separa el fondo
conectado a los bordes para obtener la silueta real y fuerza en toda region vegetal el
periodo repetido dominante, con desempate hacia la menor altura. Los niveles relativos de
Route 104, la bajada a playa y escaleras quedan registrados para R6;
los perfiles definitivos de edificios siguen perteneciendo a R7.

La tercera revision confirma que ese enfoque seguia siendo incorrecto: recortar alpha solo
aplanaba algunas celdas y no cambiaba la clasificacion que originaba los cubos. La comparacion
directa con Rojo muestra que `buildVolume` recibe exclusivamente residuos `upright` despues
de edificios, hulls, escaleras y props; no convierte collision en semantica. El detector R5
ahora exige `artMode=upright`, y los arboles General se reclaman antes como `round-hull` de
altura uniforme. Las celdas `fallback:flat` de edificios quedan planas en vez de adquirir
una altura falsa; R7 debe resolverlas positivamente antes de cualquier fold.

La cuarta revision confirma la mejora estructural y precisa la unidad de dibujo: los arboles
grandes General son matrices 2x3 (fila norte atravesable sobre cuerpo bloqueado 2x2) y los
pequenos son parejas 2x1. El candidato actual reclama primero cuatro variantes grandes y
despues cinco parejas pequenas, y las renderiza como un unico `grouped-hull`: 32x32 a altura
2 o 32x16 a altura 1. La capa base ocupa tanto la fila atravesable como los pixels recortados
de la elipse, evitando que el fondo negro aparezca entre arbol y suelo.

La quinta revision muestra dos fallos restantes de esa primera agrupacion. Los bosques
repetidos contienen cuerpos 2x2 sin fila norte y caian al fallback 2x1, por lo que solo
alcanzaban altura 1; ahora siete matrices 2x2 se resuelven antes del fallback. Ademas, usar
la composicion `full` pegaba el cesped base a la piel y la tapa del hull. La geometria de
arbol usa ahora solo `foreground` con alpha, el suelo usa solo `base`, y la UV de la tapa
abarca el footprint completo. El relleno forestal General 198/199 se trata como una unidad
round por celda, no como una pareja arbitraria ni como un componente monolitico.

La revision de la implementacion de Rojo invalida tambien el hull eliptico anterior: era
equivalente al primer torno/cilindro que la referencia descarto. El candidato actual ensambla
el dibujo completo de cada owner (16x16, 32x16 o 32x32), separa copa y fondo con flood fill
de contorno negro y fallback negro+oscuro para dither, y convierte cada pixel de la mascara
en una cuerda circular de voxeles. Frente y reverso conservan el texel fuente, los laterales
buscan hasta tres pixels hacia el interior para no pintar una pared negra, la tapa combina
aro y muestras profundas, y la ultima fila ocupada se prolonga hasta el suelo. El underlay
`base` sigue siendo independiente y cubre todas las celdas reclamadas. Las pruebas focalizadas
congelan exclusion de hierba/sombra, fallback dither, ensamblado entre cuatro metatiles,
procedencia UV por cuadrante y centrado 32x16.

La primera revision visual de este port muestra un manchurron de voxeles sobre el arbol 2D
original y una degradacion fuerte de rendimiento. El `base` del propio metatile General no
es un ground replacement valido para estas matrices, y emitir una cara independiente por
voxel multiplica innecesariamente vertices y draw payload. Este candidato queda invalidado;
R5 sigue en `implementation` hasta sustituir el suelo por una fuente plana comprobada y
fusionar caras compatibles sin perder procedencia de texel.

La causa concreta es que la hierba cian opaca de Emerald tambien es oscura: el flood generico
retenia entre 79% y 85% de los canvas General. El arte ofrece una separacion estable mejor:
la copa y tronco usan tokens de paleta 2 `{1,2,3,4,6,8}`, y hierba/sombra usan
`{12,13,14,15}`. El candidato corregido compone la mascara con los primeros, conserva el
mayor componente conectado que alcanza la mitad superior y usa la paleta unfaded para que
un fade no altere geometria. Las caras muestrean `full` porque la copa esta horneada en base;
el underlay usa el ground plano dominante de la escena. Frente/reverso se fusionan por runs
y laterales/tapas por intervalos expuestos; un hull que exceda capacidad se revierte completo
en vez de romper el chunk. Los metatiles 198/199 quedan como follaje denso de celda completa,
no como copas redondas.

La reconstruccion automatica posterior tampoco obtuvo aprobacion visual. El siguiente candidato
usa directamente `tree_model/emerald_tree.vox`, aportado por el usuario, para todo patron grande
con cuerpo 2x2. El modelo mide 32x24x48, contiene 6782 voxels y se compila a 2424 quads mediante
greedy meshing por color, sin caras internas. Los patrones 2x3 se anclan en sus dos filas sur de
cuerpo: la fila norte pintada y el cuerpo completo se reemplazan por ground plano, mientras las
dos filas bajo el modelo reciben una sombra uniforme mas oscura. El patron pequeno 2x1 no usa
este activo. La prueba de capacidad cubre 16 arboles grandes dentro de un chunk 8x8.

El primer build con el activo no lo mostro: `structureX/Y` estaban en coordenadas fuente del
layout, mientras `FindTerrainInputCell` trabaja con coordenadas visibles que incluyen
`MAP_OFFSET`. Esto forzaba `treeHullReady=false` para todos los grupos y recuperaba el bloque
elevado texturizado. `BuildInput` reconstruye ahora el origen visible a partir de `mapX/Y` y
`structureLocalX/Y`, valido tambien para mapas conectados. Ademas, cualquier patron grande que
no pueda completar su modelo degrada a ground plano, nunca al volumen fuente.

La siguiente revision aprobo el modelo y el reemplazo de ground, pero encontro hulls 2x1
cortados, tirones y el borde repetido plano. El mesher ya no copia 14544 vertices por arbol:
construye la malla local una sola vez, la sube a un VBO estatico y conserva por chunk solo
instancias `(x,y,z)` de 12 bytes. Los arboles visibles se envian con una unica llamada
`glDrawArraysInstanced`; los bounds completos permanecen en el chunk propietario para no
introducir popping. Todo `grouped-hull` de ancho 2 usa ahora el activo completo, y un owner de
altura 1 se ancla una fila al norte. El borde no conectado se marca como procedencia de render
separada y solo su matriz exacta `468/469/476/477` genera owners 2x2; conexiones y celdas
source-valid conservan su autoridad normal.

La revision posterior detecta que las montanas seguian planas. R5 clasifica ahora
`MB_MOUNTAIN_TOP` positivamente como cliff de una celda, sin convertir collision ni elevation
en altura. La occupancy se extrae de foreground parcial o, para cimas opacas, elimina mediante
flood-fill el fondo conectado a las esquinas usando la paleta unfaded. Los pixels recortados
conservan underlay base. El compilador valida todas las variantes del corpus y rechaza cualquier
mascara vacia o rectangular; runtime, mesher y build Diorama pasan sus checks focalizados.

La primera revision de montanas seguia pareciendo plana porque todas las mascaras vecinas
terminaban en la misma tapa y el greedy mesher las fusionaba. Ademas, Emerald etiqueta varias
caras graficas de Ruta 115 como `MB_NORMAL`. El candidato actual deriva por tile un heightmap
escalonado 4/16..16/16 mediante erosion de su propia silueta. Las caras `MB_NORMAL` solo se
incorporan cuando comparten filas de tile y paleta con una semilla `MB_MOUNTAIN_TOP`, conservan
mascara no rectangular y estan conectadas cardinalmente a esa semilla.

La comprobacion posterior en Ruta 104 seguia plana porque la pared visible no es roca: usa
General `198/199`, follaje denso que `BuildInput` excluia explicitamente del round hull sin
geometria alternativa. Esos tiles conservan su semantica vegetal, pero ahora consumen el
heightmap voxel 4/16..16/16 de su silueta opaca. La prueba runtime fija la celda real `(20,40)`
de `MAP_ROUTE104` antes del nuevo build visual.

La captura posterior identifica la colina correcta: `MAP_ROUTE104 TGT 25,66` mostraba
`CLS:GROUND SRC:FALLBACK`, metatile General `121/0x79`. Forma parte de la familia de terrazas
`{111,113,121,133,135,136,137,141,144}`. Solo sus usos `MB_NORMAL` se convierten en cliff
heightmapped de una celda; usos compartidos con ledges mantienen su behavior y geometria.

La revision visual invalida ese heightmap para terrazas: erosionar la silueta 2D produjo
piramides triangulares de franjas concentricas. El arte representa una pared vertical, no un
mapa topografico horizontal. La familia usa ahora un curso continuo de altura uniforme, con
tapa plana y sin alturas intermedias; esta ruta no consume el modelo VOX del arbol.

La siguiente revision confirma que un curso uniforme tampoco es suficiente: elevaba solo los
metatiles de roca como bloques cuadrados, mientras cesped y arena seguian coplanares. La familia
se resuelve ahora como contorno dirigido sobre el layout completo. En Ruta 104 `(25,66)`, `121`
impone norte=sur+1: el cesped `(25,65)` queda a nivel 1, la pared ocupa 0..1 y la arena `(25,67)`
queda a nivel 0, aunque ambos suelos compartan `elevation 3`. `137/144` usan perfiles diagonales
complementarios; reutilizaciones aisladas no forman curso y degradan de forma segura.

La revision siguiente valida la diferencia relativa pero invalida su anclaje: elevar el cesped
a 1 crea un escalon falso al entrar desde el pueblo. El componente se normaliza ahora por su
plano superior: cesped 0, pared -1..0 y playa -1. Tambien se eliminan las diagonales rectas
sinteticas. Los metatiles con foreground parcial, incluidos 136/144, usan su mascara 16x16
unfaded como contorno exacto entre plano alto y bajo, conservando el borde curvo del arte.

La captura posterior muestra que una mascara binaria sigue siendo incorrecta: eleva pixels a 0
y deja el resto en -1 sin construir la cara intermedia; ademas, muestrear foreground en los
laterales deja ver el clear negro. El contorno curvo pasa a ser el pie de una rampa voxelizada de
seis columnas que asciende progresivamente de -1 a 0. La composicion full opaca se aplica a tapa
y laterales para situar el arte original sobre la cara inclinada y cerrar todo el volumen.

La ampliacion siguiente revela seams negros en cada cambio de angulo: rectas y esquinas
calculaban rampas locales incompatibles. Los perfiles se componen ahora con funciones compartidas:
horizontal=N, vertical=E, esquina interior=min(E,N) y exterior=max(E,N). El contorno curvo solo
modula el interior, mientras las secciones de empalme son canonicas e identicas muestra a muestra.
El borde bajo termina en -1 y el alto en 0; una prueba C compara los cuatro seams completos.

La revision posterior considera la forma base aceptable, pero invalida la rampa: sus alturas
intermedias se leen como miniescalones descendentes y abren huecos entre bandas. Los marrones se
interpretan ahora como profundidad sobre una pared vertical continua. El tono oscuro queda en el
plano base, el medio sobresale un voxel y el claro dos, equivalente a -1/0/+1 alrededor del medio.
La clasificacion cromatica excluye arena y cesped; un overflow omite el relieve, nunca el chunk.

La captura inmediata invalida por completo ese experimento: sustituir la cuna por pared binaria
y superponer prismas recupera bloques cuadrados y nuevos errores negros. Se elimina toda la piel
por luminancia y se restaura la cuna curva anterior con sus perfiles N/E/min/max y materiales full.
Cualquier estudio posterior de color solo podra perturbar la profundidad de esa cuna, nunca
reemplazar su volumen base.

La siguiente captura confirma la restauracion de la cuna y aisla dos defectos todavia presentes:
grandes cavidades negras en concavidades y cortes escalonados repetidos en paredes laterales. Ambos
proceden de una sola suposicion de ocupacion: para el curso normalizado `-1..0`, las muestras
elevadas se generaban como laminas de un voxel en su altura final en vez de columnas apoyadas en
el plano inferior. El mesher rellena ahora cada muestra desde `ground - 1` sin cambiar el heightmap,
los materiales ni las funciones N/E/min/max. El test enfocado congela que todas las caras inferiores
de una terraza negativa comparten el unico plano base; queda pendiente revision visual.

La revision visual siguiente confirma que esta correccion elimina las cavidades negras y las bandas
laterales separadas. La ocupacion solida relativa a `ground` queda validada y no debe volver a
convertirse en laminas. R5 permanece en `implementation`: la confirmacion resuelve ese defecto
tecnico concreto, pero no aprueba por si sola el aspecto final, todavia muy ortogonal, de la terraza.

Para suavizar ese volumen sin reabrir huecos, el nuevo candidato modifica solo el interior del
heightmap 16x16. Las rampas compartidas N/E aplican smoothstep entero conservando exactamente 0 y
16; las esquinas usan composicion bilineal redondeada, de modo que E=N=8 produce interior=4 y
exterior=12 en vez del cruce ortogonal min/max=8. Los cuatro perimetros de cada perfil siguen
coincidiendo voxel por voxel con rampas vecinas o planos alto/bajo. La ocupacion continua desde
`ground - 1`, el arte full opaco y la resolucion permanecen intactos. Test enfocado y build Diorama
pasan; queda pendiente revision visual.

La revision visual invalida tambien este suavizado: modificar formulas interiores sigue produciendo
un volumen que no alcanza una montana voxel logica. Se detiene la iteracion por heightmaps. R5 queda
pausada por decision expresa del usuario hasta disponer de un pipeline automatico de modelos:
arte Emerald inmutable + topologia + plantilla de cuna conocida -> volumen voxel solido e
inspeccionable -> validacion de seams/provenance -> malla greedy compilada. El modelo manual del
arbol y `tools/diorama_tree/compile_vox.py` fijan el contrato de salida; el runtime solo debe
instanciar modelos compilados. No continuar esta fase ni probar nuevas formulas hasta recibir la
orden expresa del usuario.

## R6: topologia relativa y formas especiales de Rojo

**Estado:** `pending`

**Objetivo:** integrar ledges, stairs, stairwells, round/grouped hulls, stumps,
bookcases, grass y flowers con el mismo orden de claims.

Implementacion:

- [ ] Resolver niveles relativos mediante ledges, stairs, bridges y cursos medidos.
- [ ] Usar elevation solo como identidad/compatibilidad de plano.
- [ ] Rechazar grafos contradictorios en lugar de adivinar.
- [ ] Completar stairs y stairwells en cuatro direcciones.
- [ ] Portar round hull con outline cerrado y fallback aprobado.
- [ ] Portar grouped canopy, stump y bookcase.
- [ ] Integrar grass/flowers animadas desde atlas vivo.
- [ ] Asegurar soporte de objetos en todas las superficies resultantes.
- [ ] Compilar formas/topologia al IR runtime y probar paridad Python/C en esta fase.

Validacion automatica especifica:

- [ ] Fixtures de constraints acumulativas 0 -> 1 -> 2 -> 3.
- [ ] Fixtures de wildcard 0 y retain 15 sin altura metrica.
- [ ] Contradiccion produce fallback y diagnostico.
- [ ] Todas las formas respetan claims y provenance.

**Prueba manual obligatoria R6:**

1. Survey de terraces en Route 115/Mt. Chimney, stairs interiores y bridges.
2. Caminar con player y NPC sobre cada superficie y comprobar soporte visual.
3. Probar round trees, grouped forest, stump, bookcase, grass y flowers.
4. Confirmar que collision y movimiento original no cambian.
5. Guardar/cargar y cruzar conexiones de mapa.

**Gate R6:** el usuario aprueba topologia, formas especiales y soporte. Solo entonces R7.

## R7: edificios identicos al sistema de Rojo

**Estado:** `pending`

**Objetivo:** completar sobre el matcher/claims de R3 los band tables y la medicion de
pixels equivalente a `Buildings.lua`, sustituyendo los prototipos de shell.

Implementacion:

- [ ] Catalogar matrices exactas de familias de edificio.
- [ ] Reutilizar sin duplicar el matcher exacto y prioridad first-claim-wins de R3.
- [ ] Autorizar roof rows, facade, depth mapping, slab, eave y awning.
- [ ] Implementar `topRows`, `claimOnly` y estructuras entre mapas.
- [ ] Componer el footprint completo con provenance.
- [ ] Flood-fill de silueta con seals manuales cuando sea necesario.
- [ ] Medir perfil superior, taper y eave.
- [ ] Detectar panes/windows desde componentes encerrados; si falla, ajustar band/seal o
      rechazar el perfil en vez de mantener una segunda ruta de masks de prop.
- [ ] Generar shell, culling y merge compatible.
- [ ] Crear referencia Python con previews y asserts geometricos.
- [ ] Comparar counts Python/runtime.
- [ ] Eliminar completamente el sistema de edificios prototipo sustituido.
- [ ] Compilar edificios al IR runtime y renderizarlos con paridad Python/C en esta fase.

Validacion automatica especifica:

- [ ] Exact placements y claims para cada template.
- [ ] Simetria/taper/cobertura/penetracion validados cuando apliquen.
- [ ] Igualdad de voxel y shell counts entre referencia y runtime.
- [ ] Template incompleto o ambiguo falla, no genera caja.

**Prueba manual obligatoria R7:**

1. Survey de casa de Littleroot, laboratorio, Pokemon Center, Mart y un edificio grande.
2. Inspeccionar frente, laterales y tejado a 15/35/50/75.
3. Verificar doors, windows, eaves y ausencia de duplicados.
4. Probar todas las colocaciones de cada template aprobado.
5. Probar una estructura que cruza mapas o usa `claimOnly`.

**Gate R7:** el usuario aprueba cada familia de edificio de la vertical slice. Solo
entonces empieza R8.

## R8: meshing, chunks, cache e invalidacion de paridad

**Estado:** `pending`

**Objetivo:** optimizar y cerrar cache/invalidation, asegurando que runtime sigue
consumiendo exactamente la interpretacion headless ya integrada en R2-R7 y que el
equivalente de `ChunkMesher` no introduce decisiones semanticas.

Implementacion:

- [ ] Formalizar frontera classifier/structures/occupancy/mesher.
- [ ] El mesher solo consume IR compilado y snapshot inmutable.
- [ ] Culling por ocupacion real entre celdas, estructuras y chunks.
- [ ] Merge solo con provenance y UV compatibles.
- [ ] Ownership estable de estructuras cross-chunk.
- [ ] Cache separada cuando grass/flowers animadas lo requieran.
- [ ] Invalidar por metatile dinamico, layout, assets y reglas.
- [ ] Cancelar jobs obsoletos sin publicar mallas parciales.
- [ ] Mantener un unico compositor OpenGL y fallback 2D.

Validacion automatica especifica:

- [ ] Igualdad de IR Python/C y counts de shell.
- [ ] Tests de seams, owners, invalidation y jobs obsoletos.
- [ ] Resize/fullscreen/map transition sin use-after-free ni lectura mutable.

**Prueba manual obligatoria R8:**

1. Recorrer conexiones de Route 101/102/104/115 repetidamente.
2. Forzar cambios de metatile, doors, Cut y Rock Smash.
3. Probar resize y fullscreen durante movimiento.
4. Confirmar ausencia de pop-in persistente, caras duplicadas y seams.
5. Confirmar fallback en menus, combate y escenas no soportadas.

**Gate R8:** el usuario aprueba estabilidad runtime y paridad visual con el IR. Solo R9.

## R9: survey global y perfil manual completo

**Estado:** `pending`

**Objetivo:** ejecutar el mismo trabajo de curacion que hace que Rojo parezca fiable,
pero con reglas reutilizables de Emerald y sin editar celdas una a una.

Implementacion por familia:

- [ ] Exteriores generales, cliffs y forests.
- [ ] Ciudades y edificios repetidos.
- [ ] Cuevas y montanas.
- [ ] Interiores domesticos.
- [ ] Pokemon Centers, Marts, Gyms y edificios publicos.
- [ ] Furniture, counters, desks, beds, shelves y consoles.
- [ ] Agua, oceanos, underwater, waterfalls y bridges.
- [ ] Fortree, Sootopolis, Pacifidlog y topologias especiales.
- [ ] Facilities, puzzles, Secret Bases y Battle Frontier.
- [ ] Layouts alternativos, dinamicos y generados.

Para cada pin/template:

- [ ] Registrar sintoma, causa, clase elegida y evidencia.
- [ ] Survey antes/despues en cuatro pitches.
- [ ] Revisar todos los mapas que comparten tileset/template.
- [ ] Revisar un mapa no relacionado tras cambios de detector.
- [ ] Mantener lista explicita de contenido 2D/unsupported.
- [ ] No aceptar coordinate targets para un caso reutilizable.
- [ ] Mantener aprobados los 518 mapas, 441 layouts y 75 tilesets al menos con baseline
      local segura; un metatile ambiguo queda plano, no vuelve unsupported el mapa entero.
- [ ] Definir una allowlist cerrada de escenas sin overworld estable que requieren 2D.
      Cada entrada necesita motivo, owner, evidencia y aprobacion individual del usuario.
- [ ] Un mapa/layout ordinary unsupported bloquea el gate; no existe porcentaje que lo
      convierta automaticamente en aprobado.

**Prueba manual obligatoria R9:**

1. Ejecutar el manifest completo de surveys por lotes.
2. Revisar cada familia visual y firmar su estado.
3. Jugar una ruta completa exterior, una cueva, una ciudad, un interior y una facility.
4. Confirmar que no existe contenido ambiguous convertido silenciosamente.
5. Confirmar que una muestra de reglas de alto blast radius es correcta en todas sus
   apariciones.

**Gate R9:** todos los grupos obligatorios estan `approved`; solo las escenas de la
allowlist aprobada individualmente pueden permanecer 2D. Todos los mapas ordinary tienen
baseline segura y ninguna ambiguedad se presenta como geometria authoritative. Solo R10.

## R10: cierre de paridad y eliminacion final de residuos

**Estado:** `pending`

**Objetivo:** demostrar paridad del sistema, limpiar toda transicion y declarar una unica
arquitectura soportada.

Implementacion:

- [ ] Auditar codigo, schema, datos, tests y docs contra la especificacion.
- [ ] Eliminar flags temporales, migradores sin entrada real y formatos transitorios.
- [ ] Eliminar tooling IA no aprobado y sus dependencias/modelos.
- [ ] Eliminar profiles, masks, pins y templates sin placements o sin aprobacion.
- [ ] Regenerar desde cero todos los artefactos C y catalogos.
- [ ] Verificar que un checkout limpio reproduce hashes y builds.
- [ ] Confirmar que este TODO es el unico roadmap autoritativo del sistema voxel.
- [ ] Crear el TODO posterior de integracion del editor a partir del schema ya aprobado.
- [ ] Documentar limitaciones reales y fallback, sin afirmar inferencia perfecta.

Validacion automatica especifica:

- [ ] Cero codigo muerto o implementaciones duplicadas.
- [ ] Cero generated diff tras regenerar dos veces.
- [ ] Cero regla sin uso no justificada.
- [ ] Test suite completa y ambos builds desde limpio.
- [ ] Smoke tests de launch, input, audio, save/load, resize y fullscreen.

**Prueba manual obligatoria R10:**

1. Ejecutar la vertical slice Littleroot -> Route 101 y la suite de mapas de referencia.
2. Comparar surveys finales con la autoridad 2D y las aprobaciones de cada fase.
3. Probar classic y Diorama desde un checkout limpio.
4. Confirmar gameplay, save/load, audio, menus, combate y fallback.
5. Confirmar que no queda ninguna opcion o dato que active el sistema retirado.

**Gate R10:** el usuario declara explicitamente que el sistema automatico, el sistema de
correcciones manuales y el procedimiento de validacion alcanzan paridad con Rojo. Solo
entonces se puede iniciar la nueva fase de integracion del editor.
