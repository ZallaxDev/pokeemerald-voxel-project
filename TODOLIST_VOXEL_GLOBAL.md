# TODO global: Pokemon Emerald voxel fiel al arte original

Este documento sustituye el alcance limitado de las fases 10 y 12 de
`TODOLIST.md`. El objetivo ya no es corregir manualmente Villa Raiz y Ruta 101,
sino portar a Pokemon Emerald el sistema completo de interpretacion voxel de
`../DramaticShapeVoxelMod/` y cubrir todos los mapas, layouts y tilesets del
juego.

La referencia se usa para copiar principios, categorias, flujos de autorado y
criterios de calidad. No se copian IDs, coordenadas, umbrales de color, formatos,
APIs ni supuestos de Pokemon Rojo.

## Alcance global

- [ ] Cubrir los 518 mapas declarados en `data/maps/map_groups.json`.
- [ ] Cubrir los 441 layouts declarados en `data/layouts/layouts.json`.
- [ ] Cubrir los 406 layouts referenciados directamente por mapas.
- [ ] Clasificar los 35 layouts alternativos, dinamicos, limpios o no referenciados.
- [ ] Cubrir los 75 tilesets logicos: 3 primarios y 72 secundarios.
- [ ] Cubrir los 324.579 bloques de mapa sin crear reglas por coordenada para casos reutilizables.
- [ ] Cubrir exteriores, interiores, cuevas, rutas oceanicas, zonas submarinas, Secret Bases, instalaciones, Battle Frontier y mapas especiales.
- [ ] Mantener fallback 2D explicito hasta que cada familia visual tenga cobertura aprobada.

Inventario inicial:

| Recurso | Total | Cobertura curada actual |
|---|---:|---:|
| Mapas | 518 | 7 |
| Layouts | 441 | 7 |
| Tilesets logicos | 75 | 5 registrados |
| Tilesets con clasificacion real | 75 | 1 |
| Plantillas de edificio | Por catalogar | 2 prototipos |
| Overrides de coordenada | Deben ser excepcionales | 63 heredados |

## Principios no negociables

- [ ] El juego original sigue siendo la unica autoridad para movimiento, colision, elevacion de gameplay, scripts, eventos, warps, encuentros y guardados.
- [ ] El renderer solo observa snapshots inmutables y tablas generadas.
- [ ] `DIORAMA=0` conserva el renderer clasico completo.
- [ ] Menus, combates, escenas especiales, errores y contenido no aprobado conservan fallback 2D.
- [ ] La build de escritorio permanece en 32 bits.
- [ ] Los JSON bajo `data/diorama/` son la unica fuente editable de reglas.
- [ ] Los archivos C generados no se editan manualmente.
- [ ] Se autoriza significado y dimensiones gruesas; se deriva automaticamente toda geometria medible.
- [ ] Ningun objeto ambiguo se convierte mediante una conjetura silenciosa.
- [ ] Una regla invalida falla con contexto de tileset, mapa, plantilla y pixel.
- [ ] No se estira un tile o metatile sobre una superficie mayor que su banda de arte declarada.
- [ ] Toda cara visible debe conservar procedencia hacia texels del atlas vivo o una muestra de relleno declarada del mismo arte.
- [ ] La geometria se decide por ocupacion real, no solo por pertenencia a un `structureId`.
- [ ] Cada estructura reclamada excluye sus celdas de detectores posteriores.
- [ ] El juego y `map_editor` consumen el mismo modelo compilado; el frontend no reimplementa reglas.
- [ ] Las reglas reutilizables viven en tilesets, perfiles y arquetipos; los overrides de coordenada quedan reservados para excepciones demostradas.

## Frontera entre autorado y automatizacion

### Se autoriza manualmente

- [ ] Clase semantica de arte ambiguo o reutilizado.
- [ ] Arquetipo de geometria: suelo, volumen, tejado, arbol, poste, cartel, mesa, consola, relieve, etc.
- [ ] Altura, profundidad y grosor nominales cuando el dibujo no los determina.
- [ ] Matriz exacta de metatiles de cada edificio o estructura multicelda.
- [ ] Separacion entre bandas de tejado, fachada, alero, toldo y zocalo.
- [ ] Filas traseras, delanteras y ciclo de repeticion del tejado.
- [ ] Grosor de cubierta y extensiones de alero.
- [ ] Bordes sellados para flood fill.
- [ ] Prioridad entre plantillas solapables.
- [ ] Fragmentos `topRows`, `claimOnly` o equivalentes para estructuras repartidas entre mapas.
- [ ] Agrupacion de copas de arbol, setos, vallas y estructuras multicelda.
- [ ] Reemplazo de suelo explicito cuando la votacion automatica no sea fiable.
- [ ] Reglas condicionales para arte cuyo significado depende de vecinos o contexto.
- [ ] Marcado explicito de contenido no soportado que deba usar 2D.

### Se deriva automaticamente

- [ ] Todas las colocaciones de una plantilla mediante coincidencia exacta.
- [ ] Composicion de metatiles, capas, flips y paletas.
- [ ] Siluetas y fondos conectados.
- [ ] Perfil y pendiente de tejados.
- [ ] Componentes de ventanas, puertas y paneles.
- [ ] Ocupacion voxel y shell exterior.
- [ ] Eliminacion de caras ocultas.
- [ ] Fusion de runs compatibles con procedencia UV.
- [ ] Componentes conexos de estructuras y props.
- [ ] Repeticiones y alturas medibles de volumenes genericos.
- [ ] Suelo de reemplazo por votacion o correspondencia de arte.
- [ ] Invalidacion, cache, ownership de chunks y cancelacion de trabajos obsoletos.
- [ ] Informes de cobertura, conflictos, colocaciones y contenido ambiguo.

## Fase G0: retirar el prototipo incompatible

**Gate:** ninguna regla antigua puede mezclarse con el nuevo modelo volumetrico.

- [x] Marcar las implementaciones actuales de edificio perfilado y cutout simple como prototipo sustituible, no como base aprobada.
- [x] Eliminar los 59 overrides de edificio de `data/diorama/maps/littleroot_town.json`.
- [x] Eliminar los 4 overrides de ledge de `data/diorama/maps/route101.json` cuando exista su regla reutilizable equivalente.
- [x] Eliminar los `eventRules.sign` duplicados por mapa cuando exista el preset global de carteles.
- [x] Eliminar la prioridad especial que mezcla metadata de edificio con una regla visual ganadora incompatible.
- [x] Eliminar perfiles aceptados pese a ser ambiguos, como el perfil plano de 47 pixels del laboratorio.
- [x] Eliminar tests que aprueban alturas patologicas o geometria rectangular sin mascara.
- [x] Eliminar la dependencia de `faces.top = full` para superficies recortadas de edificios.
- [x] Eliminar reglas de arbol basadas exclusivamente en planos X con `baseMetatile` fijo.
- [x] Conservar temporalmente fallback plano o 2D donde la sustitucion aun no exista.
- [x] Documentar cada regla retirada y su reemplazo previsto.
- [x] Confirmar que gameplay, colisiones y mapas binarios no cambian.

## Fase G1: inventario y catalogos globales

**Gate:** todo arte usado por cualquier layout puede inspeccionarse y clasificarse.

- [x] Generar automaticamente el catalogo de los 518 mapas y sus layouts efectivos.
- [x] Generar el catalogo de los 441 layouts, incluidas variantes dinamicas.
- [x] Generar el catalogo de los 75 tilesets desde los headers reales, sin lista hardcodeada.
- [x] Resolver tilesets logicos que comparten metatiles pero usan graficos distintos.
- [x] Crear contact sheets de todos los metatiles con ID local/global, behavior, layer type y uso real.
- [x] Crear contact sheets separados para `base`, `foreground` y `full`.
- [x] Mostrar flips, paleta, tiles primarios/secundarios y slots animados de cada subtile.
- [x] Generar una matriz layout/metatile -> mapas/coordenadas de uso, enlazada desde cada tileset.
- [x] Generar frecuencia de cada metatile y matriz de vecinos cardinales.
- [x] Generar inventario de eventos: carteles, puertas, Cut, Rock Smash, berry trees, decoraciones y obstáculos dinamicos.
- [x] Generar inventario de behaviors de agua, grass, ledges, puentes, escaleras, warps y superficies especiales.
- [x] Detectar matrices repetidas de metatiles candidatas a edificios y estructuras multicelda.
- [x] Deduplicar candidatos por tileset y matriz exacta.
- [x] Generar catalogo HTML/JSON navegable con imagen, mascara, usos y contexto de colision/elevacion.
- [x] No exigir una lista manual de 518 mapas para volver a ejecutar el inventario.

Artefactos obligatorios:

- [x] `build/diorama_catalog/maps.json`.
- [x] `build/diorama_catalog/layouts.json`.
- [x] `build/diorama_catalog/tilesets.json`.
- [x] Contact sheets por pareja logica de tilesets usada por layouts.
- [x] Informe de cobertura y reglas sin uso.
- [x] Informe de arte ambiguo reutilizado con contextos distintos.
- [x] Informe de layouts alternativos y cambios en runtime.

## Fase G2: compositor Emerald unico y procedencia de pixel

**Gate:** compilador, editor y herramientas producen exactamente la misma imagen y procedencia.

- [x] Extraer un modulo Python comun para decodificar PNG indexado, paletas GBA, tiles y metatiles.
- [x] Resolver correctamente metatiles primarios y secundarios con offset global `0x200`.
- [x] Resolver los ocho subtiles, flips H/V y paleta por subtile.
- [x] Componer `base`, `foreground` y `full` de forma separada.
- [x] Tratar indice de paleta cero como transparencia de capa.
- [x] Conservar por pixel metatile, capa, subtile, tile, paleta, color index, UV local y flips.
- [x] Detectar referencias a slots animados ausentes del PNG estatico y fallar o usar frames extraidos declarados.
- [x] Reutilizar el modulo desde compilador, catalogador y backend del editor.
- [x] Eliminar el segundo decoder independiente de `map_editor/server.py`.
- [x] Comparar el compositor Python contra `DioramaMetatile_ComposeLayer()` en casos golden.
- [x] Probar todos los formatos y tilesets, no solo General/Petalburg.

## Fase G3: nuevo esquema de perfiles manuales

**Gate:** el JSON puede expresar todas las familias usadas por Rojo adaptadas a Emerald.

- [x] Sustituir el registro hardcodeado de tilesets por catalogos generados.
- [x] Definir version nueva del esquema y migracion explicita; no interpretar silenciosamente JSON antiguo.
- [x] Definir reglas globales por behavior cuando el behavior tenga significado visual fiable.
- [x] Definir pins por tileset/metatile para significado visual manual.
- [x] Definir reglas condicionales por vecinos, layer type, behavior, elevacion, map type o evento.
- [x] Definir patrones exactos multicelda con prioridad y claim mask.
- [x] Definir presets globales para eventos y objetos dinamicos.
- [x] Definir reemplazo de suelo automatico y override manual.
- [x] Definir marcas `unsupported` y motivo de fallback.
- [x] Definir pools semanticos separados para evitar que objetos distintos se fusionen al tocarse.

Arquetipos minimos:

- [x] `ground` y `void`.
- [x] `water`, `shallow-water`, `waterfall`, `current` y `hot-spring`.
- [x] `ledge`, `cliff`, `mound` y `wall-volume`.
- [x] `bridge`, `deck`, `rail` y `support`.
- [x] `stairs-n/s/e/w` y variantes descendentes.
- [x] `roof`, `top-slab` y `awning`.
- [x] `building` y `claim-only`.
- [x] `billboard`, `cutout`, `console`, `signpost` y `post`.
- [x] `counter`, `table`, `desk`, `bed` y `bookcase`.
- [x] `relief`, `round-hull`, `grouped-hull` y `stump`.
- [x] `tree`, `forest-wall`, `shrub`, `hedge`, `rock` y `boulder`.
- [x] `grass`, `flower` y `animated-cutout`.

- [x] Validar claves desconocidas, rangos, referencias, dimensiones, prioridad y solapamientos.
- [x] Incluir todos los perfiles, mascaras y muestras en el hash determinista.
- [x] Rechazar reglas muertas, IDs fuera de tileset y patrones sin colocaciones salvo que se marquen como permitidos.

## Fase G4: modelo de ocupacion y shell comun

**Gate:** la malla se genera desde ocupacion real a resolucion de pixel, no desde cajas por celda.

- [x] Definir una representacion intermedia de ocupacion `(x,y,z)` por spans, no una matriz densa ilimitada.
- [x] Permitir resolucion de un pixel de arte igual a `1/16` de celda Emerald.
- [x] Asociar a cada voxel/span una procedencia de material valida.
- [x] Generar shell preguntando por ocupacion en los seis vecinos.
- [x] Conservar caras entre componentes cuando sus intervalos no se solapen realmente.
- [x] Eliminar caras internas entre celdas, chunks y partes de una misma estructura.
- [x] Implementar ownership estable para geometria que cruza chunks.
- [x] Fusionar caras solo si son coplanares, compatibles y UV-contiguas o repetibles.
- [x] No fusionar atravesando flips, capas, paletas, slots animados o seams no contiguos.
- [x] Implementar UV a texel con inset seguro contra bleeding.
- [x] Generar bounds desde toda cara, incluidos aleros, intrados y geometria bajo suelo.
- [x] Dimensionar capacidad desde el peor caso sin depender de que la fusion funcione.
- [x] Compartir el modelo intermedio con el editor y las herramientas headless.
- [x] Mantener un backend C eficiente para runtime y una referencia Python determinista para pruebas.

## Fase G5: terreno, elevacion y superficies multicapa

**Gate:** todo mapa puede representar correctamente sus planos de terreno antes de añadir decoracion.

- [x] Revisar la decision actual de ignorar elevation raw y modelar su significado real por contexto.
- [x] Separar elevation de gameplay de altura visual sin perder puentes o planos superpuestos.
- [x] Representar varias superficies en una misma coordenada para puentes y pasos inferiores.
- [x] Generar caras laterales solo donde cambie la ocupacion de terreno.
- [ ] Clasificar suelo, caminos, arena, ceniza, roca, pavimento, madera, alfombras y void por tileset.
- [x] Implementar materiales de borde, esquina, union y transicion sin estirar arte.
- [ ] Implementar acantilados con top, cara, base, esquinas y transiciones.
- [x] Inferir volumenes locales solo sobre arte bloqueado sin regla explicita, sin elevar regiones transitables.
- [x] Medir runs norte-sur con repeticion local, limite fisico de tres metatiles y rechazo de runs cortados por el snapshot.
- [x] Reconstruir bandas laterales desde las filas propietarias del run sin estirar una unica textura.
- [x] Implementar ledges direccionales y diagonales desde behaviors y arte local.
- [x] Implementar rampas y escaleras en cuatro direcciones.
- [x] Implementar stairwells descendentes y aperturas oscuras sin convertirlos en cajas.
- [ ] Validar visualmente y perfilar excepciones de Fortree, Sootopolis, cuevas, Mt. Chimney, Jagged Pass y elevaciones especiales.

## Fase G6: agua, orillas, puentes y terreno especial

**Gate:** agua y superficies superpuestas son completas y no dependen de hacks por mapa.

- [ ] Clasificar todos los behaviors de agua usados globalmente, incluidos los 3.570 bloques exteriores actualmente omitidos.
- [ ] Diferenciar oceano, estanque, agua profunda, shallow water, puddle, corriente, cascada, hot spring y underwater.
- [ ] Construir labios y caras de orilla sin dejar grietas.
- [ ] Construir cascadas como caras verticales animadas.
- [ ] Construir puentes con deck, grosor, barandillas, soportes, agua/terreno inferior y orden de objetos.
- [ ] Mantener animaciones y paletas mediante atlas vivo sin remeshing innecesario.
- [ ] Integrar reflejos y Surf con las superficies finales.
- [ ] Probar rutas oceanicas, Pacifidlog, Sootopolis, zonas submarinas y puentes de Route 119/120.

## Fase G7: reemplazo de suelo y claim masks

**Gate:** retirar un dibujo plano nunca deja vacio ni eleva su fondo accidentalmente.

- [ ] Implementar claim mask por estructura, prop y edificio.
- [ ] Impedir que detectores posteriores vuelvan a generar las celdas reclamadas.
- [ ] Implementar votacion de suelo compatible alrededor del perimetro de edificios.
- [ ] Implementar votacion alrededor de clusters de props.
- [ ] Implementar correspondencia de pixels no enmascarados para round hulls.
- [ ] Excluir agua, void, flores y otros props incompatibles de la votacion.
- [ ] Respetar props apoyados en muebles sin pintar suelo a traves del soporte.
- [ ] Permitir `propGround` manual cuando la votacion sea ambigua.
- [ ] Generar overlay de diagnostico del suelo elegido y su procedencia.
- [ ] Probar determinismo ante orden diferente de catalogacion.

## Fase G8: exteriores, arboles y vegetacion

**Gate:** todos los exteriores usan volumenes coherentes en vez de planos cruzados genericos.

### Arboles aislados

- [ ] Catalogar metatiles y eventos de arbol en todos los tilesets exteriores.
- [ ] Separar tronco, copa, sombra pintada y suelo.
- [ ] Derivar la silueta de copa mediante mascara y contorno apropiados.
- [ ] Generar round hull o perfil voxel por pixel en lugar de una caja o plano.
- [ ] Reutilizar una plantilla por firma de arte.
- [ ] Generar laterales desde texels interiores o muestras declaradas de la copa/tronco.
- [ ] Cubrir variantes regionales y de estaciones/animacion existentes.

### Bosques y muros vegetales

- [ ] Tratar Petalburg Woods, Safari, Route 119/120/121 y masas densas como estructuras conectadas.
- [ ] Catalogar piezas interior, exterior, esquina, entrada, final y transicion.
- [ ] Agrupar copas multicelda antes de generar hulls.
- [ ] Evitar cilindros independientes por cada cuarto de copa.
- [ ] Mantener suelo y objetos visibles bajo entradas o huecos reales.

### Arboles dinamicos y berry trees

- [ ] Generar Cut trees desde snapshots de objetos, no como terreno estatico.
- [ ] Invalidar solo su region al desaparecer por Cut.
- [ ] Generar berry trees por etapa visual y visibilidad del objeto.
- [ ] Mantener soil separado y plano.

### Arbustos, setos, rocas y tocones

- [ ] Definir `shrub`, `hedge`, `rock`, `boulder`, `breakable-rock` y `stump` por tileset/evento.
- [ ] Usar hulls recortados y caps declarados.
- [ ] Generar Rock Smash desde snapshots dinamicos.
- [ ] Separar roca aislada de rock wall/acantilado.

### Hierba y flores

- [ ] Generar tall grass solo donde el behavior confirme hierba real.
- [ ] Separar base plana y mechones verticales.
- [ ] Dibujar mechones sur despues del personaje para ocluir los pies.
- [ ] Generar mascara union de todos los frames para flores animadas.
- [ ] Mantener el frame visible desde el atlas vivo.
- [ ] Catalogar flower beds y flores aisladas por tileset.

## Fase G9: props, carteles, vallas y objetos exteriores

**Gate:** los props exteriores tienen silueta, profundidad y suelo correctos.

- [ ] Crear preset global de carteles desde los 533 `bg_events` de tipo sign.
- [ ] Cubrir las variantes visuales por tileset sin repetir coordenadas por mapa.
- [ ] Definir profundidad y orientacion manual cuando el evento no lo indique.
- [ ] Implementar `post` por celda para postes, tumbas y vallas repetidas.
- [ ] Implementar piezas de valla horizontal, vertical, esquina, puerta y terminacion.
- [ ] Evitar que vallas conectadas se conviertan en un unico billboard profundo.
- [ ] Detectar props recortables desde contexto de suelo y mascara.
- [ ] Separar componentes conexos de objetos independientes dentro de un cluster.
- [ ] Implementar `console`/`cutout` con componente principal para arte contaminado por bordes vecinos.
- [ ] Definir profundidad nominal manual por arquetipo.
- [ ] Reutilizar el texel de cada pixel para front, back, top, bottom y laterales con shading direccional.

## Fase G10: interiores y mobiliario completo

**Gate:** todos los tilesets interiores tienen perfiles manuales y geometria legible.

- [ ] Catalogar los 208 layouts interiores unicos y layouts compartidos.
- [ ] Clasificar paredes, suelo, void y frente ocultable por tileset.
- [ ] Implementar ocultacion de paredes frontales sin alterar colision.
- [ ] Definir perfiles de camara por familia de interior.
- [ ] Autorizar manualmente beds, counters, tables, desks, bookcases, reliefs y walls.
- [ ] Implementar folding por bandas de 8/16 pixels sin estirar.
- [ ] Implementar bookcases como rangos verticales de poca profundidad con cap.
- [ ] Implementar props apoyados sobre mesas y mostradores.
- [ ] Implementar monitores, televisores y maquinas como billboards con profundidad declarada.
- [ ] Implementar consolas top-down como relief cuando corresponda.
- [ ] Implementar registros y maquinas recortadas conservando el componente principal.
- [ ] Mantener rugs, sombras pintadas y marcas como `ground`.
- [ ] Cubrir casas, Centros Pokemon, Marts, gimnasios, laboratorios, barcos, instalaciones, Battle Frontier y Secret Bases.

### Secret Bases y decoraciones dinamicas

- [ ] Publicar en el snapshot la colocacion, tipo, posicion, orientacion y estado visible de cada decoracion instalada.
- [ ] Mantener los datos de decoracion como valores inmutables sin exponer punteros del save block al hilo grafico.
- [ ] Catalogar todas las familias de decoracion: desks, chairs, plants, ornaments, mats, posters, dolls, cushions y large dolls.
- [ ] Reutilizar los arquetipos de furniture, billboard, relief, post y ground segun cada categoria.
- [ ] Respetar decoraciones que bloquean, se pisan, se apoyan en muebles o se colocan sobre pared sin decidir su gameplay.
- [ ] Invalidar claims, suelo y chunks afectados al colocar, quitar o mover una decoracion.
- [ ] Cubrir los seis tilesets visuales de Secret Base que comparten metatiles pero cambian graficos.
- [ ] Probar round trip de guardado/carga y confirmar que el renderer nunca escribe decoraciones.

## Fase G11: catalogo global de edificios

**Gate:** cada dibujo de edificio del juego tiene plantilla o fallback explicito.

- [ ] Detectar y deduplicar candidatos de edificio en todos los layouts exteriores.
- [ ] Crear catalogo equivalente a `assets/docs/buildings/` de la referencia.
- [ ] Registrar matriz exacta, tileset, imagen compuesta y todas las colocaciones.
- [ ] Registrar collision/elevation/eventos dentro del footprint.
- [ ] Autorizar `roofRows` por plantilla en pixels de arte, no en filas de mapa ambiguas.
- [ ] Autorizar `roofBack`, `roofFront` y `roofCycle` adaptados a metatiles Emerald.
- [ ] Autorizar `slab`, aleros por lado, awnings/ledges y zocalos.
- [ ] Autorizar `seal` por borde sin convertir el seal en pixel visible.
- [ ] Autorizar prioridad, restricciones contextuales y firmas adicionales.
- [ ] Soportar edificios repartidos entre mapas o layouts con `topRows` y `claimOnly`.
- [ ] Marcar explicitamente dibujos multi-estructura que necesiten otro arquetipo.

## Fase G12: generador volumetrico de edificios

**Gate:** cada edificio se construye como volumen voxel cerrado y no como heightfield por celdas.

- [ ] Recomponer la imagen completa de cada plantilla desde metatiles.
- [ ] Separar fondo exterior mediante flood fill y mascara editable.
- [ ] No usar `full` como mascara de ocupacion cuando `base` contiene suelo.
- [ ] Mantener mascara de ocupacion separada del material visible.
- [ ] Medir `top[x]` dentro de la banda de tejado.
- [ ] Convertir `top[x]` al sistema local usando altura de pared y slab; no usarlo como altura absoluta.
- [ ] Mantener ausentes las columnas sin pixels de tejado.
- [ ] Construir profundidad de tejado con bandas back/front/cycle.
- [ ] Construir shell de grosor constante y aleros declarados.
- [ ] Extruir fachada a traves de la profundidad del footprint.
- [ ] Dejar que el tejado recorte la pared en sus intersecciones.
- [ ] Duplicar la fila inferior interior cuando sea necesario para impedir paredes flotantes.
- [ ] Detectar ventanas, puertas y paneles por componentes encerrados.
- [ ] Retirar solo la capa frontal y conservar backing para crear entrantes.
- [ ] Implementar jambas y marcos sin agujeros completos salvo declaracion.
- [ ] Implementar de-outline interior para caras laterales de pared.
- [ ] Seleccionar texels representativos del propio edificio para fascia, rim e intrados.
- [ ] Generar ocupacion, shell, culling y fusion UV-compatible.
- [ ] Construir una sola malla local por plantilla y estamparla en todas sus colocaciones.
- [ ] Sustituir el suelo del footprint por votacion perimetral.
- [ ] Producir estadisticas de voxels, shell, quads, memoria y colocaciones.

## Fase G13: editor de perfiles y survey visual

**Gate:** todo autorado manual puede realizarse y validarse sin editar C ni inspeccionar IDs a ciegas.

- [ ] Ampliar `map_editor` a navegador global de mapas, layouts y tilesets.
- [ ] Mostrar contact sheet con búsqueda por ID, behavior, layer y uso.
- [ ] Permitir asignar arquetipo y dimensiones nominales a metatiles.
- [ ] Permitir crear reglas condicionales y pools semanticos.
- [ ] Permitir seleccionar matrices exactas de edificios en el mapa.
- [ ] Mostrar imagen compuesta completa y procedencia por pixel.
- [ ] Permitir editar bandas roof/body/back/front/cycle, slab, eaves, ledge y seal.
- [ ] Permitir pintar mascaras por spans y componentes de pane/recess.
- [ ] Mostrar ocupacion voxel, shell, caras descartadas y runs fusionados.
- [ ] Mostrar ground replacement y motivo de seleccion.
- [ ] Mostrar conflictos de claims y prioridad de plantillas.
- [ ] Comparar preview 2D original y vista voxel desde angulos reproducibles.
- [ ] Validar con el mismo compilador antes de guardar.
- [ ] Limitar escrituras a `data/diorama/`.
- [ ] Conservar campos desconocidos solo si la version del esquema los admite expresamente.

## Fase G14: cache, chunks e invalidacion global

**Gate:** la cobertura completa no introduce popping, duplicados ni memoria sin limite.

- [ ] Cachear atlas/metatiles decodificados por tileset y generacion.
- [ ] Cachear analisis de estructura por mapa/layout y revision.
- [ ] Cachear modelos de edificio por plantilla.
- [ ] Cachear hulls, props, grass y flowers por firma de arte.
- [ ] Mantener meshes CPU/GPU separados para terreno, props y vegetacion dependiente del personaje.
- [ ] Cancelar trabajos obsoletos por generaciones.
- [ ] Mantener mesh anterior durante refresh local.
- [ ] Priorizar mapa actual y precargar conexiones visibles.
- [ ] Limitar residencia y memoria durante viajes largos.
- [ ] Incluir reglas, mapa, claims y vecinos reales en firmas de chunk.
- [ ] No reconstruir geometria por cambios de paleta o frame animado si la mascara union no cambia.
- [ ] Invalidar Cut, Rock Smash, puertas, decoraciones, puzzles y cambios de layout.
- [ ] Verificar ownership estable en seams y estructuras que cruzan chunks.

## Fase G15: rollout por familias de tileset

**Gate:** cada familia completa se aprueba antes de marcar sus mapas como soportados.

- [ ] Familia 1: `gTileset_General` y los 20 secundarios usados por exteriores.
- [ ] Familia 2: ciudades y pueblos.
- [ ] Familia 3: rutas terrestres y bosques.
- [ ] Familia 4: oceano, costas y mapas submarinos.
- [ ] Familia 5: cuevas, tuneles, montanas, lava y hielo.
- [ ] Familia 6: interiores genericos compartidos.
- [ ] Familia 7: casas y edificios de historia.
- [ ] Familia 8: gimnasios y puzzles.
- [ ] Familia 9: barcos, museos, instalaciones y zonas especiales.
- [ ] Familia 10: Battle Frontier y Battle Tents.
- [ ] Familia 11: Secret Bases y decoraciones dinamicas.
- [ ] Familia 12: layouts alternativos, limpios, bloqueados y eventos legendarios.
- [ ] Familia 13: layouts no referenciados pero compilados en el juego.
- [ ] No cerrar una familia con reglas sin uso, ambiguas o pendientes de fallback sin documentar.

## Fase G16: auditoria mapa por mapa

**Gate:** los 518 mapas tienen estado, evidencia y resultado reproducible.

- [ ] Generar matriz de 518 filas con mapa, layout actual/alternativos, tilesets, clase y estado.
- [ ] Registrar `unsupported`, `generic`, `curated`, `visual-pass` y `approved`.
- [ ] Auditar todos los mapas compartiendo un layout una sola vez y validar excepciones por mapa.
- [ ] Auditar conexiones y edificios visibles antes de cruzar bordes.
- [ ] Auditar warps, puertas y cambios de layout.
- [ ] Auditar objetos dinamicos y eventos visuales.
- [ ] Auditar clima, paletas, animaciones y reflejos.
- [ ] Auditar orden de jugador/NPC con grass, puentes, muebles y paredes.
- [ ] Registrar capturas locales desde angulos fijos sin versionar assets del juego.
- [ ] No declarar cobertura global hasta que no queden filas sin clasificar.

## Fase G17: pruebas automatizadas obligatorias

### Compositor y mascaras

- [ ] Primario/secundario, flips H/V y paleta por subtile.
- [ ] Capas base/foreground/full y transparencia de indice cero.
- [ ] Slots animados y union de mascaras por frame.
- [ ] Flood fill, seals, spans y componentes conexos.
- [ ] Procedencia exacta de pixel y continuidad UV.

### Clasificacion y perfiles

- [ ] Prioridad de pins, condicionales, patrones, eventos y fallback.
- [ ] Conteos exactos de colocaciones por plantilla.
- [ ] Claims sin solapamientos ni doble geometria.
- [ ] Reglas muertas y arte ambiguo detectados.
- [ ] Compilacion determinista byte a byte.

### Geometria

- [ ] Ocupacion, shell y eliminacion de seis vecinos.
- [ ] Fusion de runs contiguos y rechazo en seams incompatibles.
- [ ] Edificios planos, gabled, asimetricos, con alero, ledge y recess.
- [ ] Ninguna pared atraviesa el tejado.
- [ ] Ningun fondo exterior genera geometria.
- [ ] Suelo de reemplazo determinista.
- [ ] Arboles agrupados, hulls, tocones y rocas.
- [ ] Props, supports, furniture y component filtering.
- [ ] Elevacion, cliffs, ledges, stairs, water y bridges.
- [ ] Estructuras cruzando limites de chunk y mapa.
- [ ] Capacidad maxima y fallo seguro con un vertice menos.

### Runtime

- [ ] Thread safety y snapshots inmutables.
- [ ] Invalidation por reglas, mapa, evento y layout.
- [ ] Animaciones sin remeshing espurio.
- [ ] Cache acotada durante viaje prolongado.
- [ ] Fallback 2D ante error, perfil invalido o escena no soportada.
- [ ] Builds classic y diorama de 32 bits.

## Fase G18: validacion visual y rendimiento

- [ ] Crear goldens de referencia por arquetipo y familia de tileset.
- [ ] Comparar 2D original, mascara, voxel occupancy, shell y resultado final.
- [ ] Capturar varios pitch/yaw para revelar laterales e intrados.
- [ ] Activar wireframe y procedencia de materiales.
- [ ] Verificar ausencia de suelo flotante, huecos negros, paredes estiradas y z-fighting.
- [ ] Verificar ventanas/puertas entrantes y soportes de props.
- [ ] Verificar arboles, bosques, grass y flowers alrededor del jugador.
- [ ] Verificar transiciones, seams, Cut, Rock Smash, berry growth y puertas.
- [ ] Medir CPU, GPU, memoria, VBOs, chunks reconstruidos y tiempo de catalogacion.
- [ ] Mantener 60 FPS en el perfil de escritorio acordado.
- [ ] Definir degradaciones explicitas por calidad sin cambiar gameplay.

## Comandos de verificacion

```bash
python3 tools/diorama_rules/validate_rules.py
python3 tools/diorama_rules/compile_rules.py
python3 tools/diorama_rules/compile_rules.py --check
python3 tools/diorama_rules/test_compile_rules.py
python3 map_editor/test_server.py
make -f Makefile_pc test-diorama \
  PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig
make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1 \
  PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig \
  -j"$(nproc)"
make -f Makefile_pc NATIVE_LINUX=1 \
  PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig \
  -j"$(nproc)"
```

Smoke tests obligatorios en Linux:

- [ ] Arrancar y cerrar repetidamente classic y diorama.
- [ ] Crear partida, guardar, cerrar y cargar sin diferencias.
- [ ] Caminar, correr, usar bici y probar input con teclado/gamepad.
- [ ] Entrar y salir de mapas, edificios, cuevas y conexiones exteriores.
- [ ] Abrir dialogos, menus y combates y verificar fallback clasico.
- [ ] Probar audio del juego, SDL2_mixer separado y ausencia segura de NVDA.
- [ ] Probar al menos una ruta de lector de pantalla sin alterar sus latches.
- [ ] Probar resize, fullscreen, VSync, integer scaling, alt-tab y speedup.
- [ ] Probar Cut, Rock Smash, puertas, berry trees, decoraciones y cambios de layout.
- [ ] Confirmar que no aparecen meshes anteriores, flashes 2D indebidos ni datos de otro mapa.

## Criterios de finalizacion global

- [ ] Los 518 mapas tienen representacion 3D aprobada; fallback queda reservado a escenas, errores y transiciones, no a mapas pendientes.
- [ ] Los 441 layouts, incluidos alternativos, tienen representacion 3D validada.
- [ ] Los 75 tilesets estan catalogados y no dependen de registros hardcodeados incompletos.
- [ ] No quedan overrides por coordenada que dupliquen una regla reutilizable.
- [ ] Ningun edificio es una coleccion de cajas por metatile.
- [ ] Ningun objeto recortado eleva su suelo de fondo.
- [ ] Ninguna cara se elimina solo por compartir ID si la ocupacion no coincide.
- [ ] Ninguna fachada o lateral estira arte fuera de su banda declarada.
- [ ] Edificios, muebles, carteles, ordenadores, arboles, bosques, rocas, vallas, grass y flowers tienen los mismos principios volumetricos que la referencia.
- [ ] Todos los objetos dinamicos siguen el estado publicado por el juego.
- [ ] Atlas, paletas y animaciones siguen siendo los originales y permanecen vivos.
- [ ] Editor, compilador, referencia headless y runtime producen geometria equivalente.
- [ ] `DIORAMA=0` no cambia visual ni funcionalmente.
- [ ] Tests, builds y auditoria visual global pasan antes de retirar un fallback.

## Referencias principales

- `../DramaticShapeVoxelMod/lib/Buildings.lua`
- `../DramaticShapeVoxelMod/lib/Structures.lua`
- `../DramaticShapeVoxelMod/lib/TileShape.lua`
- `../DramaticShapeVoxelMod/lib/ChunkMesher.lua`
- `../DramaticShapeVoxelMod/lib/TerrainAtlas.lua`
- `../DramaticShapeVoxelMod/data/voxel_heights.lua`
- `../DramaticShapeVoxelMod/assets/docs/buidling_to_voxel/sprite_to_voxel_methodology.md`
- `../DramaticShapeVoxelMod/assets/docs/buildings/README.md`
- `../DramaticShapeVoxelMod/tools/voxel-survey.md`
- `pokeemerald_diorama_plan_implementacion.md`
- `docs/emerald_pixel_profiled_buildings.md`

Regla de cierre: **autorizar significado; derivar geometria medible; fallar antes de inventar**.
