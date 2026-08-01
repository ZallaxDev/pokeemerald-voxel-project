# Paridad del sistema voxel de Pokemon Rojo

## 1. Objetivo y significado de "identico"

El objetivo es portar a Pokemon Emerald el sistema completo de interpretacion geometrica voxel de
`../DramaticShapeVoxelMod/`: clasificacion automatica conservadora, deteccion de
estructuras sobre el mapa completo, medicion de alturas desde el dibujo, voxelizacion
selectiva por pixel, perfiles manuales reutilizables, plantillas exactas de edificios,
survey visual y comprobacion del blast radius.

La referencia queda fijada al commit local
`b21fd46ea789a0b8cb99d2c7e0add5a007568a54`. Un cambio posterior de la referencia no
cambia este contrato sin una decision y una nueva matriz de trazabilidad.

"Identico" significa conservar, dentro del interprete geometrico:

- el mismo reparto entre automatizacion y significado autorizado por una persona;
- el mismo orden de resolucion y reclamacion de estructuras;
- los mismos tipos de detectores y sus fallbacks conservadores;
- el mismo principio de medir geometria visible y no inventar profundidad;
- el mismo ciclo `survey -> diagnostico -> pin -> nuevo survey -> blast radius`;
- la separacion absoluta entre presentacion voxel y gameplay.

No significa copiar IDs, coordenadas, formatos Lua, APIs de LOVE, colision Gen 1,
umbrales de una paleta de cuatro tonos ni dimensiones de tiles de Rojo. Emerald usa
metatiles 16x16 de dos capas, behaviors, collision y elevation propios. La adaptacion
debe conservar el algoritmo y sustituir sus entradas por las autoridades equivalentes
de Emerald.

No se portan el cielo, camera rig, battle arena, sombras, postprocesado ni el orden de
presentacion completo de `VoxelScene`. Emerald conserva su compositor OpenGL, snapshots,
chunks y fallback software ya establecidos. Las mejoras obligatorias de Emerald se
documentan como adaptaciones, no como comportamiento que ya existiera en Rojo.

El sistema de Rojo no es una conversion universal de una imagen 2D a 3D. Es un sistema
hibrido comparable a un perfil de 3dSen: un detector automatico genera una base y un
perfil manual corrige interpretaciones ambiguas. Su resultado maduro incluye casi
3.000 lineas de perfil, alrededor de 1.190 tiles fijados manualmente y plantillas
exactas de edificios. La paridad no se alcanzara fingiendo que esa curacion no existe.

## 2. Fuentes de autoridad de Rojo

| Pieza | Responsabilidad exacta |
|---|---|
| `main.lua` | Registra el pipeline, actualiza camara y jobs, invoca la escena voxel, conserva fallback 2D e invalida caches. No clasifica arte. |
| `lib/TileShape.lua` | Resuelve cada tile a clase, altura, modo de arte y procedencia authored/automatic. |
| `data/voxel_heights.lua` | Perfil manual: alturas nominales, pins por tileset, reglas contextuales, pools, reemplazos de suelo y plantillas de edificio. |
| `lib/Structures.lua` | Analiza el mapa completo, reclama edificios, agrupa componentes, detecta props, mide volumenes y construye formas especiales. |
| `lib/Buildings.lua` | Compone templates exactos, mide silueta/tejado/ventanas y genera el modelo voxel del edificio. |
| `lib/ChunkMesher.lua` | Convierte las decisiones anteriores en caras texturizadas, cachea meshes de mapa completo y gestiona jobs/invalidation. |
| `lib/SpriteBillboards.lua` | Renderiza jugador y NPC como quads alpha-tested. No los voxeliza. |
| `tools/voxel-survey.md` | Define el proceso obligatorio de validacion visual y revision del blast radius. |

Los datos extraidos de Rojo proporcionan mapas, bloques, atlas, listas walkable, agua,
grass, puertas, animaciones, conexiones y warps. No proporcionan respuestas semanticas
como "esto es una mesa", "estas filas forman un tejado" o "esta fachada mide 32 px".

## 3. Pipeline exacto de Rojo

### 3.1 Preparacion del mapa

Antes de clasificar geometria, el sistema dispone de:

1. El mapa completo, no solo el viewport.
2. La colocacion de tiles y bloques.
3. El atlas y acceso a sus pixels.
4. Walkability y agua a nivel de celda.
5. Sets walkable/water a nivel de tile como fallback.
6. Grass, flores animadas, puertas y metadata de mapa exterior/interior.
7. El perfil manual cargado y validado.

La unidad de gameplay de Rojo es una celda 16x16 formada por tiles 8x8. La colision de
esa celda no describe de forma independiente sus cuatro tiles. Por ello, la
clasificacion automatica de los tiles decorativos debe respetar primero el significado
de la celda; de lo contrario flores, hierba y huecos de vallas se convierten en pilares.

### 3.2 Clasificacion `TileShape`

Cada tile termina en un registro equivalente a:

```text
class, height, artMode, flat, authored
```

La resolucion efectiva es:

1. Aplicar una regla posicional manual si el contexto la satisface, por ejemplo
   `when_above` para un tile reutilizado como pared o mostrador.
2. En ausencia de una condicion mas especifica, aplicar el pin manual del tileset.
3. Aplicar derivados fiables como grass y flores animadas; Rojo los marca como authored
   para que las reglas de celda no eliminen su geometria aditiva.
4. Si la celda es agua, clasificar como `water`.
5. Si la celda es walkable, clasificar como `ground`.
6. Aplicar los sets de tile de agua o walkable del extractor.
7. Clasificar como `wall` todo tile restante.

Ese ultimo paso garantiza una respuesta determinista, no una interpretacion correcta.
Los errores del default `wall` son precisamente los que corrige el perfil manual.

Las clases separan altura y tratamiento del arte. Entre los modos usados por Rojo hay:

- plano: `ground`, `water`, `void`;
- arte en la cara superior: `ledge`, `roof`, `bed`;
- volumen con bandas frontales: `wall`, `tree`, `fence`, `counter`, `table`, `desk`;
- escalera elevada o stairwell descendente;
- cutout vertical por pixel: `billboard`, `prop`, `stool`, `cutout`;
- relieve horizontal por pixel;
- `bookcase` con ranks colapsados;
- hull cilindrico o redondeado;
- formas agrupadas como canopies y stumps.

### 3.3 Orden de reclamacion en `Structures`

La deteccion no extruye cada tile de forma independiente. Analiza la composicion del
mapa completo y respeta este orden:

1. Resolver la clase de todas las celdas/tiles mediante `TileShape`.
2. Detectar void o negro exterior mediante pixels del atlas.
3. Buscar primero las plantillas exactas de `Buildings`.
4. Reclamar todas las celdas de cada edificio ganador.
5. Plegar puertas walkable dentro de una fachada vecina cuando el perfil no lo impide.
6. Construir cilindros/canopies/stumps, escaleras y bookcases authored; al no ser
   `upright` automaticos, no entran en el flood estructural posterior.
7. Flood-fill de las regiones `upright` restantes y no autorizadas.
8. Intentar extraer props tipo sprite desde cada region elegible.
9. Convertir el residuo estructural en volumenes genericos medidos desde sus runs.
10. Construir despues los billboards, fence posts y reliefs forzados, junto con grass y
    flowers aditivos; sus clases authored ya los habian excluido del flood generico.
11. Resolver finalmente el suelo bajo props authored.

La regla principal es `first claim wins`: una estructura aceptada elimina sus celdas
del dominio de los detectores posteriores. Rojo realiza culling dentro de sus builders y
en fronteras de terreno, pero no dispone de un IR universal de ocupacion para todas las
familias. El IR comun de Emerald es una mejora deliberada que debe producir el mismo
resultado visible y evitar caras internas adicionales sin cambiar clasificacion.

### 3.4 Extraccion automatica de props por pixel

Para una region candidata, Rojo:

1. Compone la imagen de la region con un apron de un pixel.
2. Estima el fondo desde pixels conectados al suelo vecino.
3. Hace flood-fill del fondo.
4. Mide cuanto fondo accesible contiene cada tile.
5. Rechaza regiones altas, repetitivas o demasiado solidas para ser props.
6. Separa componentes conexos del foreground restante.
7. Convierte cada pixel visible en un prisma fino con el texel original.
8. Apoya cada componente sobre sus propios pies dibujados o sobre un soporte authored.

Los `billboard` manuales fuerzan esta ruta y usan segmentacion por contorno oscuro. Los
pools distintos evitan fusionar objetos que se tocan. La segmentacion puede fallar si el
dibujo llega a todos los bordes y sus propios tonos se confunden con fondo; Rojo cambia
la clase/pin o corrige la segmentacion, no dispone de una mascara manual de prop. La fase
de paridad debe hacer lo mismo. Una mascara explicita futura seria una extension Emerald.

### 3.5 Volumenes genericos y altura automatica

Las regiones solidas no reclamadas se miden sobre el grid completo de tiles:

1. Flood-fill de componentes estructurales conexos.
2. Division por runs verticales del mapa.
3. Medicion del extent de tiles de cada run.
4. Deteccion de secuencias repetidas por igualdad de tile ID. El detector de Rojo ancla
   en el tile frontal, busca la primera repeticion hacia el norte y tambien reconoce un
   trim frontal seguido por dos tiles iguales.
5. Limite de seguridad de seis filas de 8 px en Rojo.
6. Consenso de region para reconciliar columnas compatibles.
7. En exteriores, interpretacion opcional de filas superiores distintas como tejado
   inclinado; las filas repetidas permanecen como volumen nivelado.
8. Conservacion del tile propietario de cada banda para no estirar un unico tile.

La altura es la altura dibujada de una estructura en la proyeccion del juego, no una
medida fisica verdadera. La heuristica falla con muebles unidos a paredes, arte top-down,
stairs walkable y edificios que mezclan proyecciones; esos fallos se resuelven con pins
y templates.

### 3.6 Formas pixel-derived especiales

Los pixels se utilizan despues de haber seleccionado una semantica apropiada:

- `round-hull`: talla un hull desde un contorno oscuro cerrado o un fallback de dither;
- grass y flowers: cutouts alpha-tested/voxelizados desde el frame original;
- relief: mascara de contorno extruida pocos pixels sobre una superficie horizontal;
- billboard/prop: silueta vertical por pixel;
- grouped canopy: una estructura para varias celdas de copa compatibles;
- stump/bookcase/stairs: builders especificos, no el volumen generico.

La seleccion de la clase suele ser manual aunque la geometria final sea medida
automaticamente. Esto es parte esencial del sistema de Rojo.

### 3.7 Edificios

Un edificio perfilado y encontrado por exact matching no se trata como un tile pin ni
como un cubo generico. Un dibujo de edificio mezcla techo visto desde arriba, fachada
frontal y extremos inclinados. Un edificio no perfilado o cuyo perfil falla puede caer
al volumen generico en Rojo. El perfil proporciona lo que los pixels no pueden demostrar:

- matriz exacta completa de tiles;
- filas que pertenecen al tejado y a la fachada;
- mapeo de profundidad del tejado: back, front y ciclo;
- grosor de slab;
- eave, awning, ledges y seals;
- `topRows` para dibujos partidos entre mapas;
- `claimOnly` para reclamar una mitad sin generar un duplicado;
- prioridad entre templates solapables.

Despues, `Buildings` deriva automaticamente:

1. Todas las colocaciones por coincidencia exacta de la matriz.
2. La composicion a resolucion nativa.
3. La silueta mediante flood-fill exterior.
4. El perfil superior de cada columna y la pendiente del tejado.
5. Ventanas y paneles encerrados por marcos.
6. El shell de tejado, fachada, awning y huecos.
7. La eliminacion de voxels/caras ocultos y fusion compatible.

Una implementacion Python independiente genera previews y comprueba simetria, pendiente
constante, cobertura de paredes, ausencia de penetraciones y paridad de counts con
runtime. Rojo duplica manualmente parte de las tablas en esa referencia Python. Emerald
debe mejorar ese punto generando ambas implementaciones desde el mismo JSON authoritative.

### 3.8 Personajes

Jugador y NPC se renderizan como quads con alpha discard. No se infiere profundidad ni
se generan modelos voxel desde sus frames porque eso inventaria superficies no visibles.
Emerald debe conservar el mismo criterio salvo que exista un proyecto separado con
modelos authored.

### 3.9 Meshing, cache e invalidacion

`ChunkMesher` consume resultados ya clasificados. Emite planos, cajas plegadas,
volumenes medidos, cutouts y modelos de estructuras; conserva texels del atlas, elimina
caras ocultas compatibles, anade bandas y AO, crea meshes de forma asincrona y cachea
variantes de mapa completo (`full`, `body`, `grass`, `flowers`). Cambios de mapa,
bloques, assets o configuracion invalidan el mapa afectado. Emerald conserva chunks y
owners como una adaptacion necesaria para sus layouts y renderer existentes.

El mesher no debe decidir si algo es una pared, arbol o edificio. Clasificacion,
estructura y presentacion son responsabilidades separadas.

## 4. Correccion manual exacta de Rojo

El perfil manual no contiene geometria arbitraria celda por celda. Contiene conocimiento
reutilizable:

- `tileset + tile -> class`;
- altura nominal por clase;
- condiciones posicionales como `when_above`;
- pools para separar componentes que se tocan;
- `prop_ground` para declarar el suelo bajo un prop;
- parametros de formas especiales;
- templates multicelda de edificios.

Un pin evita completamente el detector generico para su arte. Por eso una correccion
puede cambiar la region automatica vecina y siempre exige un nuevo survey completo.

El procedimiento de autorado obligatorio es:

1. Capturar una vista 2D plana, que es la autoridad visual.
2. Capturar las mismas posiciones en pitches equivalentes a 15, 35, 50 y 75 grados. El
   survey documentado de Rojo usa 15/35/50, pero el runtime de la referencia fijada
   tambien ofrece 75; Emerald cubrira los cuatro para no congelar esa omision.
3. Describir el objeto incorrecto, sus celdas y la forma representada por el dibujo.
4. Identificar tileset, arte, usos y mapas que comparten la regla.
5. Elegir la clase o template mas estrecho que expresa el significado.
6. Autorizar solamente significado y dimensiones no medibles.
7. Derivar automaticamente pixels, silueta, placements, shell y UV.
8. Regenerar y repetir las capturas en un directorio nuevo.
9. Comparar todos los objetos visibles, no solo el corregido.
10. Probar al menos otro mapa que comparta el tileset.
11. Si cambio un detector comun, probar un mapa ocupado y no relacionado.
12. Ejecutar invariantes headless y builds classic/Diorama.

No se cambia collision, scripts, warps o eventos para hacer que la geometria parezca
correcta. Si una correccion visual exige cambiar gameplay, la correccion es incorrecta.

## 5. Traduccion exacta a Emerald

### 5.1 Correspondencia de entradas

| Rojo | Emerald |
|---|---|
| tile 8x8 y celda 16x16 | metatile 16x16 con ocho subtiles y dos capas |
| blocks y mapa extraido | `map.bin`, layouts, primary/secondary tilesets |
| walkable/water sets | collision, behaviors y eventos, sin tratarlos como forma absoluta |
| atlas de cuatro tonos | tiles GBA, flips, paleta por subtile y transparencia index 0 |
| map type | map type, layout, conexiones y runtime layout efectivo |
| perfil Lua | JSON estricto en `data/diorama/` compilado a C inmutable |
| runtime LOVE | compilador Python offline y renderer OpenGL C de 32 bits |
| cache de meshes por mapa | owners de chunks y snapshots inmutables como adaptacion Emerald |

La unidad geometrica de detalle sera un pixel Emerald, `1/16` de celda. Todo pixel debe
conservar metatile, capa, subtile, tile, paleta, indice de color, flips y UV. El compositor
canonico ya debe ser la unica implementacion para catalogo, detector, pruebas y runtime.

### 5.2 Informacion que Emerald mejora

Emerald aporta behaviors de agua, ledges, bridges, stairs, holes, rails, currents y
furniture; collision por celda; layer type; dos capas; alpha y elevacion de eventos.
Estos datos permiten clasificar mas casos con confianza que en Rojo.

No autorizan estas simplificaciones:

- `collision != 0` no significa pared;
- `layerType` no significa vertical;
- behavior no siempre determina altura;
- elevation no es una distancia ni un orden vertical;
- `height = elevation * constante` esta prohibido;
- un metatile reutilizado no tiene necesariamente una unica semantica global.

Elevation 0 actua como wildcard y 15 puede conservar el plano entrante. Los valores
concretos son identidades de gameplay. Solo deben separar superficies y seleccionar
soporte de objetos. Las alturas metricas se obtienen de dibujo, transiciones fiables,
templates o perfiles.

### 5.3 Orden de resolucion Emerald de paridad

La implementacion objetivo debe ejecutar offline, sobre el layout completo y con arte
ensamblado, este orden:

1. Cargar catalogos, layout efectivo, metatiles, attributes, pixels y perfil.
2. Resolver cada celda con precedencia semantica invariable:
   `contextual authored > general authored > derived reliable > cell water/ground > fallback`.
3. Buscar y reclamar templates exactos de edificios/estructuras.
4. Plegar puertas/fachadas despues de edificios y sin sobrescribir pins authored.
5. Reclamar formas authored especiales: cilindros/canopies/stumps, stairs y bookcases.
6. Agrupar regiones no reclamadas con conectividad de clasificacion y mapa.
7. Extraer props elegibles por flood de fondo y silueta.
8. Medir volumenes residuales con runs, firmas de fuente repetidas y consenso.
9. Construir billboards, fence posts, reliefs, grass y flowers authored/aditivos.
10. Resolver el ground replacement de props.
11. Resolver constraints relativas de ledges, stairs y bridges ya clasificados, sin usar
    elevation como regla metrica.
12. Generar ocupacion, shell, materiales y owners.
13. Compilar registros inmutables protegidos por layout/coordenada/metatile esperado.
14. En runtime, validar contra valores/fingerprints copiados en el snapshot y consumir
    el resultado. La validacion de globals vivos solo puede ocurrir en el game thread al
    publicar; ningun worker o thread grafico lee mapa, OAM, VRAM o paletas mutables.

La precedencia del paso 2 no puede cambiar por el orden de archivos JSON. Una condicion
especifica como `when_above` retorna antes que el pin ordinario, igual que Rojo. Los
eventos Emerald solo aportan evidencia o presets para objetos estaticos conocidos; un
NPC o evento movil nunca reclama ni elimina terreno por estar colocado sobre una celda.

### 5.4 Fallback

Si una region no puede clasificarse con confianza:

1. conservar el metatile plano o el framebuffer 2D;
2. registrar la ambiguedad y toda su evidencia;
3. no generar una columna especulativa;
4. no aplicar una regla de otra colocacion sin comprobar contexto;
5. no inventar pixels o caras sin procedencia.

Esto es una divergencia deliberada respecto a Rojo, cuyo fallback automatico final es
`wall` y cuyo fallback 2D se aplica a la escena completa si no hay mesh. Emerald adopta
un fallback local plano y un fallback software de escena mas conservadores porque un
`wall` por defecto sobre collision rica produciria falsos volumenes. La divergencia debe
permanecer visible en la matriz de trazabilidad.

El software renderer original sigue siendo obligatorio para menus, combates,
transiciones, escenas especiales, errores y contenido no soportado. No se porta la
presentacion de combates sobre el overworld 3D que ofrece la referencia de Rojo.

## 6. Arquitectura objetivo

El port debe separar modulos equivalentes a la referencia:

| Responsabilidad | Modulo objetivo |
|---|---|
| Clasificacion tipo `TileShape` | `tools/diorama_rules/tile_shape.py` |
| Analisis tipo `Structures` | `tools/diorama_rules/structures.py` |
| Props y segmentacion | `tools/diorama_rules/pixel_objects.py` |
| Edificios | `tools/diorama_rules/buildings.py` |
| Perfil y precedencia | `tools/diorama_rules/profiles.py` y JSON autoritativo |
| Survey reproducible | `tools/diorama_survey/` |
| IR de ocupacion/shell | modelo Python comun y backend C existente |
| Runtime | tablas C generadas y renderer que no reclasifica |

Los nombres pueden cambiar solo antes de publicar cada modulo. Los JSON de Emerald son
la unica fuente editable, mejora intencional respecto a la duplicacion manual de algunas
tablas de referencia de edificios en Rojo. No debe conservarse en
paralelo un detector viejo "por si acaso". Cuando una fase sustituya una ruta:

- se elimina su codigo, flags, datos, tests y documentacion obsoletos en el mismo cambio;
- no se deja codigo bajo `#if 0`;
- no se dejan aliases de schema sin consumidores reales;
- no se mantienen dos compositores, clasificadores o resolvers;
- los artefactos generados se regeneran y no conservan records del sistema retirado;
- el fallback plano/2D se conserva porque es una funcion del producto, no residuo.

El editor visual queda expresamente fuera de este trabajo. Ninguna fase depende de
`map_editor/`, ninguna validacion manual se realiza con el editor y no se ampliara su
schema o UI hasta que el pipeline headless y runtime alcance paridad aprobada.

## 7. IA local como herramienta opcional

Una IA local puede reducir clasificacion y revision, pero no resolver la ambiguedad 2D
a 3D ni escribir reglas autoritativas sin aprobacion.

Usos permitidos:

- embeddings locales para agrupar metatiles o estructuras visualmente similares;
- segmentacion asistida, por ejemplo SAM/SAM2 sobre pixel art escalado con nearest
  neighbor, para proponer masks;
- un VLM local para sugerir etiquetas como roof, facade, tree, counter o stairs;
- deteccion de anomalias para priorizar surveys;
- comparacion de capturas antes/despues y ranking de regiones que cambiaron;
- propagacion de una etiqueta revisada a candidatos con la misma firma exacta.

Usos prohibidos:

- convertir una probabilidad en una regla runtime automaticamente;
- inferir profundidad oculta y presentarla como medida;
- enviar ROM, assets o capturas a servicios remotos;
- introducir una dependencia de modelo en el juego;
- producir resultados no reproducibles sin registrar modelo, version, hash, prompt,
  seed y parametros;
- mantener tooling IA que no supere un benchmark acordado.

La prioridad tecnica es CV determinista: alpha, flood-fill, connected components,
repeticion, provenance y constraints. Un modelo solo genera un informe de candidatos.
Si se evalua un VLM, debe ser pequeno y local, por ejemplo una variante vision de 7B
cuantizada ejecutable con llama.cpp/Ollama; si se evalua segmentacion, debe compararse
contra masks golden a resolucion nativa. Si no mejora precision o tiempo humano de forma
medible, se elimina por completo al terminar el spike.

## 8. Validacion de paridad

La paridad no se demuestra con coverage numerica ni porque cada celda reciba una clase.
Requiere:

- una matriz versionada `funcion/regla Rojo -> adaptacion Emerald -> fixture -> resultado`;
- fixtures que comparen decisiones intermedias con la referencia conceptual;
- capturas 2D y pitches 15/35/50/75 desde posiciones reproducibles;
- comprobacion de silhouette, altura dibujada, soporte y ausencia de caras inventadas;
- tests de exact matching, claims, repeticion y segmentacion;
- igualdad Python/C del IR consumido por runtime;
- survey de todos los mapas que comparten una regla modificada;
- un mapa no relacionado tras cambios en detectores comunes;
- mutaciones de metatile y layouts alternativos;
- builds i386 classic y Diorama;
- aprobacion manual explicita antes de iniciar la fase siguiente.

El procedimiento ejecutable, el orden de implementacion y los gates manuales estan en
`TODOLIST_DIORAMA_RED_PARITY.md`. Ese archivo es la autoridad de avance de este port.

## 9. Puntos de lectura de la referencia fijada

- Precedencia y clasificacion: `lib/TileShape.lua:195-369`.
- Orden de estructuras y claims: `lib/Structures.lua:126-437`.
- Hulls redondeados y canopies: `lib/Structures.lua:477-1017`.
- Reliefs, bookcases y stairs: `lib/Structures.lua:1024-1443`.
- Runs, repeticion y roofs genericos: `lib/Structures.lua:1445-1584`.
- Segmentacion de props: `lib/Structures.lua:1586-2117`.
- Exact matching de edificios: `lib/Buildings.lua:590-675`.
- Perfil manual completo: `data/voxel_heights.lua`.
- Survey y blast radius: `tools/voxel-survey.md`.
- Personajes billboard: `lib/SpriteBillboards.lua`.
- Emision y cache de mapa: `lib/ChunkMesher.lua:207-1108`.
