# Plan de implementación: Pokémon Esmeralda Diorama 3D

**Base técnica:** [`gradenGnostic/pokeemerald-multiplatform`](https://github.com/gradenGnostic/pokeemerald-multiplatform)
**Objetivo:** añadir un modo de representación 3D tipo diorama al overworld de Pokémon Esmeralda, conservando la lógica, los mapas, los sprites, las paletas y los assets originales.
**Fecha de revisión:** 29 de julio de 2026
**Rama revisada:** `master` disponible públicamente en esa fecha
**Recomendación inicial:** fijar un commit concreto del repositorio antes de empezar y desarrollar siempre sobre una rama propia.

---

## 1. Resumen ejecutivo

El proyecto es viable sobre `pokeemerald-multiplatform`, pero no debe plantearse como un filtro que transforme la imagen final de 240×160 píxeles. La ruta correcta es conservar la simulación original y añadir un renderer alternativo que lea el estado lógico del overworld.

El port actual funciona así:

1. El juego ejecuta directamente el código decompilado de Pokémon Esmeralda.
2. El renderer software reproduce la GPU de GBA a partir de VRAM, OAM y paletas.
3. `VDraw()` genera una imagen BGR555 de 240×160.
4. Esa imagen se convierte a ARGB8888 y se sube a una textura SDL.
5. SDL escala y presenta la textura en la ventana.

El modo diorama debe funcionar así:

1. El juego continúa actualizando movimiento, scripts, colisiones, eventos, animaciones y cámara.
2. Al final de cada actualización del overworld se publica un snapshot inmutable del estado visual.
3. El hilo gráfico consume el último snapshot completo.
4. Un renderer OpenGL genera terreno, edificios, agua, billboards y sombras.
5. El framebuffer 2D original se conserva para menús, combates, transiciones, fallback y, más adelante, overlays de interfaz.

### Decisión principal

Usar **SDL2 para ventana, entrada, audio y ciclo de aplicación**, pero sustituir `SDL_Renderer` por un **compositor OpenGL 3.3** en Windows y Linux cuando `DIORAMA=1`.

No se recomienda intentar mezclar un `SDL_Renderer` acelerado y un contexto OpenGL sobre la misma ventana. Es más limpio que OpenGL presente tanto la escena 3D como la textura 2D original.

### Resultado esperado del primer vertical slice

Una versión jugable de:

- Villa Raíz.
- Ruta 101.
- Pueblo Escaso como extensión opcional inmediata.

Debe incluir:

- Suelo 3D.
- Desniveles básicos.
- Árboles y props como billboards o perfiles simples.
- Jugador y NPC animados.
- Cámara inclinada y seguimiento suave.
- Entrada en puertas y cambio de mapas.
- Fallback automático al renderer 2D para diálogos, menús y combates.
- Cero cambios en colisiones, scripts, guardado o lógica de juego.

---

## 2. Qué se ha encontrado en el repositorio

### 2.1 Backend de plataforma

El port selecciona:

```make
TARGET_PLATFORM := PLATFORM_SDL2
TILE_RENDERER := RENDERER_EASY_DRAW
```

El ejecutable de escritorio sigue siendo de 32 bits, con `-m32`. Esta restricción está relacionada con la compatibilidad de punteros y estructuras del código original. No debe intentarse una migración a 64 bits dentro del mismo proyecto de diorama.

### 2.2 Presentación actual

`src/platform/sdl2.c` crea:

- `SDL_Window`.
- `SDL_Renderer` acelerado.
- Una textura streaming ARGB8888.
- Un viewport escalado con opciones de fullscreen, integer scaling, borde y fondo.

`VDraw()` realiza la conversión final:

```c
static uint16_t gbaImage[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static uint32_t image[DISPLAY_WIDTH * DISPLAY_HEIGHT];

DrawFrame(gbaImage);

for (...) {
    // BGR555 -> ARGB8888
}

SDL_UpdateTexture(texture, NULL, image, DISPLAY_WIDTH * sizeof(Uint32));
```

Esto confirma que el framebuffer final ya ha perdido la semántica de mapa, elevación, objeto y capa. El renderer 3D debe engancharse antes, leyendo estructuras del juego.

### 2.3 Modelo de ejecución y sincronización

`AgbMain()` se ejecuta en un hilo separado. `VBlankIntrWait()` publica la disponibilidad de un frame y espera un semáforo. El hilo SDL procesa eventos y presenta el frame.

Consecuencia:

- El renderer no debe leer directamente estructuras mutables mientras el hilo del juego las modifica.
- Es obligatorio introducir un puente de snapshots con doble o triple buffer.
- El hilo gráfico solo puede leer snapshots publicados de forma atómica.

### 2.4 Información de mapas ya disponible

El código expone directamente:

- ID de metatile.
- Comportamiento del metatile.
- Tipo de capa.
- Colisión.
- Elevación.
- Dimensiones del layout.
- Tilesets primario y secundario.
- Paletas.
- Conexiones entre mapas.
- Modificaciones dinámicas del mapa.

Cada celda del mapa contiene:

- 10 bits de ID de metatile.
- 2 bits de colisión.
- 4 bits de elevación.

Los atributos del metatile contienen:

- 8 bits de comportamiento.
- 4 bits de tipo de capa.

Esto proporciona una base semántica mucho mejor que analizar una captura 2D.

### 2.5 Información de objetos y sprites

Los `ObjectEvent` vivos incluyen, entre otros:

- ID gráfico.
- ID de sprite.
- Coordenadas actuales y anteriores.
- Elevación actual y anterior.
- Dirección.
- Estado de movimiento.
- Invisibilidad.
- Prioridad y flags relacionados con terreno.

`gSprites` contiene:

- OAM.
- Posición y offsets.
- Animación y frame.
- Tile inicial.
- Paleta.
- Flips.
- Visibilidad.

La secuencia normal del overworld ya ejecuta:

```text
scripts -> tasks -> animación de sprites -> cámara -> OAM -> paletas -> animaciones de tiles
```

El snapshot debe publicarse después de estas actualizaciones y antes de que el hilo gráfico presente el siguiente frame.

### 2.6 Opciones de plataforma existentes

El menú ya tiene una página `DISPLAY` y el backend dispone de un enum `PlatformSetting`. Es el sitio natural para añadir una entrada de acceso a una página separada `DIORAMA`.

No conviene meter todas las opciones nuevas en la página de pantalla actual porque el espacio vertical es limitado.

---

## 3. Principios de diseño

### 3.1 La lógica original es la autoridad

El renderer 3D nunca decidirá:

- Si el jugador puede caminar.
- Si una puerta se abre.
- Si un NPC bloquea un tile.
- Si se activa un script.
- Si hay un encuentro.
- Si el jugador está sobre o bajo un puente.

Solo representa el estado que el juego ya ha decidido.

### 3.2 No escribir desde el renderer

La dependencia debe ser unidireccional:

```text
Juego original -> snapshot -> renderer 3D
```

Nunca:

```text
renderer 3D -> estructuras del juego
```

La cámara del diorama es visual. El movimiento y el control siguen usando la cuadrícula y las reglas originales.

### 3.3 El renderer 2D siempre debe seguir funcionando

El renderer original se conserva para:

- Combates.
- Menús.
- Pantalla de título.
- Inventario.
- Pokédex.
- Diálogos durante el MVP.
- Minijuegos y escenas especiales.
- Fallback ante errores o mapas sin reglas.
- Comparación visual y depuración.

### 3.4 Sin assets nuevos no significa sin metadatos

Se reutilizarán los gráficos originales. Aun así, es necesario incorporar reglas que indiquen que un metatile representa, por ejemplo:

- Un suelo plano.
- Una pared.
- Un tejado.
- Un árbol.
- Una escalera.
- Agua.
- Un puente.
- Un recorte vertical.
- Parte de una plantilla de edificio.

Estas reglas no son assets gráficos nuevos. Son información de interpretación 3D.

### 3.5 Desarrollo por vertical slices

No intentar cubrir Hoenn completa desde el principio. Cada hito debe producir algo ejecutable y comprobable.

---

## 4. Arquitectura objetivo

```text
+---------------------------------------------------------------+
|                        Código del juego                        |
| scripts | colisiones | eventos | cámara | sprites | mapas     |
+------------------------------+--------------------------------+
                               |
                               | publicación por frame
                               v
+---------------------------------------------------------------+
|                   DioramaSceneSnapshot                         |
| mapa | celdas | objetos | cámara | paleta | clima | estado UI  |
+------------------------------+--------------------------------+
                               |
                     intercambio atómico
                               v
+---------------------------------------------------------------+
|                    Compositor OpenGL                           |
|                                                               |
|  +-----------------------+   +------------------------------+  |
|  | Renderer de diorama   |   | Textura framebuffer GBA     |  |
|  | terreno, props, NPC   |   | UI, combates, fallback      |  |
|  +-----------------------+   +------------------------------+  |
|                         composición final                      |
+---------------------------------------------------------------+
                               |
                               v
                        SDL_GL_SwapWindow
```

### 4.1 Módulos propuestos

```text
include/diorama/
  diorama.h
  scene_snapshot.h
  render_mode.h
  rules.h
  tileset_decode.h
  mesh_builder.h
  object_renderer.h
  camera.h
  diagnostics.h

src/diorama/
  diorama.c
  scene_snapshot.c
  render_mode.c
  rules.c
  tileset_decode.c
  metatile_atlas.c
  mesh_builder.c
  chunk_cache.c
  object_renderer.c
  camera.c
  diagnostics.c

src/diorama/gl/
  gl_backend.c
  gl_resources.c
  gl_shader.c
  gl_scene_renderer.c
  gl_compositor.c

src/data/diorama/
  diorama_rules.generated.c
  diorama_rules.generated.h

data/diorama/
  defaults.json
  tilesets/
  maps/
  buildings/

tools/diorama_rules/
  compile_rules.py
  validate_rules.py

tests/diorama/
  test_rules.c
  test_snapshot.c
  test_mesh_builder.c
```

---

## 5. Decisión sobre el backend gráfico

## 5.1 MVP: OpenGL 3.3 Core en escritorio

### Ventajas

- Integración directa con una ventana SDL2.
- Depth buffer, texturas, shaders, FBO y shadow maps disponibles.
- Buen soporte en Windows y Linux.
- Menor capa de abstracción para un primer prototipo.
- Suficiente para pixel art, mallas por chunks y posprocesado.

### Cambios principales en `src/platform/sdl2.c`

Cuando `ENABLE_DIORAMA` esté activo en escritorio:

1. Crear la ventana con `SDL_WINDOW_OPENGL`.
2. Configurar atributos de contexto antes de crearla.
3. Crear `SDL_GLContext`.
4. Inicializar el backend GL.
5. No crear `SDL_Renderer`.
6. Crear una textura GL para el framebuffer GBA.
7. Presentar mediante `SDL_GL_SwapWindow()`.

La ruta clásica seguirá compilando con `SDL_Renderer` cuando `DIORAMA=0`.

### Configuración sugerida

```c
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
```

### En Windows

Añadir `-lopengl32`.

### En Linux

Añadir el enlace a OpenGL, preferiblemente mediante `pkg-config gl`, además de SDL2 y SDL2_image.

### Carga de funciones GL

Opciones, por orden recomendado:

1. Incluir `glad` generado para OpenGL 3.3 Core como código fuente del proyecto.
2. Implementar un cargador pequeño mediante `SDL_GL_GetProcAddress` si se quiere evitar una dependencia más grande.

No depender de que las cabeceras del sistema expongan todas las funciones modernas.

## 5.2 Fase posterior: OpenGL ES 3.0 para Android

Android no debe bloquear el MVP. Mantener inicialmente:

```text
Windows: diorama disponible
Linux: diorama disponible
Android: renderer 2D original
```

Cuando el renderer de escritorio esté estable:

- Crear backend GLES 3.0.
- Reutilizar shaders con macros de compatibilidad.
- Revisar formatos de textura y precisión.
- Reducir sombras, distancia de dibujado y tamaño de FBO.

## 5.3 Alternativa futura

Si el mantenimiento multiplataforma se vuelve costoso, migrar la capa `src/diorama/gl/` a `sokol_gfx` o `bgfx`. No introducir esa abstracción antes de validar el vertical slice.

---

## 6. Modos de representación

```c
enum DioramaRenderMode
{
    DIORAMA_RENDER_CLASSIC_2D,
    DIORAMA_RENDER_AUTO,
    DIORAMA_RENDER_FORCE_3D,
    DIORAMA_RENDER_DEBUG,
};
```

### `CLASSIC_2D`

Siempre usa el renderer original.

### `AUTO`

Usa diorama únicamente en estados compatibles. Fallback automático en menús, combates, transiciones y escenas no soportadas.

### `FORCE_3D`

Solo para desarrollo. Intenta mantener el renderer 3D en más situaciones y muestra errores visibles.

### `DEBUG`

Añade:

- Rejilla de tiles.
- ID de metatile.
- Elevación.
- Colisión.
- Behavior.
- Límites de chunk.
- Bounding boxes de objetos.
- Estadísticas de malla y draw calls.

---

## 7. Detección del estado del juego

No inferir el estado únicamente a partir de píxeles. Publicar explícitamente el estado desde el código del juego.

```c
enum DioramaSceneKind
{
    DIORAMA_SCENE_UNAVAILABLE,
    DIORAMA_SCENE_OVERWORLD_FREE,
    DIORAMA_SCENE_OVERWORLD_SCRIPTED,
    DIORAMA_SCENE_DIALOGUE,
    DIORAMA_SCENE_MENU,
    DIORAMA_SCENE_BATTLE,
    DIORAMA_SCENE_TRANSITION,
    DIORAMA_SCENE_SPECIAL,
};
```

### Hooks recomendados

- `CB2_Overworld()` y `CB2_OverworldBasic()`:
  - Marcar contexto de overworld.
  - Publicar snapshot después de `OverworldBasic()`.
- `CB2_LoadMap()` y callbacks de retorno al campo:
  - Marcar transición.
  - Invalidar mapa, tileset, atlas y chunks.
- Finalización de carga de mapa:
  - Incrementar `mapGeneration`.
  - Crear snapshot completo.
- Entrada a batalla, menús y escenas especiales:
  - Cambiar `sceneKind` o usar el callback principal como parte de un registro explícito.

### Recomendación para el MVP

Usar diorama solo cuando se cumpla todo:

```text
gMain.callback2 == CB2_Overworld
no hay transición de paleta activa
no hay menú modal activo
no hay cuadro de diálogo visible
no hay escena especial marcada
el snapshot es válido
el mapa tiene reglas mínimas
```

En cualquier otro caso, presentar el framebuffer 2D completo.

---

## 8. Puente seguro entre el hilo del juego y el hilo gráfico

## 8.1 Problema

El hilo SDL y el hilo de `AgbMain()` pueden operar al mismo tiempo. Leer `gMapHeader`, `gObjectEvents`, `gSprites`, VRAM o paletas directamente desde OpenGL puede producir:

- Estados parciales.
- Lecturas inconsistentes.
- Crashes.
- Glitches intermitentes difíciles de reproducir.

## 8.2 Solución

Triple buffer de snapshots:

```c
#define DIORAMA_SNAPSHOT_COUNT 3

struct DioramaSnapshotExchange
{
    struct DioramaSceneSnapshot snapshots[DIORAMA_SNAPSHOT_COUNT];
    SDL_atomic_t publishedIndex;
    SDL_atomic_t sequence;
};
```

### Flujo

1. El hilo del juego elige un buffer que no sea el publicado.
2. Rellena el snapshot completo.
3. Ejecuta una barrera de memoria.
4. Publica el índice y aumenta la secuencia atómicamente.
5. El hilo gráfico lee el último índice publicado.
6. El renderer nunca escribe en ese snapshot.

### Regla importante

No guardar punteros a estructuras mutables del juego dentro del snapshot. Copiar valores y referencias estables identificadas por generación.

## 8.3 Estructura propuesta

```c
#define DIORAMA_MAX_VISIBLE_CELLS 4096
#define DIORAMA_MAX_OBJECTS       64

struct DioramaCellSnapshot
{
    s16 mapX;
    s16 mapY;
    u16 metatileId;
    u8 behavior;
    u8 layerType;
    u8 collision;
    u8 elevation;
    u16 flags;
};

struct DioramaObjectSnapshot
{
    u8 active;
    u8 localId;
    u8 graphicsId;
    u8 spriteId;
    u8 elevation;
    u8 facingDirection;
    u8 movementDirection;
    u8 flags;

    s16 currentMapX;
    s16 currentMapY;
    s16 previousMapX;
    s16 previousMapY;

    s16 screenX;
    s16 screenY;
    s16 spriteX2;
    s16 spriteY2;

    u16 tileNum;
    u8 paletteNum;
    u8 oamShape;
    u8 oamSize;
    u8 hFlip;
    u8 vFlip;
};

struct DioramaSceneSnapshot
{
    u32 sequence;
    u32 mapGeneration;
    u32 paletteGeneration;
    u32 tilesetAnimationGeneration;

    enum DioramaSceneKind sceneKind;
    u8 mapGroup;
    u8 mapNum;
    u16 mapLayoutId;
    u8 mapType;
    u8 weather;

    s16 cameraMapX;
    s16 cameraMapY;
    s16 cameraPixelX;
    s16 cameraPixelY;

    u16 visibleCellCount;
    struct DioramaCellSnapshot cells[DIORAMA_MAX_VISIBLE_CELLS];

    u8 objectCount;
    struct DioramaObjectSnapshot objects[DIORAMA_MAX_OBJECTS];

    u16 fadedPalette[PLTT_BUFFER_SIZE];
};
```

La estructura final puede optimizarse después. Primero debe ser clara y determinista.

---

## 9. Extracción del mapa

## 9.1 Fuente de verdad

Usar las funciones existentes:

- `MapGridGetMetatileIdAt()`.
- `MapGridGetMetatileBehaviorAt()`.
- `MapGridGetMetatileLayerTypeAt()`.
- `MapGridGetCollisionAt()`.
- `MapGridGetElevationAt()`.
- `GetCameraFocusCoords()`.
- `GetMapConnectionAtPos()`.

Estas funciones ya entienden el buffer ampliado y las conexiones de mapas. No duplicar esa lógica inicialmente.

## 9.2 Área copiada al snapshot

Para el MVP, copiar un cuadrado alrededor de la cámara:

```text
radio horizontal: 16 metatiles
radio vertical:   16 metatiles
```

Esto da 33×33 celdas, muy por debajo del máximo sugerido.

Ventajas frente a copiar el mapa completo:

- Snapshot pequeño.
- Compatible con mapas conectados.
- Menos trabajo al cambiar de mapa.
- Suficiente para ocultar el pop-in con la cámara inclinada.

En una fase posterior puede mantenerse una caché completa por mapa.

## 9.3 Coordenadas

Definir desde el principio una convención única:

```text
X mundo: este/oeste
Z mundo: norte/sur
Y mundo: altura
1 metatile: 1,0 unidad de mundo
1 píxel de sprite: 1/16 unidad de mundo
```

No mezclar `Y` de pantalla con `Y` de altura.

---

## 10. Decodificación de tilesets y atlas

## 10.1 Datos disponibles

Cada `MapLayout` referencia tilesets primario y secundario. Cada tileset proporciona:

- Gráficos de tiles 4bpp.
- Paletas.
- Composición de metatiles.
- Atributos.
- Callbacks de animación.

Cada metatile usa ocho tiles de 8×8, normalmente cuatro por capa.

## 10.2 Estrategia del MVP

Al cambiar de tileset:

1. Decodificar tiles 4bpp de 8×8.
2. Aplicar índice de paleta.
3. Aplicar flip horizontal y vertical.
4. Componer cada metatile en una o varias texturas RGBA de 16×16.
5. Empaquetar los metatiles en un atlas GL.
6. Guardar UV por metatile y capa.

### Ventaja

Es sencillo y fácil de depurar.

### Inconveniente

Las animaciones y cambios de paleta pueden requerir actualizaciones parciales del atlas.

## 10.3 Evolución recomendada

### Etapa A

Atlas RGBA generado en CPU.

### Etapa B

Actualizar únicamente metatiles afectados por animaciones.

### Etapa C

Textura indexada y lookup de paleta en shader:

- R8 para índices de color.
- Textura de paleta separada.
- Cambios meteorológicos y fades sin reconstruir todo el atlas.

No empezar por la etapa C. Primero validar geometría, coordenadas y reglas.

## 10.4 Separación de capas

Conservar por metatile:

- Capa inferior.
- Capa media.
- Capa superior.
- Máscara de transparencia.

El `layerType` original debe ayudar a decidir qué parte se usa como:

- Textura del suelo.
- Overlay plano.
- Fachada.
- Recorte vertical.
- Elemento que cubre al jugador.

---

## 11. Sistema de reglas 3D

## 11.1 Orden de resolución

Resolver la forma con esta prioridad:

1. Override específico de mapa y coordenada.
2. Override específico de tileset y metatile.
3. Plantilla de edificio reconocida.
4. Regla por comportamiento.
5. Regla por colisión y elevación.
6. Heurística visual.
7. Fallback plano.

Esto evita que una heurística global rompa casos especiales.

## 11.2 Tipos iniciales de forma

```c
enum DioramaShape
{
    DIORAMA_SHAPE_FLAT,
    DIORAMA_SHAPE_EXTRUDED,
    DIORAMA_SHAPE_CLIFF,
    DIORAMA_SHAPE_LEDGE,
    DIORAMA_SHAPE_STAIRS,
    DIORAMA_SHAPE_WATER,
    DIORAMA_SHAPE_BRIDGE,
    DIORAMA_SHAPE_BILLBOARD,
    DIORAMA_SHAPE_CUTOUT,
    DIORAMA_SHAPE_ROOF,
    DIORAMA_SHAPE_BUILDING_PART,
    DIORAMA_SHAPE_HIDDEN,
};
```

## 11.3 Esquema JSON propuesto

`data/diorama/defaults.json`:

```json
{
  "version": 1,
  "elevationStep": 0.5,
  "defaultShape": "flat",
  "behaviors": {
    "MB_TALL_GRASS": {
      "shape": "cutout",
      "height": 0.65,
      "castsShadow": false
    },
    "MB_POND_WATER": {
      "shape": "water",
      "height": 0.02,
      "animated": true
    }
  }
}
```

`data/diorama/tilesets/general.json`:

```json
{
  "tileset": "gTileset_General",
  "metatiles": {
    "145": {
      "shape": "billboard",
      "height": 2.2,
      "anchor": "bottom-center"
    },
    "312": {
      "shape": "roof",
      "baseHeight": 1.6,
      "profile": "gable-x"
    }
  }
}
```

`data/diorama/maps/littleroot_town.json`:

```json
{
  "map": "MAP_LITTLEROOT_TOWN",
  "overrides": [
    {
      "x": 10,
      "y": 7,
      "shape": "building-part",
      "building": "player_house"
    }
  ]
}
```

## 11.4 Compilación a C

No añadir un parser JSON al runtime del juego durante el MVP.

Usar:

```text
JSON editable -> herramienta Python -> tablas C generadas
```

Ventajas:

- Cero parsing durante el juego.
- Errores detectados al compilar.
- Compatible con el modelo del repositorio.
- Lookup rápido.
- Se pueden generar hashes y tablas compactas.

## 11.5 Herramienta de validación

`validate_rules.py` debe comprobar:

- IDs de mapa válidos.
- Símbolos de tileset válidos.
- Metatiles dentro de rango.
- Alturas razonables.
- Perfiles existentes.
- Overrides duplicados.
- Plantillas incompletas.
- Referencias a comportamientos válidos.

---

## 12. Generación de geometría

## 12.1 Chunks

Usar chunks de **8×8 metatiles** para el MVP.

Cada chunk mantiene:

```c
struct DioramaChunk
{
    s16 chunkX;
    s16 chunkY;
    u32 mapGeneration;
    u32 rulesGeneration;
    bool dirty;

    GLuint terrainVao;
    GLuint terrainVbo;
    GLuint terrainIbo;
    u32 indexCount;
};
```

### Motivos para 8×8

- Reconstrucción barata.
- Buena granularidad para cambios dinámicos.
- Culling sencillo.
- Menos geometría regenerada al modificar un tile.

Si el número de draw calls crece demasiado, agrupar chunks vecinos o pasar a 16×16.

## 12.2 Altura base

```c
worldY = normalizedElevation * elevationStep;
```

No usar directamente el valor de cuatro bits como metros visuales. Algunos valores tienen semántica especial. Crear una función de normalización:

```c
float Diorama_NormalizeElevation(u8 elevation, u8 behavior);
```

## 12.3 Terreno

Por cada celda:

1. Crear cara superior.
2. Comparar altura con vecinos.
3. Crear caras laterales donde el vecino sea más bajo.
4. Aplicar UV del metatile.
5. Separar materiales opacos, recortes alfa y transparentes.

## 12.4 Acantilados y ledges

No extruir únicamente un cubo genérico. Los ledges necesitan orientación y forma propia para conservar la lectura del pixel art.

## 12.5 Agua

MVP:

- Plano ligeramente por encima del suelo.
- Textura original animada.
- Alpha o mezcla mínima.
- Sin reflejos.

Fase posterior:

- Distorsión sutil en shader.
- Reflejos de sprites seleccionados.
- Profundidad visual.
- Orillas suavizadas mediante máscara.

## 12.6 Edificios

Implementar en tres niveles:

### Nivel 1: bloque simple

Extrusión de la huella con fachada y parte superior.

### Nivel 2: perfiles de tejado

- Gable X.
- Gable Z.
- Tejado escalonado.
- Tejado plano.

### Nivel 3: plantillas

Reconocer un conjunto de metatiles y construir un edificio coherente con:

- Fachadas.
- Laterales.
- Tejado.
- Voladizos.
- Puerta.
- Sombras.

No modelar partes nunca visibles más allá de lo necesario para la cámara permitida.

---

## 13. Renderizado de jugador, NPC y objetos

## 13.1 Estrategia del MVP

Representar cada personaje como un billboard vertical:

- Textura original del frame actual.
- Alpha test.
- Punto de anclaje en los pies.
- Ligera inclinación o alineación parcial con la cámara.
- Escala derivada de las dimensiones del gráfico.
- Posición interpolada entre coordenadas anterior y actual.

## 13.2 Obtención del frame

Ruta recomendada para el MVP:

1. A partir de `ObjectEvent.spriteId`, leer la copia estable del estado de `gSprites[spriteId]` durante la creación del snapshot.
2. Capturar:
   - `tileNum`.
   - `paletteNum`.
   - Shape y size de OAM.
   - Flips.
   - Posición y offsets.
3. Decodificar el gráfico desde la memoria OBJ copiada al snapshot o desde una caché preparada en el hilo del juego.
4. Subir una textura solo cuando cambie la clave visual del frame.

Clave de caché sugerida:

```c
struct DioramaSpriteFrameKey
{
    u16 tileNum;
    u8 paletteNum;
    u8 shape;
    u8 size;
    u8 hFlip;
    u8 vFlip;
    u32 objVramGeneration;
    u32 paletteGeneration;
};
```

No decodificar todos los sprites cada frame.

## 13.3 Interpolación

El juego mantiene su ritmo original. El compositor puede renderizar a 60 Hz o a la frecuencia de la pantalla interpolando:

```c
position = lerp(previousPosition, currentPosition, frameAlpha);
```

La interpolación debe desactivarse:

- Durante warps.
- Al teletransportar objetos.
- Si cambia el mapa.
- Si la distancia supera un umbral.

## 13.4 Props del mapa

Para árboles, flores, postes y carteles:

- Empezar con billboards o cross-billboards.
- Migrar solo los objetos que realmente mejoren con volumen.
- Evitar convertir cada píxel transparente en geometría.

---

## 14. Cámara 3D

## 14.1 Cámara inicial

- Proyección perspectiva suave.
- Pitch configurable, por ejemplo 45 a 60 grados.
- Yaw fijo, inicialmente alineado con el mapa.
- Distancia calculada para mostrar un área parecida a la vista original.
- Seguimiento del jugador con amortiguación.

## 14.2 Opciones

```text
Pitch: 35° a 70°
Zoom: 0,75x a 1,50x
Seguimiento suave: sí/no
Rotación de cámara: bloqueada en MVP
```

No permitir rotación libre al principio. Los assets originales no contienen laterales completos y una cámara libre revelaría geometría inventada o huecos.

## 14.3 Composición diorama

Añadir más adelante:

- Foco en el jugador.
- Profundidad de campo.
- Tilt-shift.
- Viñeta muy ligera opcional.

Estos efectos son cosméticos. Deben llegar después de que mapa, objetos y transiciones sean correctos.

---

## 15. Integración de interfaz 2D

## 15.1 MVP: fallback completo

Cuando aparezca cualquier UI modal:

1. Renderizar el framebuffer original completo.
2. Pausar o mantener en segundo plano la escena 3D.
3. Volver al diorama al cerrar la UI.
4. Aplicar transición breve o respetar el fade del juego para ocultar el cambio.

Es la solución con menor riesgo.

## 15.2 Fase posterior: overlay sobre 3D

El renderer software de GBA mezcla mundo y UI. Para superponer diálogos sobre el mundo 3D habrá que separar capas.

Propuesta:

```c
void DrawFrameLayers(
    u16 *worldOutput,
    u16 *uiOutput,
    u8 *uiAlpha
);
```

Posibles criterios:

- BG2/BG3 como mundo.
- BG0/BG1 como interfaz, según el estado.
- OAM dividido entre objetos del campo y sprites de UI.
- Ventanas del motor como UI.

No asumir que la misma separación vale para todas las escenas. Crear perfiles por `sceneKind`.

## 15.3 Resultado final deseado

```text
Escena 3D
+ cuadro de diálogo pixel-perfect
+ retrato o iconos originales
+ fades y transiciones originales
```

---

## 16. Cambios dinámicos del mapa

Añadir invalidación en:

- `MapGridSetMetatileIdAt()`.
- `MapGridSetMetatileEntryAt()`.
- `MapGridSetMetatileImpassabilityAt()`.

Cada cambio debe:

1. Marcar la celda modificada.
2. Calcular el chunk afectado.
3. Marcar también chunks vecinos si pueden cambiar caras laterales.
4. Aumentar una secuencia de cambios.
5. Copiar el nuevo estado al siguiente snapshot.

```c
void Diorama_MarkCellDirty(s16 mapX, s16 mapY);
```

Casos a probar:

- Árbol cortado.
- Roca destruida.
- Puertas.
- Puentes móviles.
- Puertas de gimnasio.
- Bases secretas.
- Puzles.
- Cambios de agua o terreno por scripts.

---

## 17. Conexiones, warps y mapas

## 17.1 Conexiones exteriores

Como las funciones de `MapGrid` ya resuelven el buffer ampliado, el MVP puede generar el área alrededor de la cámara sin construir manualmente un grafo de mapas.

Más adelante:

- Precargar chunks del mapa conectado.
- Mantener atlas si comparte tilesets.
- Ocultar cargas con la geometría vecina.

## 17.2 Warp

Secuencia recomendada:

1. `sceneKind = TRANSITION`.
2. Presentar fade original en 2D.
3. Invalidar caché de mapa.
4. Cargar nuevo layout y tilesets.
5. Generar atlas y chunks iniciales.
6. Publicar snapshot válido.
7. Volver a `OVERWORLD_FREE`.

## 17.3 Interiores

Tratar cada interior como un espacio independiente, sin necesidad de continuidad espacial real con el exterior.

La cámara puede tener un perfil diferente:

- Pitch algo menor.
- Zoom más cercano.
- Altura de paredes limitada.
- Ocultación de paredes frontales si bloquean la vista.

---

## 18. Clima, paletas y animaciones

## 18.1 Orden recomendado

1. Animaciones de tiles.
2. Fades de paleta.
3. Lluvia.
4. Niebla.
5. Ceniza y arena.
6. Reflejos.
7. Efectos especiales de mapas concretos.

## 18.2 Paletas

Usar `gPlttBufferFaded` como fuente de color visible cuando se cree el snapshot. Esto preserva:

- Fades.
- Tintes.
- Cambios horarios si el proyecto los incorpora.
- Efectos globales de color.

## 18.3 Lluvia y partículas

Inicialmente, conservar el framebuffer 2D para escenas con clima complejo o dibujar partículas simples en espacio de pantalla.

Después, mapear el clima original a emisores 3D sin cambiar su lógica.

---

## 19. Opciones de usuario

Añadir a `PlatformSetting`:

```c
enum PlatformSetting
{
    // existentes
    PLATFORM_SETTING_RENDER_MODE,
    PLATFORM_SETTING_DIORAMA_PITCH,
    PLATFORM_SETTING_DIORAMA_ZOOM,
    PLATFORM_SETTING_DIORAMA_SHADOWS,
    PLATFORM_SETTING_DIORAMA_POSTPROCESS,
    PLATFORM_SETTING_DIORAMA_DEBUG,
};
```

### Nueva página `DIORAMA`

```text
Modo:          2D / Auto / 3D / Debug
Inclinación:   Baja / Media / Alta
Zoom:          75 / 100 / 125 / 150
Sombras:       No / Blob / Alta
Posprocesado:  No / Ligero / Completo
Depuración:    No / Sí
Volver
```

### Configuración persistente

Claves sugeridas:

```ini
renderMode=1
dioramaPitch=55
dioramaZoom=100
dioramaShadows=1
dioramaPostprocess=0
dioramaDebug=0
```

---

## 20. Cambios de build

## 20.1 Variable de compilación

```make
DIORAMA ?= 0

ifeq ($(DIORAMA),1)
    CPPFLAGS += -DENABLE_DIORAMA
endif
```

Uso:

```bash
make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1
```

## 20.2 Bibliotecas

Linux:

```make
PLATFORM_INCLUDES += $(shell pkg-config --libs gl)
```

Windows:

```make
PLATFORM_INCLUDES += -lopengl32
```

## 20.3 Mantener dos rutas

```text
DIORAMA=0: build actual, sin cambios funcionales
DIORAMA=1: compositor OpenGL y renderer 3D disponible
```

El build clásico debe mantenerse verde en CI en todo momento.

## 20.4 No mezclar con la migración a 64 bits

Mantener `-m32` durante todo el proyecto inicial. Separar cualquier investigación de 64 bits en otra rama y otro roadmap.

---

## 21. Archivos existentes que probablemente se modificarán

| Archivo | Cambio previsto |
|---|---|
| `Makefile_pc` | Flag `DIORAMA`, OpenGL, fuentes nuevas y generación de reglas |
| `src/platform/sdl2.c` | Contexto GL, compositor, presentación y ruta clásica condicional |
| `include/platform.h` | Nuevas opciones de renderer |
| `src/option_menu.c` | Entrada y página de opciones de diorama |
| `include/strings.h` | Declaraciones de textos nuevos |
| `src/strings.c` o datos equivalentes | Textos del menú |
| `src/overworld.c` | Publicación de snapshot y estado de escena |
| `src/fieldmap.c` | Invalidación de chunks en cambios de metatile |
| `include/fieldmap.h` | Hooks o notificaciones del diorama |
| `src/event_object_movement.c` | Solo si hace falta publicar datos adicionales de objetos |
| `src/platform/gba_easy_draw.c` | Más adelante, separación de mundo e interfaz |

Evitar tocar lógica de movimiento, colisión o scripts salvo para emitir notificaciones pasivas.

---

## 22. Roadmap por fases

Las estimaciones son orientativas para una persona con experiencia en C, SDL y OpenGL. No incluyen aprendizaje profundo del motor ni creación masiva de reglas para toda Hoenn.

## Fase 0: baseline reproducible

**Esfuerzo orientativo:** 2 a 4 días.

### Tareas

- Hacer fork.
- Fijar commit base.
- Compilar Windows y Linux sin modificaciones.
- Crear guardados de prueba.
- Capturar screenshots de referencia.
- Añadir CI para `DIORAMA=0`.
- Documentar toolchain de 32 bits.
- Crear rama `feature/diorama`.

### Entregables

- Build reproducible.
- Checklist de escenarios base.
- Guardados en Villa Raíz, Ruta 101, combate, menú y un interior.

### Criterios de aceptación

- El ejecutable original funciona igual que antes.
- Los guardados cargan y persisten.
- CI produce al menos un build de escritorio.

---

## Fase 1: compositor OpenGL con paridad 2D

**Esfuerzo orientativo:** 1 a 2 semanas.

### Tareas

- Crear ventana SDL con OpenGL.
- Inicializar GL 3.3.
- Subir el framebuffer original como textura.
- Reproducir nearest-neighbor, viewport, fullscreen e integer scaling.
- Reimplementar fondo y borde en el compositor.
- Añadir fallback a negro si faltan recursos.
- Mantener la ruta SDL_Renderer cuando `DIORAMA=0`.

### Entregables

- Build `DIORAMA=1` que sigue mostrando el juego exactamente en 2D.

### Criterios de aceptación

- Comparación pixel-perfect del área de juego a escala 1x.
- Sin cambios en input, audio, speedup o sincronización.
- Resize y fullscreen estables.
- No hay leaks de contexto o texturas.

---

## Fase 2: estado de escena y snapshots

**Esfuerzo orientativo:** 1 semana.

### Tareas

- Crear `DioramaSceneSnapshot`.
- Implementar triple buffer.
- Publicar estado desde `CB2_Overworld()`.
- Detectar mapa, cámara, celdas visibles y objetos.
- Crear overlay de depuración en texto.
- Añadir modo `AUTO`.

### Entregables

- El hilo GL puede visualizar una rejilla abstracta basada en el mapa real.

### Criterios de aceptación

- Sin lecturas directas de globals mutables desde el hilo GL.
- Sin carreras detectables al entrar y salir de mapas.
- Secuencia de snapshots siempre monotónica.
- Fallback 2D al no existir snapshot válido.

---

## Fase 3: atlas de metatiles y mapa plano

**Esfuerzo orientativo:** 1 a 2 semanas.

### Tareas

- Decodificar 4bpp.
- Aplicar paletas y flips.
- Componer metatiles de 16×16.
- Crear atlas GL.
- Dibujar un plano texturizado por celda.
- Implementar cámara perspectiva fija.
- Resolver transparencia básica.

### Entregables

- Villa Raíz representada como plano 3D inclinado con los gráficos originales.

### Criterios de aceptación

- IDs de metatile y texturas coinciden con el renderer 2D.
- No hay bleeding entre celdas del atlas.
- Los cambios de mapa reconstruyen el atlas correctamente.
- 60 FPS en una GPU de escritorio modesta.

---

## Fase 4: elevación y mallas por chunks

**Esfuerzo orientativo:** 2 semanas.

### Tareas

- Normalizar elevaciones.
- Generar caras superiores y laterales.
- Añadir chunks 8×8.
- Frustum culling.
- Marcar chunks sucios.
- Añadir modo wireframe/debug.
- Implementar ledges y acantilados básicos.

### Entregables

- Terreno con profundidad y desniveles funcionales.

### Criterios de aceptación

- El jugador visualmente ocupa la elevación correcta.
- No aparecen grietas entre chunks.
- Cambiar un metatile reconstruye únicamente los chunks necesarios.
- La colisión sigue siendo completamente original.

---

## Fase 5: personajes y NPC como billboards

**Esfuerzo orientativo:** 2 semanas.

### Tareas

- Extraer frame, paleta y flips de OAM.
- Crear caché de texturas de sprites.
- Dibujar jugador y NPC.
- Anclar sprites por los pies.
- Interpolar movimiento.
- Añadir shadow blob.
- Resolver orden por profundidad.

### Entregables

- Exploración completa de Villa Raíz con jugador y NPC animados.

### Criterios de aceptación

- Todos los frames de caminar se ven correctamente.
- Flips y direcciones coinciden con el juego original.
- Sin vibración de sprites al mover la cámara.
- Warps desactivan interpolación.

---

## Fase 6: reglas y vertical slice Villa Raíz + Ruta 101

**Esfuerzo orientativo:** 2 a 4 semanas.

### Tareas

- Implementar JSON y compilador de reglas.
- Clasificar árboles, hierba, carteles, casas y bordes.
- Añadir perfiles básicos de edificios.
- Crear reglas de Villa Raíz y Ruta 101.
- Automatizar validación.
- Añadir capturas golden.

### Entregables

- Primera demo pública interna del diorama.

### Criterios de aceptación

- Todo el recorrido Villa Raíz -> Ruta 101 es jugable.
- Los edificios tienen volumen coherente desde la cámara permitida.
- No existen huecos críticos en la geometría.
- El juego vuelve a 2D al mostrar diálogo o menú.
- El guardado sigue siendo compatible.

---

## Fase 7: cambios dinámicos, conexiones y transiciones

**Esfuerzo orientativo:** 2 a 3 semanas.

### Tareas

- Hooks de metatiles dinámicos.
- Reconstrucción parcial.
- Precarga de conexiones.
- Warps robustos.
- Fades entre 3D y 2D.
- Pruebas de puertas, Cut, Rock Smash y puzles.

### Criterios de aceptación

- Ningún cambio visual exige recargar el mapa completo.
- Las conexiones exteriores no enseñan vacío cerca de la cámara.
- Las transiciones no muestran un frame del mapa anterior.

---

## Fase 8: agua, animaciones y clima básico

**Esfuerzo orientativo:** 3 a 5 semanas.

### Tareas

- Actualizaciones parciales del atlas.
- Animación de agua.
- Paletas faded.
- Lluvia y niebla básicas.
- Reflejos simples.
- Surf y elevación sobre agua.

### Criterios de aceptación

- El aspecto sigue los cambios de paleta originales.
- El agua animada no obliga a regenerar toda la escena.
- Surf, reflejos y objetos sobre agua mantienen el orden correcto.

---

## Fase 9: interfaz 2D sobre el mundo 3D

**Esfuerzo orientativo:** 4 a 8 semanas.

### Tareas

- Separar salidas del renderer software.
- Extraer alpha de ventanas y UI.
- Clasificar sprites de campo frente a sprites de interfaz.
- Superponer diálogos, map popup y menú de inicio sobre el diorama.
- Conservar fallback completo para escenas complejas.

### Criterios de aceptación

- Los diálogos aparecen sobre el mundo 3D sin fondo 2D residual.
- El texto conserva resolución y timings originales.
- Las transiciones de paleta siguen siendo correctas.

---

## Fase 10: interiores y edificios avanzados

**Esfuerzo orientativo:** 2 a 4 meses, progresivo.

### Tareas

- Perfiles de cámara interior.
- Ocultación de paredes frontales.
- Muebles como props o extrusiones.
- Plantillas de Centros Pokémon, tiendas, casas y gimnasios.
- Reglas por tileset interior.
- Herramienta visual de edición de alturas opcional.

### Criterios de aceptación

- Todos los interiores obligatorios de la historia son legibles.
- Ningún objeto interactivo queda oculto.
- Las paredes no bloquean al jugador desde la cámara fija.

---

## Fase 11: sombras y posprocesado

**Esfuerzo orientativo:** 2 a 4 semanas.

### Tareas

- Shadow map direccional.
- PCF ligero.
- Ambient occlusion aproximada o baked por vértice.
- Depth of field.
- Tilt-shift.
- Opciones de calidad.

### Criterios de aceptación

- Los efectos pueden desactivarse.
- No degradan la legibilidad del pixel art.
- El modo bajo mantiene 60 FPS en hardware objetivo.

---

## Fase 12: cobertura completa y Android

**Esfuerzo orientativo:** varios meses.

### Tareas

- Auditoría de todos los mapas.
- Reglas restantes.
- Casos especiales.
- Battle Frontier, cuevas, buceo y escenas únicas.
- Backend GLES 3.0.
- Perfiles de rendimiento móvil.
- Documentación para contribuidores.

---

## 23. Secuencia recomendada de pull requests

1. `build: add DIORAMA feature flag`
2. `platform: add OpenGL compositor with 2D framebuffer parity`
3. `diorama: add scene state and snapshot exchange`
4. `diorama: expose visible field map cells`
5. `diorama: decode tilesets and build metatile atlas`
6. `diorama: render flat overworld map`
7. `diorama: add chunked elevation meshing`
8. `diorama: render object-event billboards`
9. `diorama: add compiled rules pipeline`
10. `content: add Littleroot and Route 101 diorama rules`

Cada PR debe ser ejecutable y mantener el modo clásico.

---

## 24. Pruebas

## 24.1 Unitarias

- Normalización de elevación.
- Resolución de reglas.
- Cálculo de chunks.
- Generación de caras laterales.
- Coordenadas mapa -> mundo.
- Decodificación 4bpp.
- Flips de tiles.
- Composición de metatiles.
- Interpolación y detección de teletransporte.

## 24.2 Snapshots deterministas

Para mapas conocidos, guardar una representación textual o binaria estable de:

- Celdas visibles.
- Objetos.
- Cámara.
- Generaciones.

Comparar en CI.

## 24.3 Geometry tests

Por chunk:

- Número de vértices.
- Número de índices.
- Bounding box.
- Hash de geometría.

## 24.4 Golden screenshots

Capturas de referencia para:

- Villa Raíz.
- Ruta 101.
- Interior de casa.
- Agua.
- Noche o fade.
- Diálogo y fallback 2D.

## 24.5 Regresión de gameplay

- Movimiento.
- Bicicleta.
- Surf.
- Colisiones.
- Ledges.
- Warps.
- Scripts.
- Encuentros.
- Combates.
- Guardado y carga.
- Speedup.
- Mandos.
- Cambio de ventana y fullscreen.

## 24.6 Thread safety

- Cambio rápido de mapas.
- Resize mientras se carga un mapa.
- Alt-tab repetido.
- Speedup durante warps.
- Cierre de la aplicación durante VBlank.
- Instrumentación con ThreadSanitizer en una build Linux auxiliar cuando sea posible.

---

## 25. Presupuestos de rendimiento

Objetivo inicial: **60 FPS a 1280×720** en un PC modesto con soporte OpenGL 3.3.

### CPU

- Publicación de snapshot: menos de 1 ms de media.
- Reconstrucción de un chunk: menos de 1 ms de media.
- Ninguna reconstrucción completa por frame.
- Decodificación de sprite solo al cambiar el frame visual.

### GPU

- Menos de 200 draw calls en el vertical slice.
- Atlas único por combinación de tilesets siempre que sea posible.
- Un batch para terreno opaco.
- Un batch para cutouts.
- Un batch o instancing para billboards.
- Transparencia ordenada solo donde sea necesaria.

### Memoria

- Snapshots acotados.
- Caché LRU de frames de sprite.
- Liberación de atlas y chunks al cambiar de tileset.
- Métricas visibles en modo debug.

---

## 26. Riesgos y mitigaciones

| Riesgo | Impacto | Mitigación |
|---|---:|---|
| Lecturas concurrentes de globals | Crashes y glitches | Snapshot inmutable con triple buffer |
| Mezcla de mundo y UI en framebuffer | Bloquea diálogos sobre 3D | Fallback 2D en MVP, separación de capas después |
| Assets 2D ambiguos | Geometría incoherente | Reglas por behavior, tileset, mapa y plantilla |
| Animaciones de tiles | Atlas obsoleto | Generaciones y updates parciales |
| Cambios de paleta | Colores incorrectos | Usar paleta faded o shader indexado |
| Restricción de 32 bits | Toolchain incómodo | Mantenerla y no mezclar migraciones |
| SDL_Renderer + OpenGL | Contextos conflictivos | Compositor OpenGL único cuando DIORAMA=1 |
| Demasiados draw calls | Bajo rendimiento | Chunks, atlas y batching |
| Cámara revela zonas sin arte | Huecos visuales | Pitch/yaw restringidos y geometría cerrada mínima |
| Interiores tapan al jugador | Mala legibilidad | Perfil interior y ocultación de paredes |
| Android ralentiza el MVP | Retraso general | Posponer GLES hasta estabilizar escritorio |
| Cobertura manual enorme | Proyecto interminable | Vertical slices y priorización por historia |

---

## 27. Herramientas de desarrollo recomendadas

## 27.1 Overlay debug dentro del juego

Mostrar:

```text
Map: group/num/layout
Camera: x/y + pixel offset
Snapshot sequence
Map generation
Visible cells
Active chunks / dirty chunks
Vertices / triangles
Draw calls
Atlas generation
Objects / cached sprite frames
Renderer mode / fallback reason
Frame CPU / GPU
```

## 27.2 Selector de celda

Con ratón en modo debug:

- Raycast contra terreno.
- Mostrar mapa X/Y.
- Metatile ID.
- Behavior.
- Colisión.
- Elevación.
- Regla aplicada.
- Fuente de la regla.

## 27.3 Hot reload de reglas

No es necesario para el juego final, pero puede acelerar mucho el trabajo:

1. Herramienta externa recompila JSON a un blob binario de desarrollo.
2. El renderer recarga el blob al pulsar una tecla.
3. Se invalidan chunks.

Mantener la generación C para builds de distribución.

## 27.4 Exportación diagnóstica

Permitir exportar:

- Snapshot JSON.
- Atlas PNG.
- Chunk OBJ o glTF solo para depuración.
- Captura 2D y 3D lado a lado.

No incluir assets exportados en distribuciones públicas.

---

## 28. Alcance recomendado del primer release

### Incluir

- Windows y Linux.
- Modo 2D clásico.
- Modo diorama automático.
- Exteriores principales de la primera zona.
- Jugador y NPC.
- Elevación básica.
- Casas, árboles, hierba, carteles y agua básica.
- Fallback 2D para UI y combates.
- Configuración persistente.
- Overlay debug.

### No incluir todavía

- Cámara libre.
- Todos los interiores.
- Sombras avanzadas.
- Depth of field.
- Android.
- Battle Frontier.
- Buceo completo.
- Separación completa de UI.
- Reconstrucción automática perfecta de todos los edificios.

---

## 29. Estimación global orientativa

Para una persona trabajando de forma sostenida:

| Alcance | Estimación aproximada |
|---|---:|
| Prueba técnica de mapa plano | 3 a 5 semanas |
| Vertical slice Villa Raíz + Ruta 101 | 6 a 10 semanas |
| Exteriores principales de Hoenn | 4 a 8 meses |
| Juego completo razonablemente pulido | 9 a 18 meses |

La parte más costosa no será OpenGL. Será clasificar y corregir los casos visuales especiales de mapas, edificios, interiores y efectos.

---

## 30. Definición de terminado para el vertical slice

El vertical slice se considera terminado cuando:

- El proyecto compila con `DIORAMA=0` y `DIORAMA=1`.
- El modo clásico conserva el comportamiento original.
- Villa Raíz y Ruta 101 se muestran en 3D usando assets originales.
- El jugador y todos los NPC visibles se animan correctamente.
- Movimiento, colisiones, scripts y encuentros no han cambiado.
- Los warps funcionan.
- Menús, diálogos y combates usan fallback 2D sin glitches graves.
- Los cambios de mapa no producen carreras ni frames corruptos.
- Las reglas se editan en JSON y se compilan a C.
- Hay tests básicos de snapshot, reglas y geometría.
- El overlay debug explica por qué cada celda tiene su forma.
- Se mantienen 60 FPS en el hardware objetivo.

---

## 31. Primer sprint concreto

### Día 1

- Fork y commit fijado.
- Builds clásicos Windows/Linux.
- Guardados de prueba.

### Día 2

- `DIORAMA` flag.
- Esqueleto `include/diorama` y `src/diorama`.
- Build sin comportamiento nuevo.

### Días 3 a 5

- Contexto OpenGL.
- Shader de fullscreen quad.
- Textura del framebuffer GBA.
- Paridad visual 2D.

### Días 6 a 8

- Triple buffer.
- Snapshot de cámara y celdas.
- Rejilla de depuración.

### Días 9 a 12

- Decoder de tiles 4bpp.
- Composición de metatiles.
- Atlas.

### Días 13 a 15

- Plano texturizado de Villa Raíz.
- Cámara inicial.
- Toggle 2D/3D.

Al final de este sprint debe existir una prueba técnica útil, aunque todavía no haya árboles volumétricos ni NPC en 3D.

---

## 32. Recomendación final de implementación

Empezar por una sustitución limpia de la presentación, no por modificar el renderer GBA ni por modelar edificios.

El orden correcto es:

```text
paridad 2D en OpenGL
-> snapshot seguro
-> mapa plano real
-> elevación
-> objetos
-> reglas
-> vertical slice
-> casos dinámicos
-> UI integrada
-> pulido
```

El error más peligroso sería comenzar creando geometría vistosa antes de resolver:

- Sincronización de hilos.
- Cambios de mapa.
- Estados de fallback.
- Atlas y paletas.
- Invalidación de chunks.

Con esas bases resueltas, el resto se convierte en una expansión progresiva del diccionario visual de Hoenn, no en una reescritura del juego.

---

## 33. Fuentes revisadas

### Repositorio base

- [pokeemerald-multiplatform](https://github.com/gradenGnostic/pokeemerald-multiplatform)
- [`Makefile_pc`](https://github.com/gradenGnostic/pokeemerald-multiplatform/blob/master/Makefile_pc)
- [`src/platform/sdl2.c`](https://github.com/gradenGnostic/pokeemerald-multiplatform/blob/master/src/platform/sdl2.c)
- [`src/platform/gba_easy_draw.c`](https://github.com/gradenGnostic/pokeemerald-multiplatform/blob/master/src/platform/gba_easy_draw.c)
- [`src/fieldmap.c`](https://github.com/gradenGnostic/pokeemerald-multiplatform/blob/master/src/fieldmap.c)
- [`include/fieldmap.h`](https://github.com/gradenGnostic/pokeemerald-multiplatform/blob/master/include/fieldmap.h)
- [`include/global.fieldmap.h`](https://github.com/gradenGnostic/pokeemerald-multiplatform/blob/master/include/global.fieldmap.h)
- [`src/overworld.c`](https://github.com/gradenGnostic/pokeemerald-multiplatform/blob/master/src/overworld.c)
- [`include/event_object_movement.h`](https://github.com/gradenGnostic/pokeemerald-multiplatform/blob/master/include/event_object_movement.h)
- [`include/sprite.h`](https://github.com/gradenGnostic/pokeemerald-multiplatform/blob/master/include/sprite.h)
- [`include/palette.h`](https://github.com/gradenGnostic/pokeemerald-multiplatform/blob/master/include/palette.h)
- [`include/platform.h`](https://github.com/gradenGnostic/pokeemerald-multiplatform/blob/master/include/platform.h)
- [`src/option_menu.c`](https://github.com/gradenGnostic/pokeemerald-multiplatform/blob/master/src/option_menu.c)

### Referencia conceptual

- [DramaticShapeVoxelMod](https://github.com/DramaticShape/DramaticShapeVoxelMod)
- [`data/voxel_heights.lua`](https://github.com/DramaticShape/DramaticShapeVoxelMod/blob/master/data/voxel_heights.lua)
- [`lib/TileShape.lua`](https://github.com/DramaticShape/DramaticShapeVoxelMod/blob/master/lib/TileShape.lua)
- [`lib/Buildings.lua`](https://github.com/DramaticShape/DramaticShapeVoxelMod/blob/master/lib/Buildings.lua)
