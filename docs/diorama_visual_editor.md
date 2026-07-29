# Editor visual de mapas diorama

## Objetivo

Crear una herramienta local, ligera y rapida para corregir la representacion 3D
de los mapas sin modificar mapas, colisiones, eventos, scripts ni assets del
juego. El editor trabaja exclusivamente sobre metadatos visuales de
`data/diorama/` y reutiliza los tiles, metatiles y paletas originales.

La herramienta debe permitir que una persona sin editar JSON manualmente pueda:

- Ver el mapa original en planta y una previsualizacion 3D simultanea.
- Seleccionar una celda, un rectangulo o un conjunto conectado de celdas.
- Elevar, ocultar o convertir celdas en paredes, suelos, tejados o props.
- Elegir un metatile original como material para cada cara.
- Agrupar varias celdas como una estructura continua.
- Aplicar presets reutilizables, por ejemplo carteles, arboles o mostradores.
- Guardar cambios concretos del mapa sin duplicar reglas globales.
- Validar y compilar las reglas antes de aceptar el guardado.

## Principios obligatorios

1. El juego original sigue siendo la autoridad de gameplay.
2. El editor nunca escribe en `data/maps/`, `data/layouts/` ni
   `data/tilesets/`.
3. Los assets del juego son de solo lectura.
4. Solo se escriben fuentes JSON versionadas bajo `data/diorama/`.
5. Los archivos C generados nunca se editan manualmente.
6. La previsualizacion puede ser aproximada, pero el guardado debe usar el mismo
   vocabulario de reglas que consume el renderer.
7. Una regla reutilizable no se copia en todos los mapas.
8. Una correccion espacial concreta si pertenece al archivo de su mapa.
9. El editor no necesita modificar el juego en ejecucion. La previsualizacion
   debe actualizarse en memoria inmediatamente y el juego se recompila al final.

## Arquitectura recomendada

Usar una aplicacion web local sin Electron y sin una cadena de build frontend:

- Backend: Python 3 y biblioteca estandar.
- Frontend: HTML, CSS y JavaScript sin framework.
- Vista 2D: Canvas 2D.
- Vista 3D: Three.js fijado a una version y almacenado localmente.
- Persistencia: peticiones HTTP locales al backend Python.
- Sin base de datos, npm, servicios externos ni conexion a Internet.

Three.js es la unica dependencia propuesta porque simplifica seleccion, camara,
geometria, materiales y picking. Debe servirse desde el repositorio, no desde un
CDN. Si se evita cualquier dependencia, puede sustituirse por WebGL directo, pero
eso aumentaria mucho el coste del editor sin mejorar los datos generados.

Estructura prevista:

```text
tools/diorama_editor/
  server.py
  index.html
  editor.js
  editor.css
  vendor/three.module.min.js
```

El servidor solo debe escuchar en `127.0.0.1`, validar todas las rutas y limitar
la escritura a `data/diorama/`.

## Flujo de datos existente

Ya existe un exportador por mapa:

```bash
python3 tools/diorama_rules/export_editor_map.py \
  MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F \
  --output /tmp/diorama-map.json
```

El documento exportado contiene:

- Simbolo, layout, dimensiones y tipo de mapa.
- Una celda estable por coordenada `x,y`.
- Metatile global y metatile local de cada celda.
- Tileset primario o secundario.
- Colision, elevacion, comportamiento y tipo de capa originales.
- Eventos de objeto, warp, coordenada y fondo.
- Reglas diorama actuales.
- Rutas a tiles, metatiles, atributos y paletas.

Fuentes editables actuales:

```text
data/diorama/defaults.json       reglas por comportamiento
data/diorama/tilesets/*.json     reglas reutilizables por metatile
data/diorama/buildings/*.json    plantillas multicelda reutilizables
data/diorama/maps/*.json         colocaciones y excepciones de un mapa
```

La previsualizacion debe resolver reglas en el mismo orden que el juego:

```text
override de mapa o evento
regla de metatile del tileset
estructura colocada
comportamiento
colision/elevacion
heuristica visual
suelo plano
```

## Reutilizacion frente a reglas de mapa

### Reglas reutilizables

Se usan cuando el mismo elemento visual representa lo mismo en muchos mapas:

- Carteles.
- Arboles, arbustos y flores.
- Mostradores y estanterias comunes.
- Camas, mesas, ordenadores y macetas.
- Paredes asociadas a metatiles concretos de un tileset.
- Plantillas de casas, tiendas y Centros Pokemon.

Estas reglas deben vivir en tilesets, presets o plantillas. Un cartel no debe
editarse celda por celda ni copiarse en cada archivo de mapa.

### Reglas concretas de mapa

Se usan cuando la geometria depende de una composicion espacial unica:

- Limites y habitaciones de un interior concreto.
- Agrupacion de varias celdas en un edificio continuo.
- Altura excepcional de una plataforma.
- Tejado, pared o material diferente en una coordenada concreta.
- Ocultacion de una pared frontal de una habitacion determinada.

El archivo del mapa debe guardar coordenadas originales, nunca coordenadas con
`MAP_OFFSET`.

## Ampliacion necesaria del esquema

El compilador actual ya soporta `flat`, `extruded`, `cutout`, edificios, tejados
y materiales por cara. Para el editor se deben anadir dos conceptos sin romper
las reglas existentes.

### Presets de props

Crear fuentes como `data/diorama/props/*.json` con presets reutilizables. Ejemplo
orientativo, todavia no implementado:

```json
{
  "version": 1,
  "presets": {
    "wooden_sign": {
      "primitive": "panel",
      "width": 0.82,
      "height": 0.9,
      "depth": 0.12,
      "anchor": "ground-center",
      "axis": "x",
      "faces": {
        "front": { "metatile": "self", "layer": "foreground" },
        "back": { "metatile": "self", "layer": "foreground" },
        "sides": { "metatile": "self", "layer": "base" }
      }
    }
  }
}
```

`panel` representa una textura plana con grosor. Sirve para carteles, televisores,
cuadros, armarios finos y otros elementos que no deben ser un billboard sin
volumen. Debe permitir:

- Anchura, altura y profundidad.
- Orientacion `x`, `z` o libre en incrementos de 90 grados.
- Anclaje al suelo, centro o borde de celda.
- Material frontal, trasero, lateral y superior.
- Fuente `self` para conservar el metatile de la celda.
- Capa `full`, `base`, `foreground` o `none`.

Una regla de tileset o de evento debe referenciar el preset por nombre. Por
ejemplo, todos los eventos `sign` de un tileset pueden usar `wooden_sign`; el
mapa solo necesita una excepcion si un cartel concreto es diferente.

### Estructuras de mapa

Generalizar las colocaciones de edificios a estructuras editables. Una
estructura debe poder almacenar:

- Identificador local estable.
- Rectangulo o mascara de celdas.
- Altura de cuerpo y tejado.
- Perfil de tejado.
- Paredes visibles por lado.
- Material por cara.
- Regla de ocultacion de pared frontal.

Las plantillas reutilizables siguen en `buildings/` o en un futuro
`structures/`. El mapa solo guarda el nombre de la plantilla, posicion y
overrides locales.

## Selector de materiales

El usuario no debe escribir IDs de metatile. El selector mostrara dos catalogos:

- Tileset primario.
- Tileset secundario.

Cada entrada debe incluir miniatura, ID local, ID global, comportamiento y tipo
de capa. Al escoger un metatile secundario, el editor convierte automaticamente
el ID local al ID global sumando `0x200`.

El selector se usa de forma independiente para:

- Cara superior.
- Norte, este, sur y oeste.
- Frente y reverso de un panel.
- Laterales de un panel.
- Capa completa, base o foreground.

Debe existir una accion de cuentagotas para tomar el metatile de cualquier celda
del mapa y aplicarlo como material.

## Interfaz minima

### Barra superior

- Selector de mapa.
- Abrir, guardar, deshacer y rehacer.
- Validar.
- Regenerar reglas.
- Alternar vista original, reglas resueltas y colisiones.

### Panel izquierdo

- Mapa 2D en planta.
- Seleccion por clic, arrastre rectangular y relleno conectado.
- Capas opcionales para eventos, elevacion y colision.

### Vista central

- Previsualizacion 3D actualizada al instante.
- Orbita libre solo para editar.
- Boton para volver a la camara real del perfil del mapa.
- Seleccion sincronizada con la vista 2D.

### Inspector derecho

- Regla efectiva y procedencia.
- Tipo de geometria.
- Altura de suelo y altura de volumen.
- Orientacion y anclaje.
- Materiales por cara.
- Preset reutilizable.
- Crear estructura o convertir seleccion en override.
- Eliminar override para volver a la regla heredada.

### Pie

- Ruta del JSON que se modificara.
- Estado de validacion.
- Numero de overrides y estructuras.
- Advertencias de reglas duplicadas o que convendria promover a tileset/preset.

## Operaciones esenciales del MVP

1. Abrir un mapa y representar todas sus celdas en 2D.
2. Decodificar miniaturas de metatiles con flips, capas y paletas originales.
3. Mostrar una previsualizacion 3D aproximada de las reglas resueltas.
4. Seleccionar celdas y cambiar `shape`, `groundHeight` y `height`.
5. Elegir materiales por cara con miniaturas.
6. Crear una extruson rectangular continua.
7. Crear o colocar una plantilla de edificio.
8. Aplicar un preset reutilizable de prop.
9. Crear un panel con profundidad para un cartel.
10. Deshacer y rehacer toda operacion de edicion.
11. Guardar solo los cambios del archivo diorama correspondiente.
12. Ejecutar validacion y compilacion mostrando errores con coordenadas.

No forman parte del primer MVP:

- Edicion de mapas jugables.
- Modelado libre de vertices.
- Importacion de texturas externas.
- Edicion de sprites o animaciones.
- Sincronizacion en vivo con el proceso del juego.
- Electron, cuentas, nube o colaboracion remota.

## Guardado seguro

Antes de escribir:

1. Mantener una copia en memoria del documento original.
2. Generar JSON determinista con indentacion de dos espacios.
3. Escribir primero un temporal junto al destino.
4. Validar el temporal con las mismas reglas del compilador.
5. Sustituir el archivo de destino solo si la validacion pasa.
6. Ejecutar:

```bash
python3 tools/diorama_rules/validate_rules.py
python3 tools/diorama_rules/compile_rules.py
python3 tools/diorama_rules/compile_rules.py --check
```

El editor no debe guardar el documento exportado completo. Debe conservar solo
las claves admitidas por el esquema de `data/diorama/`.

## Evolucion propuesta

### Paso 1: visor de solo lectura

- Servidor local.
- Lista de mapas configurados.
- Canvas 2D con metatiles y eventos.
- Vista 3D con reglas actuales.
- Inspector de celda y procedencia de regla.

### Paso 2: edicion de overrides

- Seleccion y herramientas de altura/forma.
- Materiales por cara.
- Guardado, validacion, undo y redo.

### Paso 3: presets y props con profundidad

- Esquema `props/`.
- Primitiva `panel`.
- Carteles reutilizables por tileset o evento.
- Promocion de varios overrides iguales a preset reutilizable.

### Paso 4: estructuras e interiores

- Paredes continuas.
- Ocultacion frontal.
- Plantillas de habitaciones y edificios.
- Casas, tiendas, Centros Pokemon y gimnasios.

## Criterios de aceptacion

- Abrir un mapa configurado tarda menos de dos segundos en una maquina de
  desarrollo normal.
- Una edicion aparece en la vista 3D sin recompilar el juego.
- Es posible dar grosor a todos los carteles mediante una sola regla reusable.
- Es posible construir paredes de varias celdas y elegir su arte sin escribir IDs.
- Guardar un mapa no modifica reglas ni mapas ajenos.
- Un JSON invalido nunca reemplaza una fuente valida.
- El resultado compilado es determinista.
- El modo clasico y el fallback 2D no cambian.
- Ninguna accion del editor modifica gameplay o assets originales.

## Primera prueba recomendada

Usar `MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F` como mapa inicial porque ya tiene
perfil interior, dimensiones pequenas y tilesets registrados. Para probar
reutilizacion, usar despues los carteles de Villa Raiz: definir una sola vez el
preset con profundidad y comprobar que todas las posiciones generadas desde
eventos `sign` lo heredan sin crear overrides por coordenada.
