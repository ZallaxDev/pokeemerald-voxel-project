# TODO: paridad completa con el sistema voxel de Pokemon Rojo

## Estado

- Rama de trabajo: `feature/diorama-red-parity`.
- Base historica inspeccionada: `da18b4436`; sus decisiones geometricas no son autoridad.
- Referencia Rojo congelada: `b21fd46ea789a0b8cb99d2c7e0add5a007568a54`.
- Especificacion: `docs/diorama_red_parity.md`.
- Editor visual: fuera de alcance hasta terminar este TODO.
- Fase activa: ninguna; la siguiente es **R2**.
- Ultima fase aprobada manualmente: **R1**.
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
- [ ] Cada correccion se valida de nuevo desde el principio del gate de esa fase.
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

Verificacion automatica minima comun a todas las fases de codigo:

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

**Estado:** `pending`

**Objetivo:** portar la primera etapa de Rojo con precedencia formal y una unica fuente
manual, todavia sin detector de estructuras.

Implementacion:

- [ ] Crear un clasificador headless unico equivalente a `TileShape.lua`.
- [ ] Definir salida `class`, `height`, `artMode`, `pool`, `authored`, `source` y
      confidence/evidence.
- [ ] Aplicar behaviors visualmente fiables antes del fallback conservador.
- [ ] Mantener collision como evidencia, nunca como sinonimo de wall.
- [ ] Implementar pins por tileset/metatile.
- [ ] Implementar condiciones contextuales equivalentes a `when_above` y vecinos
      cardinales sin depender del orden JSON.
- [ ] Implementar alturas nominales por clase.
- [ ] Implementar pools semanticos.
- [ ] Implementar `propGround` manual y su forma automatica pendiente.
- [ ] Derivar grass, flowers, water y animaciones cuando la metadata sea fiable.
- [ ] Dejar arte ambiguo plano y emitir un registro de ambiguedad.
- [ ] Eliminar cualquier clasificador anterior sustituido y sus tests.
- [ ] Compilar la decision final a C; runtime no reinterpreta precedencia.
- [ ] Publicar overlay runtime `class/source/evidence` desde datos compilados y snapshot,
      sin leer globals mutables.

Spike IA local completo opcional, no autoritativo:

- [ ] Registrar decision `not-run` o `run` sin bloquear R2.
- [ ] Si se ejecuta, definir antes benchmark, precision minima y ahorro humano minimo.
- [ ] Evaluar solo offline/local embeddings, segmentacion y opcionalmente un VLM.
- [ ] Registrar licencia, modelo, hash, seed, prompt, parametros, recursos y tiempos.
- [ ] Excluir pesos y outputs de Git; no crear dependencia de build/runtime.
- [ ] Conservar solo una herramienta que supere los umbrales predeclarados.
- [ ] Si no mejora el flujo, eliminar codigo, dependencias, pesos y documentacion del
      spike en esta misma fase.
- [ ] Ninguna sugerencia IA entra en JSON authoritative sin revision humana.

Validacion automatica especifica:

- [ ] Fixtures de precedencia: context > pin general > behavior fiable > fallback.
- [ ] Fixtures de tile reutilizado con dos contextos.
- [ ] Cero celda clasificada wall solamente por collision.
- [ ] Paridad Python/C de todas las salidas del clasificador.

**Prueba manual obligatoria R2:**

1. Ejecutar survey en Route 101, Route 115 y Granite Cave B1F.
2. Activar overlay runtime de `class/source` sin usar el editor.
3. Confirmar ground, water, ledges y stairs conocidos.
4. Confirmar que arboles, edificios y roca ambigua permanecen planos si no tienen pin.
5. Seleccionar y commitear un pin realmente authoritative, verificar todas sus
   apariciones y su blast radius; los pins temporales solo existen en fixtures headless.

**Gate R2:** el usuario aprueba precedencia, fallback y formato de correccion manual.
Solo entonces empieza R3.

## R3: reclamacion, conectividad y orden `Structures`

**Estado:** `pending`

**Objetivo:** portar el esqueleto de `Structures.lua` sobre layouts completos, sin
voxelizar props ni medir alturas todavia.

Implementacion:

- [ ] Crear IR de candidatos con owner, cells, pixels, bbox, class y evidence.
- [ ] Implementar aqui el matcher minimo de matrices exactas necesario para que
      edificios/estructuras queden reclamados antes de props y volumenes; R7 construira
      su shell completo.
- [ ] Implementar claim masks y `first claim wins`.
- [ ] Implementar prioridad determinista entre template, authored special, prop
      candidate y generic volume.
- [ ] Detectar tiles void por fuente completamente transparente/negra, como Rojo, y
      combinarlo con metadata Emerald; tratar el ring exterior como problema separado.
- [ ] Implementar folding de puertas solo bajo reglas probadas.
- [ ] Flood-fill de regiones visuales no reclamadas.
- [ ] Separar conectividad de mapa y conectividad real de pixels.
- [ ] Impedir fusion entre pools distintos.
- [ ] Emitir overlays headless de region/claim/source.
- [ ] Eliminar region builders anteriores sustituidos.
- [ ] Compilar claims/owners al IR runtime y renderizar diagnostico con el mesher actual.

Validacion automatica especifica:

- [ ] Claims independientes del orden de iteracion.
- [ ] Ninguna celda reclamada llega a un detector posterior.
- [ ] Componentes iguales con distinto pool permanecen separados.
- [ ] Conectividad no cruza layouts o void no conectado.

**Prueba manual obligatoria R3:**

1. Survey de Littleroot, Route 115, Fortree y un Pokemon Center.
2. Revisar overlays de claims desde los mismos spots.
3. Confirmar que edificio, vegetacion, props y suelo forman candidatos separados.
4. Confirmar que puertas walkable no rompen fachadas ni desaparecen sin reemplazo.
5. Mover la camara y confirmar que los componentes no cambian.

**Gate R3:** el usuario aprueba boundaries y orden de claims. Solo entonces empieza R4.

## R4: props automaticos y cutouts por pixel

**Estado:** `pending`

**Objetivo:** reproducir `extractObjects`, billboards forzados, reliefs y soporte sobre
objetos sin convertir arte ambiguo en cajas.

Implementacion:

- [ ] Componer region con apron y provenance.
- [ ] Estimar fondo desde suelo vecino compatible.
- [ ] Flood-fill de fondo y separar componentes foreground.
- [ ] Implementar thresholds configurables y justificados por fixtures Emerald.
- [ ] Rechazar regiones altas/repetitivas que no sean props.
- [ ] Implementar los modos de outline/clase de Rojo cuando el flood sea ambiguo; una
      prop edge-to-edge debe cambiar de clase o quedar sin soportar, no usar mask extra.
- [ ] Generar un prisma por pixel con texel original.
- [ ] Apoyar componentes en sus pies y sobre soportes authored.
- [ ] Implementar `propGround` por votacion y override manual.
- [ ] Implementar `relief` horizontal y pools separados.
- [ ] No modificar sprites de personajes, que siguen como billboards.
- [ ] Compilar cutouts/reliefs al IR runtime y demostrar paridad Python/C en esta fase.

Validacion automatica especifica:

- [ ] Masks golden pixel a pixel.
- [ ] Cada voxel visible tiene provenance valida.
- [ ] Ningun foreground perdido o fondo incluido en fixtures aprobados.
- [ ] Prop sobre mesa conserva altura y no pinta suelo a traves del soporte.

**Prueba manual obligatoria R4:**

1. Survey multi-pitch de signs, plants, TVs, stools, cut tree y boulder.
2. Verificar silueta pixel a pixel contra `flat`.
3. Verificar objetos blancos/grises encerrados por outline.
4. Verificar un caso edge-to-edge que debe rechazarse y quedar plano/perfilado.
5. Probar otro mapa que comparta cada tileset modificado.

**Gate R4:** el usuario aprueba masks, soporte y ground replacement. Solo entonces R5.

## R5: volumenes genericos y alturas repeat-aware

**Estado:** `pending`

**Objetivo:** portar el detector automatico que falta actualmente: medir el dibujo
ensamblado y no repetir IDs de metatile como sustituto del arte.

Implementacion:

- [ ] Medir runs norte-sur sobre componentes estructurales completos, igual que Rojo.
- [ ] Detectar repeticion con una firma Emerald equivalente al tile ID de Rojo: tile,
      capa, paleta, flips y provenance. Comparar pixels solo como evidencia adicional.
- [ ] Distinguir extent dibujado de secuencia repetida.
- [ ] Aplicar limite fisico equivalente a seis filas de 8 px, adaptado y justificado a
      bandas de arte Emerald, no copiado como tres metatiles por defecto.
- [ ] Implementar consenso de region y registrar conflictos.
- [ ] Analizar ambos ejes cuando el arte Emerald lo requiera.
- [ ] Conservar una banda source distinta por tramo de cara.
- [ ] Detectar top rows diferentes como candidato de roof solo en exterior.
- [ ] No procesar celdas reclamadas, passable decorativas ni clases authored.
- [ ] Emitir confidence y dejar plano cualquier candidato contradictorio.
- [ ] Introducir el detector nuevo en un unico modulo; el detector viejo ya fue retirado
      en R0 y no puede reaparecer como fallback oculto.
- [ ] Compilar volumenes al IR runtime y renderizarlos en esta fase.

Validacion automatica especifica:

- [ ] Bosque largo produce arboles repetidos, no monolito.
- [ ] Fachada de varias bandas conserva altura dibujada y source por banda.
- [ ] Firmas de fuente equivalentes forman repeticion aunque procedan de metatiles
      distintos.
- [ ] Un metatile ID igual con capa/paleta/provenance distinta no se fuerza a repetir.
- [ ] Resultado independiente de viewport y orden.

**Prueba manual obligatoria R5:**

1. Survey de Route 115, Mt. Chimney, Jagged Pass, Fortree y Granite Cave B1F.
2. Comparar cada cliff/forest/wall con su extent en `flat`.
3. Confirmar que no existen torres por bordes repetidos.
4. Confirmar que muebles y edificios reclamados no entran en volumen generico.
5. Probar pitches 15/35/50/75 y volver a cada mapa tras una transicion.

**Gate R5:** el usuario aprueba la deteccion automatica general de alturas. Este gate es
el cierre real de la deteccion automatica de terreno. Solo entonces empieza R6.

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
