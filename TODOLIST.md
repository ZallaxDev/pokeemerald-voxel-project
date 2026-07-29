# TODO: Pokemon Emerald Diorama 3D

Este documento convierte `pokeemerald_diorama_plan_implementacion.md` en una
secuencia ejecutable. El juego original sigue siendo la autoridad para gameplay y
el renderer 2D debe permanecer disponible durante todo el proyecto.

## Reglas de avance

- Las fases se ejecutan en orden estricto.
- No se inicia una fase mientras la anterior tenga tareas o pruebas obligatorias pendientes.
- `[ ]` significa pendiente, `[x]` completado y `[!]` bloqueado.
- Cada fase termina con una prueba manual reproducible por el usuario.
- Los resultados manuales se anotan en la propia fase y en el historial.
- No se versionan ejecutables, `build/`, configuraciones, partidas ni capturas con assets del juego.
- Los comandos se ejecutan desde la raiz del repositorio.

## Estado base

- Repositorio: `https://github.com/ZallaxDev/pokeemerald-voxel-project.git`
- Upstream accesible: `https://github.com/KamiKitsune420/pokeemerald-multiplatform.git`
- Commit base: `152622978` (`Speak the remaining menus, add HQ cries and reader hotkeys`)
- Rama de trabajo: `feature/diorama`
- Plataforma inicial: Ubuntu x86_64, ejecutable Linux i386, OpenGL 3.3 Core
- Android/GLES queda fuera del camino critico hasta la Fase 12.

## Fase 0: baseline reproducible

**Gate:** el ejecutable clasico debe compilar, arrancar y conservar gameplay,
audio, accesibilidad y guardado antes de modificar la presentacion.

- [x] Registrar repositorio, upstream, commit base y rama de trabajo.
- [x] Auditar las instrucciones especiales de `AGENTS.md` y `CLAUDE.md`.
- [x] Confirmar que el build conserva `-m32`.
- [x] Inventariar la toolchain disponible.
- [x] Instalar dependencias Ubuntu i386 disponibles en Resolute.
- [x] Resolver SDL2_mixer 2.8.1 i386 como dependencia estatica reproducible.
- [x] Compilar desde limpio con `make -f Makefile_pc linux -j4`.
- [x] Confirmar con `file pokeemerald` que el ejecutable es ELF de 32 bits.
- [x] Añadir CI para el ejecutable SDL clasico (`DIORAMA=0`).
- [x] Crear una partida de prueba, guardar, cerrar y cargarla correctamente.
- [x] Aceptar la partida actual como referencia local; las copias por escenario se aplazan por decision del usuario.
- [x] Aceptar el baseline visual validado manualmente; las capturas por escenario se aplazan por decision del usuario.
- [x] Completar el smoke test manual y registrar el resultado.

Instalacion pendiente:

```bash
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install -y build-essential gcc-multilib libc6-dev-i386 pkg-config \
  libsdl2-dev:i386 libsdl2-image-dev:i386 libgl-dev:i386 mesa-utils xvfb
```

Ubuntu Resolute no publica `libsdl2-mixer-dev:i386`. El build descarga
SDL2_mixer 2.8.1, verifica su SHA-256 y genera una biblioteca estatica i386 en
`build/deps/`. Se habilitan sus decodificadores internos OGG y MP3, por lo que
los pasos, indicaciones espaciales y gritos HQ conservan su funcionalidad.

Prueba manual realizable por el usuario:

```bash
make -f Makefile_pc linux -j4
file pokeemerald
./pokeemerald
```

Checklist dentro del juego: iniciar partida, caminar en Villa Raiz y Ruta 101,
abrir y cerrar el menu, entrar y salir de una casa, iniciar un combate, comprobar
musica/efectos, guardar, cerrar, volver a abrir y cargar. Probar tambien resize,
fullscreen, speedup y al menos una tecla del lector de pantalla si esta disponible.

Resultado del usuario: **APROBADO CON CORRECCION** (2026-07-29)

El primer intento mostro persistencia visual. La comparacion controlada aislo
errores de composicion en Easy Draw y persistencia adicional en los drivers SDL
acelerados de Linux. Tras seleccionar Fast Draw y SDL software exclusivamente
en `NATIVE_LINUX`, el usuario valido intro, textos, movimiento, transiciones y
fullscreen sin rastros. Windows y Android no se modificaron.
El usuario comprobo tambien crear una partida, guardarla, cerrar el juego y
cargarla sin problemas, y solicito continuar con la Fase 1.

## Fase 1: compositor OpenGL con paridad 2D

**Gate:** `DIORAMA=1` presenta el framebuffer original sin diferencias funcionales.

- [x] Añadir `DIORAMA ?= 0` y `ENABLE_DIORAMA` sin cambiar `DIORAMA=0`.
- [x] Crear el esqueleto minimo de `include/diorama/` y `src/diorama/`.
- [x] Enlazar OpenGL solo en builds diorama de escritorio.
- [x] Crear ventana SDL OpenGL 3.3 Core y contexto en el hilo de presentacion.
- [x] Cargar las funciones GL sin depender de extensiones expuestas por cabeceras del sistema.
- [x] Crear compositor, shader de quad y textura ARGB8888 del framebuffer GBA.
- [x] Reproducir nearest-neighbor, viewport 3:2, integer scaling y VSync.
- [x] Reproducir fondo y borde, con negro seguro cuando falten recursos.
- [x] Liberar contexto, shaders, buffers y texturas al cerrar.
- [x] Compilar `DIORAMA=0` y `DIORAMA=1`.
- [x] Añadir ambos builds y su smoke test al CI de escritorio.

Prueba manual realizable por el usuario:

```bash
make -f Makefile_pc linux -j4
./pokeemerald
make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1 -j4
./pokeemerald
```

Comparar a escala 1x título, Villa Raiz, menu y combate. Probar resize,
fullscreen, VSync, integer scaling, input, audio, speedup y cierre repetido.

Resultado del usuario: **APROBADO** (2026-07-29)

El usuario comparo las rutas clasica y OpenGL y valido titulo, partida, menu,
resize, fullscreen, VSync, speedup, audio y cierre sin diferencias visuales ni
funcionales.

## Fase 2: estado de escena y snapshots

**Gate:** el hilo grafico solo consume snapshots completos e inmutables.

- [x] Definir `DioramaSceneKind` y motivos explicitos de fallback.
- [x] Definir snapshots de celdas, objetos, camara, mapa y paleta sin punteros mutables.
- [x] Implementar intercambio triple-buffer con publicacion atomica.
- [x] Publicar tras las actualizaciones de `OverworldBasic()` sin sustituir `AX_OverworldScan()`.
- [x] Copiar un area 33x33 mediante las funciones existentes de `MapGrid`.
- [x] Copiar objetos y datos visuales estables de sprites.
- [x] Incrementar secuencias y generaciones de forma monotona.
- [x] Implementar fallback 2D si el snapshot no es valido.
- [x] Dibujar rejilla abstracta y diagnosticos desde el snapshot.
- [x] Añadir tests deterministas y concurrentes de publicacion y contenido.

Prueba manual realizable por el usuario: ejecutar en modo debug, recorrer Villa
Raiz, Ruta 101, una casa y varios warps; comprobar que secuencia, mapa, camara,
celdas y objetos cambian sin congelaciones ni datos del mapa anterior. Repetir
con speedup, resize y alt-tab.

Resultado del usuario: **APROBADO** (2026-07-29)

El usuario valido secuencia, generaciones, mapa, camara, objetos, warps,
fallbacks y estabilidad con speedup, resize y alt-tab. La rejilla permanece
activa correctamente en interiores cuando el overworld permite movimiento.

## Fase 3: atlas de metatiles y mapa plano

- [x] Decodificar tiles 4bpp y aplicar paletas y flips.
- [x] Componer capas de metatiles 16x16 en CPU.
- [x] Crear atlas GL con margenes que eviten bleeding.
- [x] Invalidar el atlas al cambiar tileset, animacion o generacion relevante.
- [x] Dibujar una celda texturizada por cada celda del snapshot.
- [x] Implementar camara perspectiva fija y transparencia basica.
- [x] Añadir tests de decoder, flips, composicion y UV.

Prueba manual realizable por el usuario: recorrer Villa Raiz y cambiar a Ruta
101; comparar IDs/texturas con el modo 2D y confirmar ausencia de bleeding,
texturas obsoletas y caidas bajo 60 FPS.

Resultado del usuario: **APROBADO** (2026-07-29)

El usuario valido Villa Raiz, Ruta 101 e interiores: graficos y colores
correctos, orientacion y movimiento continuos, animaciones, warps sin datos
obsoletos, ausencia de bleeding y rendimiento estable a 60 FPS.

## Fase 4: elevacion y mallas por chunks

- [x] Normalizar elevaciones sin interpretar literalmente valores especiales.
- [x] Generar caras superiores y laterales a partir de vecinos.
- [x] Dividir en chunks 8x8 y cerrar uniones sin grietas.
- [x] Implementar culling y reconstruccion selectiva de chunks sucios.
- [x] Añadir wireframe, limites de chunk y metricas.
- [x] Implementar ledges y acantilados basicos sin alterar colision.
- [x] Añadir tests de coordenadas, caras, bounds y hashes de geometria.

Prueba manual realizable por el usuario: recorrer desniveles y ledges, inspeccionar
uniones en wireframe y modificar un metatile mediante un evento; verificar que
solo se reconstruyen los chunks necesarios y que la colision original no cambia.

Resultado del usuario: **APROBADO** (2026-07-29)

Implementacion completada el 2026-07-29:

- Las elevaciones raw se conservan como planos de gameplay y no elevan terreno
  ordinario. Solo behaviors visuales explicitos generan altura.
- Los ledges se elevan solo en sus propias celdas, con el perfil bajo usado por
  la referencia de Pokemon Rojo; no propagan terrazas al terreno vecino.
- El agua surfable se dibuja 2/16 de unidad bajo el suelo para recuperar el
  labio de costa; los puentes conservan alturas explicitas por tipo.
- El terreno se divide en chunks mundiales 8x8 con halo, chunks parciales en el
  borde del snapshot y ownership determinista de caras laterales.
- Cada chunk conserva su VBO y solo se reconstruye cuando cambia la firma de su
  contenido o halo; paletas, animaciones y camara no invalidan geometria.
- El renderer aplica depth test, alpha cutout, frustum culling y fallback 2D si
  el subsistema de terreno no esta disponible.
- `F3` alterna wireframe, limites amarillos de chunk y metricas `CH`, `VIS`,
  `CUL`, `REB`, `TRI` y `DRA`.
- El snapshot conserva elevacion actual y previa de objetos para anclar el
  jugador al plano efectivo en la siguiente fase.
- `make -f Makefile_pc test-diorama` cubre coordenadas negativas, normalizacion,
  campos de ledges, planos de gameplay, caras, chunks parciales, costuras,
  bounds, firmas, hashes y frustum.

Primera validacion visual rechazada el 2026-07-29: Ruta 101 seguia plana y dos
celdas de mobiliario junto a la cama aparecian elevadas. Las capturas confirmaron
que elevation `4` del dormitorio era un plano de colision/prioridad, mientras que
los ledges de Ruta 101 usan behavior direccional con elevation `0`. Se sustituyo
la tabla de alturas raw por clasificacion visual basada en behaviors.

Segunda validacion visual rechazada el 2026-07-29: propagar la altura de los
ledges elevaba regiones completas y partia arboles vecinos. La comparacion con
la referencia de Pokemon Rojo confirmo que cada ledge debe ser un volumen bajo
aislado y que el agua debe conservar una pequena depresion. Se eliminaron las
terrazas propagadas y se restauro el agua a -2/16 de unidad.

La tercera validacion manual fue aprobada: los ledges elevan solo sus propios
tiles, el terreno y los arboles vecinos permanecen enteros, el agua recupera su
depresion y los interiores no convierten planos de colision en altura visual.

Validacion solicitada: recorrer desniveles y ledges con `F3` activado, cruzar
limites amarillos caminando y confirmar que no hay grietas ni bloques que
aparezcan de golpe. En las metricas, `REB` debe caer a `0` estando quieto y al
caminar reconstruir solo los chunks cuyo contenido o halo entra en el snapshot.
Probar tambien un evento que cambie un metatile, si esta disponible en la zona,
y confirmar que movimiento y colision siguen identicos al modo clasico.

## Fase 5: jugador y NPC como billboards

- [ ] Copiar frame, paleta, shape, size, flips y offsets desde OAM al snapshot.
- [ ] Crear cache de texturas por clave visual y generaciones.
- [ ] Dibujar jugador, NPC y shadow blobs anclados por los pies.
- [ ] Resolver profundidad y elementos que cubren al jugador.
- [ ] Interpolar movimiento y desactivarlo en warps o teletransportes.
- [ ] Añadir pruebas de interpolacion y deteccion de saltos.

Prueba manual realizable por el usuario: caminar y correr en las cuatro direcciones,
hablar con NPC, cruzar un warp y usar speedup; validar frames, flips, anclaje,
orden de profundidad y ausencia de vibracion.

Resultado del usuario: **PENDIENTE**

## Fase 6: reglas y vertical slice Villa Raiz + Ruta 101

- [ ] Definir JSON versionado para defaults, behaviors, tilesets, mapas y edificios.
- [ ] Crear compilador Python de JSON a tablas C, sin parser JSON en runtime.
- [ ] Validar mapas, simbolos, rangos, alturas, perfiles y duplicados.
- [ ] Implementar prioridad completa de resolucion y fallback plano.
- [ ] Clasificar suelo, arboles, hierba, carteles, casas, agua y bordes.
- [ ] Implementar bloques y perfiles basicos de tejado.
- [ ] Crear reglas de Villa Raiz y Ruta 101.
- [ ] Añadir tests de reglas y capturas golden locales.
- [ ] Implementar fallback 2D en dialogos, menus y combates.

Prueba manual realizable por el usuario: jugar el recorrido completo Villa Raiz
-> Ruta 101, entrar en casas, hablar, abrir menus, combatir, guardar y cargar;
confirmar volumen coherente, ausencia de huecos y fallback 2D correcto.

Resultado del usuario: **BLOQUEADO POR FASE 5**

## Fase 7: cambios dinamicos, conexiones y transiciones

- [ ] Notificar cambios desde las funciones `MapGridSet*` sin decidir gameplay.
- [ ] Invalidar celda, chunk y vecinos afectados.
- [ ] Precargar conexiones exteriores compatibles.
- [ ] Robustecer generaciones de mapa, warps y descarte de frames antiguos.
- [ ] Añadir fades entre 3D y fallback 2D.
- [ ] Probar puertas, Cut, Rock Smash, puentes y puzles.

Prueba manual realizable por el usuario: ejecutar cada caso dinamico disponible,
cruzar conexiones y encadenar warps con speedup; comprobar que no aparece vacio,
un frame antiguo ni una recarga completa innecesaria.

Resultado del usuario: **BLOQUEADO POR FASE 6**

## Fase 8: agua, animaciones y clima basico

- [ ] Actualizar regiones parciales del atlas para tiles animados.
- [ ] Implementar agua animada sin regenerar toda la escena.
- [ ] Consumir paletas faded publicadas en snapshots.
- [ ] Añadir lluvia y niebla basicas con fallback para climas no soportados.
- [ ] Añadir reflejos simples, Surf y orden correcto sobre agua.

Prueba manual realizable por el usuario: visitar agua y mapas con lluvia/niebla,
usar Surf y provocar fades; validar animacion, colores, reflejos, profundidad y FPS.

Resultado del usuario: **BLOQUEADO POR FASE 7**

## Fase 9: interfaz 2D sobre mundo 3D

- [ ] Separar mundo, UI y alpha en el renderer software mediante perfiles de escena.
- [ ] Clasificar OAM de campo frente a sprites de interfaz.
- [ ] Superponer dialogos, popup de mapa y menu de inicio.
- [ ] Mantener fallback completo para escenas complejas y errores.
- [ ] Preservar resolucion, timings y accesibilidad del texto.

Prueba manual realizable por el usuario: abrir dialogos, menu de inicio y popup de
mapa sobre 3D; comprobar ausencia de fondo 2D residual y comparar texto, fades,
voz y timings con el modo clasico.

Resultado del usuario: **BLOQUEADO POR FASE 8**

## Fase 10: interiores y edificios avanzados

- [ ] Definir perfiles de camara interior y ocultacion de paredes frontales.
- [ ] Representar muebles y objetos interactivos de forma legible.
- [ ] Crear plantillas de casas, tiendas, Centros Pokemon y gimnasios.
- [ ] Añadir reglas por tileset interior y cubrir interiores de historia.
- [ ] Evaluar una herramienta visual de alturas solo tras estabilizar las reglas.

Prueba manual realizable por el usuario: recorrer los interiores obligatorios de
la historia y accionar todos los objetos visibles; verificar que paredes y muebles
no ocultan al jugador, NPC ni puntos interactivos.

Resultado del usuario: **BLOQUEADO POR FASE 9**

## Fase 11: sombras y posprocesado

- [ ] Implementar shadow map direccional y PCF ligero.
- [ ] Implementar oclusión aproximada o baked por vertice.
- [ ] Añadir depth of field, tilt-shift y niveles de calidad configurables.
- [ ] Permitir desactivar cada efecto y exponer metricas CPU/GPU.

Prueba manual realizable por el usuario: comparar calidad desactivada, baja y alta
a 1280x720; confirmar legibilidad del pixel art y 60 FPS en el perfil bajo.

Resultado del usuario: **BLOQUEADO POR FASE 10**

## Fase 12: cobertura completa y Android

- [ ] Auditar todos los mapas y completar reglas y casos especiales.
- [ ] Cubrir cuevas, buceo, Battle Frontier y escenas unicas.
- [ ] Implementar backend GLES 3.0 sin degradar el backend de escritorio.
- [ ] Crear perfiles de rendimiento movil y documentacion para contribuidores.
- [ ] Ejecutar regresion completa de gameplay y thread safety.

Prueba manual realizable por el usuario: completar una matriz de mapas y escenas en
Linux, Windows y Android; validar guardados compatibles, fallback, 60 FPS de
escritorio y el presupuesto movil acordado.

Resultado del usuario: **BLOQUEADO POR FASE 11**

## Historial

### 2026-07-29

- Se reviso el plan de implementacion completo y se convirtio en fases con gates.
- Se leyeron `AGENTS.md`, `CLAUDE.md`, `README.md`, `Makefile_pc` y el CI existente.
- Se fijo como base el commit `152622978` y se creo `feature/diorama`.
- Se confirmo que `Makefile_pc` usa `-m32` y separa `build/linux`.
- Se detecto que el CI existente solo construye/compara el ROM upstream.
- Se comprobo Ubuntu 64 bits con arquitectura secundaria i386 habilitada, GCC 15.2.0 y Make 4.4.1.
- Se detecto que faltan los paquetes de desarrollo SDL2, SDL2_image, SDL2_mixer y OpenGL para i386.
- Se intento instalar la toolchain; `sudo` solicito autenticacion interactiva y bloqueo el paso.
- El usuario instalo la toolchain i386 disponible en Ubuntu Resolute.
- Se confirmo que Resolute no publica `libsdl2-mixer-dev:i386` aunque si publica SDL2 y SDL2_image.
- Se valido SDL2_mixer 2.8.1 estatico i386 con OGG (`stb_vorbis`) y MP3 (`minimp3`) integrados.
- Se automatizo su descarga con SHA-256 fijado y su compilacion bajo `build/deps/`.
- Se configuro `pkg-config` para consultar explicitamente las bibliotecas i386.
- El primer build detecto la dependencia nativa `libpng-dev`; el usuario la instalo.
- El segundo build genero los assets y detecto que `speech.c` no aislaba Win32 en Linux.
- Se mantuvo el backend NVDA Win32 y se añadieron no-ops para plataformas no Win32, como exige el contrato de accesibilidad.
- Se corrigio el prefijo del bootstrap SDL2_mixer para que `configure` reciba una ruta absoluta.
- Se completo `make -f Makefile_pc NATIVE_LINUX=1 mostlyclean` seguido del build clasico sin errores.
- `file` confirmo un ELF i386 de 32 bits y `ldd` confirmo dependencias SDL i386 resolubles.
- El ejecutable permanecio activo durante el smoke test automatizado de 10 segundos bajo Xvfb.
- Se eliminaron dos consultas de Make no utilizadas que emitian falsos errores de MinGW al invocar el target Linux.
- El usuario detecto composicion residual de textos, sprites y transiciones en el baseline Linux.
- `SDL_RENDER_DRIVER=software` elimino la persistencia entre pantallas pero no los errores internos de capas de Easy Draw.
- Un build diagnostico con Fast Draw elimino los errores internos de capas.
- OpenGL ES 2 reprodujo la persistencia del backend acelerado; Fast Draw con SDL software fue visualmente estable.
- Se seleccionaron Fast Draw y SDL software solo para `NATIVE_LINUX`; Windows y Android conservan sus rutas actuales.
- Se reconstruyo desde limpio y el nuevo baseline Linux supero el arranque automatizado bajo Xvfb.
- El usuario aprobo el baseline corregido sin persistencia en intro, textos, movimiento, transiciones y fullscreen.
- El usuario valido crear, guardar, cerrar y cargar una partida, y autorizo cerrar la Fase 0; las copias y capturas por escenario quedaron aplazadas.
- Se añadio un job CI para compilar el ejecutable SDL clasico de 32 bits.
- Se implemento `DIORAMA=1` con un unico compositor OpenGL 3.3 Core, sin crear `SDL_Renderer` en esa ruta.
- El compositor conserva la conversion ARGB8888, nearest-neighbor, viewport, escala entera, fondos, borde y fallback negro.
- Los cambios de VSync, fullscreen y escala se aplican en el hilo SDL mediante eventos, sin mover llamadas GL al hilo del juego.
- Los objetos clasicos y diorama se separaron en `build/linux` y `build/linux-diorama`, con binarios internos distintos para alternar flags de forma segura.
- Ambos builds produjeron ELF i386 y el compositor permanecio activo bajo Xvfb/Mesa con y sin artwork disponible.
- El CI de escritorio usa una matriz `DIORAMA=0/1` y ejecuta un smoke test de presentacion para ambos.
- El usuario aprobo la comparacion visual y funcional entre las rutas clasica y OpenGL; la Fase 1 queda cerrada.
- Se definio un snapshot de valores para mapa, camara, rejilla 33x33, objetos, OAM y paleta faded, sin punteros a estado mutable.
- Se implemento un triple buffer con slot publicado y slot lector fijado mediante atomicos SDL; el renderer copia antes de consumir.
- Cada ciclo publica overworld actualizado o un registro de fallback, evitando conservar una escena anterior durante menus, batallas, cargas o link handling.
- `AUTO` solo sustituye el framebuffer durante overworld libre; fades, scripts, dialogos, menus y escenas no soportadas siguen en 2D.
- La rejilla diagnostica muestra celdas reales, camara, objetos y texto con secuencia, mapa y generaciones desde la copia local del hilo GL.
- `test-diorama-snapshot` valida copias completas, secuencias monotonicas, pinning, fallback, limites y 20.000 publicaciones con lector/escritor concurrentes.
- El test de snapshots se incorporo al job `DIORAMA=1` del CI de escritorio.
- Ambos builds i386 compilan y el diorama supera el smoke test GLX; la Fase 2 queda pendiente de validacion manual en mapas y warps.
- El usuario aprobo la rejilla y los snapshots en Villa Raiz, Ruta 101, interiores y warps; la Fase 2 queda cerrada.
- Se amplio el snapshot con los graficos 4bpp animados del charbase real, las ocho entradas de cada metatile y la fase subpixel de camara, siempre por valor.
- Se implemento composicion CPU de las dos capas 8x8 con flips, paletas BGR555 y color cero transparente.
- Se creo un atlas GL 576x576 con gutters duplicados, UV a centros de texel, cache por ID e invalidacion por mapa, paleta y animacion.
- El mapa plano usa quads por celda, proyeccion de perspectiva fija, interpolacion perspective-correct, pan y movimiento subpixel continuo.
- `test-diorama-metatile` cubre decoder, flips, colores, capas, alpha, gutters, deduplicacion y UV; CI ejecuta ambos tests diorama.
- Builds clasico y diorama, tests, `diff --check`, ELF i386 y smoke GLX pasan; la Fase 3 queda pendiente de comparacion visual y rendimiento manual.
- El usuario aprobo fidelidad visual, animaciones, continuidad, warps y 60 FPS en Villa Raiz, Ruta 101 e interiores; la Fase 3 queda cerrada.
