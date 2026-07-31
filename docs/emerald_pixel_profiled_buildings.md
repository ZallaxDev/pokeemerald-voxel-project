# Edificios perfilados por pixel para Pokemon Emerald

> **Estado:** prototipo schema v1 retirado en G0. Este documento conserva el
> analisis historico, pero sus cajas por celda, perfiles unidimensionales y
> culling por `structureId` no son una base aprobada. G11-G12 los sustituiran por
> patrones exactos, mascaras de ocupacion y un shell volumetrico comun.

## Objetivo

Portar a Pokemon Emerald los conceptos del sistema de edificios perfilados de
`DramaticShapeVoxelMod` para evitar fachadas estiradas y permitir tejados,
aleros, entrantes y siluetas con precision de un pixel.

La implementacion debe:

- Recomponer el arte completo de cada edificio desde metatiles originales.
- Mantener una escala de un pixel de arte por `1/16` de celda en el mundo.
- Dividir fachadas en franjas verticales sin estirar un metatile completo.
- Recortar mediante UV la ultima franja cuando la altura no sea entera.
- Extraer o definir perfiles de tejado con precision de un pixel.
- Omitir geometria fuera de la silueta real.
- Eliminar caras internas y fusionar caras compatibles.
- Usar los atlas, paletas y animaciones publicados por el juego.
- Producir la misma geometria en el juego y en `map_editor`.

Este trabajo es exclusivamente visual. No puede modificar mapas jugables,
colisiones, eventos, scripts, elevaciones ni guardados.

## Restricciones obligatorias

1. El juego original sigue siendo la autoridad de gameplay.
2. El hilo grafico solo consume snapshots inmutables y tablas generadas.
3. No se leen mapas, VRAM, sprites o paletas mutables desde el hilo grafico.
4. El modo clasico y el fallback 2D deben permanecer completos.
5. La build de escritorio sigue siendo de 32 bits.
6. Los JSON de `data/diorama/` son la unica fuente editable.
7. Los archivos C generados no se editan manualmente.
8. `../DramaticShapeVoxelMod/` es referencia de conceptos y no se modifica.
9. Los IDs, umbrales de color y coordenadas de Pokemon Rojo no son compatibles.
10. Toda geometria nueva debe invalidarse por generacion de reglas o mapa.

## Diagnostico actual

Las plantillas actuales contienen una altura de cuerpo y un unico material por
cara:

```json
{
  "width": 5,
  "height": 5,
  "roofRows": 3,
  "bodyHeight": 1.2,
  "roofHeight": 0.9,
  "profile": "gable-x",
  "faces": {
    "south": { "metatile": "self", "layer": "full" }
  }
}
```

El mesher convierte la cara sur en un solo quad y expande sus UV sobre toda la
altura. Si la fachada original ocupa dos filas de metatiles, solo se utiliza el
metatile de la fila exterior y se estira verticalmente.

El flujo responsable esta en:

- `src/diorama/rules.c:105-173`: resuelve una colocacion como estructura.
- `src/diorama/gl/gl_terrain_renderer.c:240-304`: convierte materiales
  resueltos en UV del atlas.
- `src/diorama/terrain/terrain_mesh.c:112-143`: construye los cuatro laterales.
- `src/diorama/terrain/terrain_mesh.c:349-391`: emite un quad por cara lateral.
- `include/diorama/rules.h:112-182`: estructuras resueltas y generadas.
- `include/diorama/terrain_mesh.h:29-67`: entrada material del mesher.

## Como lo resuelve la referencia de Pokemon Rojo

### Reconstruccion del edificio

`../DramaticShapeVoxelMod/lib/Buildings.lua:97-133` recompone una imagen completa
desde la matriz de tiles de 8x8. Para cada pixel conserva el color y la
coordenada original del atlas.

Las plantillas se reconocen comparando la matriz completa, no por un unico tile:

- `../DramaticShapeVoxelMod/lib/Buildings.lua:590-675`
- `../DramaticShapeVoxelMod/data/voxel_heights.lua:2157-2828`

### Silueta y perfil del tejado

La referencia inunda el fondo desde los bordes y mide el primer pixel interior
de cada columna:

- `../DramaticShapeVoxelMod/lib/Buildings.lua:135-191`
- `../DramaticShapeVoxelMod/lib/Buildings.lua:286-399`

El perfil de altura se obtiene con:

```text
T[x] = yTop - firstInteriorPixel[x]
```

El tejado ocupa un volumen de grosor constante alrededor de `T[x]`. Los campos
`slab`, `frontEave` y `ledge` controlan grosor, alero y salientes.

### Geometria y reduccion

La referencia crea conceptualmente ocupacion por pixel, elimina caras ocultas y
fusiona recorridos adyacentes que conservan una procedencia UV valida:

- `../DramaticShapeVoxelMod/lib/Buildings.lua:401-446`: ocupacion y shell.
- `../DramaticShapeVoxelMod/lib/Buildings.lua:448-486`: continuidad UV.
- `../DramaticShapeVoxelMod/lib/Buildings.lua:488-585`: fusion de quads.

Las caras parciales recortan UV en vez de estirar el tile:

- `../DramaticShapeVoxelMod/lib/ChunkMesher.lua:241-248`
- `../DramaticShapeVoxelMod/lib/ChunkMesher.lua:553-615`

### Transparencia

Los edificios omiten geometria fuera de la silueta. Los props recortados generan
prismas solo para pixeles retenidos:

- `../DramaticShapeVoxelMod/lib/Structures.lua:1601-1824`
- `../DramaticShapeVoxelMod/lib/Structures.lua:1875-2059`

El shader descarta alpha antes de escribir profundidad:

- `../DramaticShapeVoxelMod/lib/Voxel3D.lua:204-210`
- `../DramaticShapeVoxelMod/lib/ShadowMap.lua:95-103`

Las ventanas y puertas de edificios perfilados no son agujeros completos. La
referencia elimina una capa frontal y deja otra detras para crear un entrante:

- `../DramaticShapeVoxelMod/lib/Buildings.lua:221-260`
- `../DramaticShapeVoxelMod/lib/Buildings.lua:377-394`

## Diferencias que debe absorber el port

Pokemon Rojo trabaja con tiles de 8x8 y cuatro tonos. Emerald requiere:

- Metatiles de 16x16 formados por ocho subtiles de 8x8.
- Dos capas de cuatro subtiles.
- Flips horizontales y verticales por subtile.
- Paleta por subtile.
- Tilesets primario y secundario.
- Tiles animados que pueden no existir en el PNG estatico.
- Materiales `full`, `base`, `foreground` y `none`.

La clasificacion no debe copiar los umbrales blanco/gris/negro de la referencia.
En Emerald se debe usar indice de paleta cero como transparencia de capa,
metadatos explicitos y mascaras editables para casos ambiguos.

## Diseno de datos

### Version inicial del esquema

Ampliar las plantillas de `data/diorama/buildings/*.json` con un objeto opcional
`pixelProfile`:

```json
{
  "version": 1,
  "templates": {
    "littleroot_house": {
      "width": 5,
      "height": 5,
      "roofRows": 3,
      "bodyHeight": 1.75,
      "roofHeight": 0.9,
      "profile": "gable-x",
      "pixelProfile": {
        "version": 1,
        "reference": {
          "map": "MAP_LITTLEROOT_TOWN",
          "x": 2,
          "y": 4
        },
        "facades": {
          "south": {
            "mode": "footprint-rows",
            "rows": 2,
            "unitHeight": 1.0,
            "fit": "natural"
          }
        },
        "roof": {
          "mode": "pixel-silhouette",
          "layer": "full",
          "slabPixels": 2,
          "eaves": { "north": 0, "east": 0, "south": 2, "west": 0 },
          "seal": []
        },
        "recesses": {
          "mode": "explicit-mask",
          "depthPixels": 1
        }
      },
      "faces": {}
    }
  }
}
```

Semantica:

- `reference.map/x/y` identifica una colocacion representativa usada para
  componer y analizar el arte durante la compilacion.
- `footprint-rows` toma metatiles reales de filas interiores del footprint.
- Para `south`, la franja cero usa la ultima fila y las siguientes avanzan al
  norte.
- `unitHeight` usa unidades de celda; `1.0` equivale a 16 pixeles.
- `fit: natural` conserva escala y recorta la ultima franja.
- Un futuro `fit: contain` puede comprimir todas las filas, pero no pertenece a
  la primera implementacion.
- `slabPixels`, eaves y recesses usan pixeles de arte, no unidades de celda.
- `seal` permite cerrar lados donde el dibujo toca el borde y el flood fill no
  puede reconocer el exterior.

### Limites iniciales

Definir constantes pequenas y validadas:

```c
#define DIORAMA_BUILDING_MAX_FACADE_ROWS 4
#define DIORAMA_BUILDING_MAX_PROFILE_COLUMNS (32 * 16 + 1)
```

La primera version solo necesita `facades.south`. Norte, este y oeste conservan
el material actual. Generalizar despues de validar las casas y el laboratorio.

### Representacion compilada

No almacenar una textura RGBA nueva. Compilar selectores y perfiles que sigan
muestreando el atlas vivo del juego.

Agregar a `struct DioramaGeneratedBuildingTemplate`:

```c
uint8_t southFacadeRows;
uint8_t facadeFit;
uint8_t roofSlabPixels;
uint8_t roofEaveSouthPixels;
uint16_t roofProfileOffset;
uint16_t roofProfileCount;
```

Agregar una tabla generada:

```c
extern const uint8_t gDioramaBuildingRoofProfilePixels[];
extern const size_t gDioramaBuildingRoofProfilePixelCount;
```

Cada entrada del perfil expresa altura en pixeles para un limite vertical de
columna. Una plantilla de ancho `N` necesita `N * 16 + 1` muestras.

Agregar a `struct DioramaResolvedCell` y `struct DioramaTerrainCell`:

```c
uint16_t structureTemplateId;
uint8_t structureSouthFacadeRows;
```

No copiar el perfil completo en cada celda. El mesher puede consultar la tabla
generada inmutable por `structureTemplateId`.

## Compositor Emerald

### Archivo nuevo

Crear:

```text
tools/diorama_rules/building_profiles.py
```

Responsabilidades:

1. Leer la colocacion de referencia desde los catalogos de mapas y layouts.
2. Leer `map.bin`, `metatiles.bin`, `metatile_attributes.bin`, `tiles.png` y
   paletas.
3. Resolver metatiles primarios y secundarios con offset global `0x200`.
4. Decodificar los ocho subtiles con flips y paleta.
5. Producir por separado las imagenes `base`, `foreground` y `full`.
6. Recomponer el footprint completo de `width * 16` por `height * 16` pixeles.
7. Conservar para cada pixel su metatile fuente, capa y coordenada UV local.
8. Extraer el perfil del tejado dentro de las filas declaradas por `roofRows`.
9. Aplicar `seal` antes del flood fill.
10. Devolver perfiles y mascaras deterministas al compilador.

Reutilizar conceptos, no importar directamente código Lua. El decodificador del
editor actual en `map_editor/server.py` puede servir como referencia para PNG,
paletas, flips y composicion, pero la implementacion comun debe moverse a un
modulo reutilizable para evitar dos decodificadores divergentes.

### Transparencia y mascara

Para cada capa, un pixel con indice de paleta cero es transparente. Para `full`,
componer primero base y despues foreground.

El modo `pixel-silhouette` debe:

1. Crear una mascara binaria de pixels visibles.
2. Inundar desde el borde los pixels transparentes.
3. Considerar exterior todo pixel transparente conectado al borde.
4. Considerar interior los pixels visibles no inundados.
5. Aplicar cierres `seal` antes de inundar.
6. Medir el primer pixel interior de cada columna de tejado.

No usar luminancia para decidir transparencia. Si el arte no tiene un fondo
separable, exigir una mascara explicita en lugar de adivinar.

## Implementacion por fases

## Fase 1: fachadas apiladas

Esta fase corrige el problema visible antes de introducir perfiles por pixel.

### Compilador

Modificar `tools/diorama_rules/compile_rules.py`:

1. Permitir `pixelProfile` dentro de plantillas.
2. Validar version, caras permitidas, `rows` entre 1 y 4, `unitHeight > 0` y
   `fit == "natural"`.
3. Rechazar `rows > height - roofRows`.
4. Generar `southFacadeRows` y `facadeFit`.
5. Incluir todos los campos nuevos en el hash determinista.
6. Actualizar `render_c()` para las nuevas estructuras.

Modificar `tools/diorama_rules/test_compile_rules.py`:

1. Probar una fachada valida de dos filas.
2. Rechazar cero filas, demasiadas filas, modos desconocidos y campos extra.
3. Confirmar que dos compilaciones producen bytes identicos.

### Resolucion

Modificar:

- `include/diorama/rules.h`
- `src/diorama/rules.c`

Acciones:

1. Propagar `structureTemplateId` y `structureSouthFacadeRows` al resultado.
2. Mantener cero en reglas que no procedan de edificios.
3. No cambiar la prioridad de resolucion existente.

### Preparacion de materiales

Modificar `src/diorama/gl/gl_terrain_renderer.c:240-304`.

Para una celda de borde sur de una estructura:

1. Detectar `structureLocalY == structureHeight - 1`.
2. Para cada franja `i`, buscar la celda fuente en:

```text
sourceX = structureX + structureLocalX
sourceY = structureY + structureHeight - 1 - i
```

3. Obtener la celda desde el snapshot visible, nunca desde globals del juego.
4. Convertir su metatile al UV del atlas actual.
5. Copiar material, capa y UV a un array fijo de franjas en
   `DioramaTerrainCell`.
6. Si una fuente no esta disponible, marcar la estructura incompleta y usar la
   pared simple existente como fallback seguro.

La busqueda debe funcionar aunque estructura y chunk no coincidan. No depender
solo del halo de una celda de `DioramaTerrainChunkInput`.

### Mesher

Modificar:

- `include/diorama/terrain_mesh.h`
- `src/diorama/terrain/terrain_mesh.c`

Acciones:

1. Agregar `southFacadeCount` y materiales de franja a
   `DioramaTerrainCell`.
2. Aumentar `DIORAMA_TERRAIN_MAX_FACES` para el peor caso validado.
3. Crear `AppendQuadCropped()` que acepte UV parciales.
4. Sustituir el quad sur unico de edificios por franjas verticales.
5. Emitir cada franja entre:

```text
bottom = groundHeight + i * unitHeight
top = min(bottom + unitHeight, groundHeight + structureBodyHeight)
```

6. Para una franja parcial, interpolar V y mostrar solo la parte correspondiente
   del metatile. No comprimir el metatile completo.
7. Conservar la eliminacion de caras internas por `structureId`.
8. Incluir los nuevos materiales y campos en
   `DioramaTerrain_ChunkSignature()`.

Para una franja superior visible en una fraccion `f` desde su parte inferior:

```text
croppedVTop = v1 + (v0 - v1) * f
croppedVBottom = v1
```

Verificar la orientacion contra los tests del atlas; no corregirla mediante un
flip especial por edificio.

### Datos iniciales

Modificar:

```text
data/diorama/buildings/basic.json
```

Agregar una fachada sur de dos filas a `littleroot_house` y `birch_lab`. Ajustar
`bodyHeight` visualmente solo despues de que las dos franjas funcionen; no usar
la altura para compensar una textura estirada.

## Fase 2: compositor y perfil de tejado por pixel

### Herramienta

Implementar `tools/diorama_rules/building_profiles.py` y llamarla desde
`compile_rules.py`.

Acciones exactas:

1. Componer el footprint de referencia completo.
2. Limitar el analisis a `roofRows * 16` pixels superiores.
3. Crear la mascara de silueta.
4. Medir `width * 16 + 1` alturas de limites de columna.
5. Normalizar alturas a pixels sobre `bodyHeight`.
6. Emitir el perfil en `gDioramaBuildingRoofProfilePixels`.
7. Fallar con ruta, plantilla y pixel si el perfil es ambiguo.
8. Nunca generar silenciosamente un perfil plano ante un error.

### Mesher

Modificar `ResolveRoofHeight()` en
`src/diorama/terrain/terrain_mesh.c:93-110`:

1. Consultar el perfil por `structureTemplateId` cuando exista.
2. Convertir X local de mundo a indice de pixel con 16 pixels por celda.
3. Interpolar solo dentro de un pixel si el vertice no cae exactamente en la
   rejilla.
4. Mantener `gable-x`, `gable-z` y `flat` como fallback para plantillas sin
   `pixelProfile`.

Para conservar cortes de un pixel, subdividir la cara superior en tiras de
ancho `1/16` cuando dos muestras consecutivas tengan distinta altura. Fusionar
tiras consecutivas solo si tienen igual plano y UV contiguos.

### Aleros y grosor

Generar shell de tejado con:

- Superficie superior perfilada.
- Intrados a `slabPixels / 16.0` bajo la superficie.
- Fascia en limites expuestos.
- Extension por lado de `eavePixels / 16.0`.

Asignar cada span a una celda propietaria estable para evitar duplicados cuando
un edificio cruza limites de chunk.

## Fase 3: mascaras, transparencia y entrantes

### Alpha discard

El shader de terreno ya descarta texels transparentes. Verificarlo en:

```text
src/diorama/gl/gl_terrain_renderer.c
```

Si cualquier pase de profundidad o sombras se agrega posteriormente, debe usar
el mismo umbral de alpha antes de escribir profundidad.

No habilitar blending para resolver recortes opacos. Un fragmento transparente
con depth write puede ocultar geometria situada detras.

### Mascaras geometricas

Agregar fuentes opcionales bajo:

```text
data/diorama/buildings/masks/*.json
```

Las mascaras deben referirse desde la plantilla y codificar recorridos por fila,
no matrices RGBA ni nuevas texturas. Ejemplo:

```json
{
  "version": 1,
  "width": 80,
  "height": 80,
  "rows": {
    "0": [[8, 71]],
    "1": [[7, 72]]
  }
}
```

El compilador debe validar dimensiones y rangos, y convertirlos en spans
deterministas.

### Entrantes

Implementar entrantes solo despues de estabilizar la shell:

1. Definir regiones explicitas de puerta y ventana en la mascara.
2. Retirar la cara frontal `depthPixels / 16.0`.
3. Emitir fondo y cuatro jambas interiores.
4. Mantener marcos opacos en el plano frontal.
5. No crear agujeros completos salvo que la plantilla lo declare.

## Integracion con el editor visual

Modificar:

- `map_editor/server.py`
- `map_editor/editor.js`
- `map_editor/index.html`
- `map_editor/editor.css`
- `map_editor/test_server.py`

### Backend

1. Reutilizar `building_profiles.py` para componer previews.
2. Incluir `pixelProfile`, perfil compilado y spans en `/api/map`.
3. Servir una imagen de diagnostico de la mascara sin escribir assets.
4. Validar el candidato con el mismo compilador antes de guardar.
5. Mantener escrituras limitadas a `data/diorama/`.

### Frontend

1. Sustituir la fachada estirada por las mismas franjas y UV recortadas.
2. Dibujar tejados con las muestras de perfil compiladas.
3. Mostrar overlays de silueta, perfil y procedencia de pixel.
4. Permitir editar `bodyHeight`, filas de fachada, slab y eaves.
5. Permitir pintar o borrar recorridos de mascara por pixel.
6. Actualizar WebGL inmediatamente usando un borrador en memoria.
7. Mostrar advertencia si la preview usa fallback matematico.

El editor no debe reimplementar la extraccion con reglas diferentes. El backend
debe entregar el perfil canonico producido por el modulo comun.

## Pruebas requeridas

### Python

Extender `tools/diorama_rules/test_compile_rules.py` con:

1. Composicion de metatiles primarios y secundarios.
2. Flips H/V de subtile.
3. Capas base, foreground y full.
4. Transparencia de indice cero.
5. Perfil conocido de una casa de Villa Raiz.
6. Perfil conocido del laboratorio.
7. Validacion de seals y mascaras.
8. Generacion determinista.
9. Error explicito ante una referencia fuera del mapa.
10. Error si la colocacion de referencia no coincide con dimensiones.

Extender `map_editor/test_server.py` con:

1. Presencia de perfiles en la API.
2. Atlas y mascara validos.
3. Round trip sin modificar campos desconocidos admitidos.
4. Rechazo de rutas de mascara fuera de `data/diorama/`.

### C

Extender `tests/diorama/test_rules.c` con:

1. Propagacion de template y filas de fachada.
2. Fallback de plantillas antiguas.
3. Hashes actualizados solo tras revisar el resultado.

Extender `tests/diorama/test_terrain_mesh.c` con:

1. Fachada sur de dos franjas.
2. Segunda franja recortada a altura parcial.
3. UV de la franja parcial sin estiramiento.
4. Perfil de tejado con muestras por pixel.
5. Intrados y fascia de alero.
6. Eliminacion de caras interiores.
7. Edificio cruzando un limite de chunk.
8. Capacidad maxima de vertices.
9. Firma de chunk sensible al perfil y materiales de fachada.
10. Fallback si faltan celdas fuente en el snapshot.

Extender `tests/diorama/test_metatile_atlas.c` con alpha de capas y recortes UV
si la cobertura existente no prueba los bordes parciales.

## Verificacion visual

Usar primero:

```text
MAP_LITTLEROOT_TOWN
```

Casos:

1. Casa izquierda y derecha muestran las dos filas de fachada sin estirar.
2. Las puertas y ventanas mantienen sus proporciones originales.
3. El tejado conserva la silueta pixelada en ambos extremos.
4. El alero sobresale sin mostrar un rectangulo transparente que escriba depth.
5. El laboratorio usa la misma tecnica con su anchura de siete celdas.
6. La vista del editor coincide con una captura del juego desde el mismo angulo.
7. Cambiar paleta o animacion actualiza arte sin reconstruir la geometria.
8. Cambiar reglas o mascara invalida los chunks afectados.

## Comandos de verificacion

Desde la raiz de `pokeemerald-voxel-project`:

```bash
python3 tools/diorama_rules/validate_rules.py
python3 tools/diorama_rules/compile_rules.py
python3 tools/diorama_rules/compile_rules.py --check
python3 tools/diorama_rules/test_compile_rules.py
python3 map_editor/test_server.py
make -f Makefile_pc test-diorama
```

Build diorama en CachyOS/Arch:

```bash
make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1 \
  PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig \
  -j"$(nproc)"
```

Build clasica obligatoria:

```bash
make -f Makefile_pc NATIVE_LINUX=1 \
  PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig \
  -j"$(nproc)"
```

## Orden de implementacion

1. Congelar el esquema `pixelProfile` con tests de validacion.
2. Implementar fachadas apiladas y UV parciales en juego.
3. Implementar la misma fachada en el editor.
4. Extraer el compositor comun de metatiles Emerald.
5. Generar perfiles de tejado desde una colocacion de referencia.
6. Emitir tejado subdividido, slab y eaves en C.
7. Consumir el perfil canonico en el editor.
8. Agregar mascaras explicitas y recortes geometricos.
9. Agregar entrantes de puertas y ventanas.
10. Optimizar fusion de spans y revisar limites de vertices.
11. Ejecutar tests, build clasica, build diorama y comparacion visual.

No comenzar por recesos o transparencia avanzada antes de corregir las fachadas
y establecer un perfil canonico compartido. La primera entrega debe solucionar
el estiramiento actual sin comprometer el fallback ni la sincronizacion.

## Criterios de aceptacion

- Ningun metatile de fachada se estira sobre mas de su altura configurada.
- Una pared de altura no entera recorta UV y no comprime el ultimo metatile.
- El tejado puede cambiar altura cada `1/16` de celda.
- Los pixels fuera de la silueta no generan geometria ni escriben profundidad.
- Los aleros tienen superficie, fascia e intrados coherentes.
- No existen caras internas entre celdas de una estructura.
- El editor y el juego usan el mismo perfil compilado.
- Los atlas vivos siguen reflejando paletas y animaciones.
- La geometria compilada es determinista.
- Un perfil o mascara invalido nunca sustituye reglas validas.
- `DIORAMA=0` no cambia visual ni funcionalmente.
- Las pruebas diorama y ambas builds de escritorio pasan.
