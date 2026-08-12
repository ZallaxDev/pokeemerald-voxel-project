# Pruebas manuales de paridad Diorama

Las pruebas manuales las ejecuta exclusivamente el usuario. Al terminar cada ciclo, el
agente debe indicar en espanol una lista breve con lugares, resultado esperado y
comprobaciones. No se piden capturas, hashes, comandos auxiliares ni manifests locales.

## Flujo permanente de validacion

La validacion visual precede al cierre automatico completo:

1. Mientras una fase esta en `implementation`, se corrige un unico objeto o clase.
2. El agente ejecuta solo sus tests focalizados y genera un build Diorama incremental.
3. El usuario comprueba un escenario visual reducido y comunica el resultado.
4. Si falla, la fase sigue en `implementation`; no se ejecutan suite global, build classic,
   hashes ni smoke completo.
5. Cuando el usuario confirma que el candidato reducido se ve correctamente, se ejecutan
   una vez la suite completa, paridad global, builds classic/Diorama y smoke final.
6. Solo despues se pasa a `manual-pending` y se solicita la aprobacion explicita del gate.

Los tests automaticos verifican contratos tecnicos, no apariencia. En particular, una
mask golden solo detecta cambios en una mascara y puede conservar una mascara equivocada;
no demuestra volumen, orientacion, profundidad, visibilidad ni fidelidad 3D.

Desde R3, F6 no forma parte del gate salvo que una captura sea necesaria para documentar
un defecto concreto. F5 solo se pide cuando comparar pitches fijos demuestra una propiedad
geometrica u oclusion que no puede comprobarse moviendo la camara directamente. Cada fase
debe eliminar pasos redundantes y conservar unicamente las inspecciones visuales y smokes
funcionales que puedan detectar un fallo de esa fase.

## R0

1. Ve a Villa Raiz, Ruta 101, Ruta 104, Ruta 115 y Monte Cenizo con Diorama activo.
2. Comprueba que absolutamente todo el terreno permanece plano y que no aparecen
   estructuras, columnas ni torres inventadas.
3. Mueve la camara y cambia el viewport; la geometria no debe cambiar.
4. Sal y vuelve a cada mapa; el resultado debe seguir siendo identico.
5. Arranca el renderer classic y comprueba movimiento, menus, combate, audio, guardado y
   carga.

Si algo falla, indica el mapa y lo observado. Si todo pasa, aprueba R0 explicitamente.

## R1

1. Pulsa F3 para mostrar `POS:x,y`, ve a Villa Raiz, ponte en (10,10) mirando al sur
   y pulsa F5 para recorrer `flat`,
   `v15`, `v35`, `v50` y `v75`; pulsa F6 en cada vista.
2. Comprueba que la vista plana coincide con el renderer classic y que
   mapa, posicion, direccion y contenido no cambian entre vistas.
3. Confirma mediante el manifest que las cinco vistas comparten el mismo snapshot.
4. F5 congela el frame y fija la ventana a 960x640; pulsa Ctrl+P al terminar para reanudar
   el juego y restaurar la ventana y el renderer anteriores.

Si algo falla, indica el mapa, la vista y lo observado. Si todo pasa, aprueba R1
explicitamente.

## R2

R2 dispone del clasificador compilado y del overlay. Este procedimiento no sustituye la
ejecucion manual ni implica aprobacion visual.

1. Usa una partida normal y desplazate sin teletransporte. El juego original conserva la
   autoridad sobre movimiento, collision, scripts, eventos, warps, encuentros y guardado.
2. Ejecuta `R2-ROUTE101-CLASSIFIER` en Ruta 101, (10,10), mirando al norte, a 960x640.
   Recorre `flat`, `v15`, `v35`, `v50` y `v75` con postprocesado desactivado y overlay
   `class/source/evidence`.
3. Ejecuta `R2-ROUTE115-CLASSIFIER` en Ruta 115, (18,41), mirando al norte, con las mismas
   cinco vistas. Collision por si sola nunca debe producir `wall`.
4. Ejecuta `R2-GRANITE-CAVE-B1F-CLASSIFIER`: entra por el warp (25,13), da un paso a
   (25,14), mira al norte y repite las cinco vistas. La roca ambigua queda plana sin pin
   authoritative.
5. Para `R2-ROUTE115-AUTHORITATIVE-PIN-BLAST-RADIUS`, revisa la flor
   `gTileset_General:4` desde `(21,63)`. Revisa sus 615 apariciones en el catalogo, no
   solo el ancla, y confirma los campos
   `class/height/artMode/pool/authored/source/evidence`.
6. Ejecuta `R2-CLASSIC-DIORAMA-REGRESSION-SMOKE` sin NVDA en ambos binarios. Prueba
   movimiento, input, audio, save/load, resize, fullscreen, menus, combate y fallback 2D.
7. En cada escenario cruza el limite o warp indicado mediante gameplay normal, vuelve al
   punto exacto y confirma que decisiones, evidencia y hash semantico no cambian.
8. Comprueba que ground, grass, water, ledges y stairs solo se clasifican con evidencia
   fiable; arboles, edificios, cliffs y roca ambigua permanecen planos sin pin revisado.

El spike IA opcional consta como `not-run`, no es autoridad y no bloquea R2. Si algo
falla, indica scenario ID, vista, clase, source y evidencia observada. R2 solo puede
aprobarse tras implementar y pasar las pruebas automaticas y despues de la aprobacion
manual explicita del usuario.

## R3

R3 publica claims y regiones como diagnostico visual; edificios, mostradores y props
siguen planos hasta sus fases geometricas. F3 dibuja boundaries cyan y muestra
`OWN`, `CLM`, `RGN` y el source estructural junto al source del clasificador.

1. Ejecuta `R3-LITTLEROOT-CLAIMS` en Villa Raiz, (5,9), mirando al norte. La casa
   completa debe tener un unico template estable; la puerta sigue visible, transitable y
   no adquiere altura. Revisa tambien las ocho apariciones exactas declaradas por el
   catalogo: tres edificios de Villa Raiz, dos signposts y tres mostradores de Centro
   Pokemon; no basta con el ancla.
2. Ejecuta `R3-ROUTE115-REGIONS` en Ruta 115, (18,41), mirando al norte. Agua,
   vegetacion y suelo no deben compartir region cuando difieren class o pool.
3. Ejecuta `R3-FORTREE-CLAIMS` en Arborada, (5,7), mirando al norte. Entra y sal del
   Centro Pokemon y confirma que puertas y pasarelas no rompen fachadas ni cambian los
   componentes.
4. Ejecuta `R3-POKEMON-CENTER-CLAIMS` en el Centro Pokemon de Pueblo Escaso, (7,7),
   mirando al norte. El mostrador debe ser un template furniture separado del suelo y de
   la enfermera, que sigue siendo un sprite billboard.
5. En los cuatro escenarios recorre `flat`, `v15`, `v35`, `v50` y `v75`, mueve la
   camara y repite tras el round trip normal. Boundaries, `CLM`, `RGN`, `OWN` y source no
   deben depender del pitch, viewport o orden de visita.
6. Ejecuta `R3-CLASSIC-DIORAMA-REGRESSION-SMOKE` sin NVDA. Comprueba movimiento,
   input, audio, save/load, resize, fullscreen, menus, combate y fallback 2D.

La comprobacion principal es visual. Si algo falla, indica scenario ID, coordenada,
pitch y los valores `OWN/CLM/RGN/SRC`. R3 solo se aprueba cuando el usuario lo declara
explicitamente despues de estas pruebas.

## R4

F3 muestra `PX:<id> G:<metatile>` cuando la celda objetivo pertenece a un objeto pixel
compilado. `PX` debe desaparecer junto con el claim si una mutacion invalida el candidato.

1. Ejecuta `R4-LITTLEROOT-SIGN` y `R4-OLDALE-SIGN-SHARED-TILESET`. Compara `flat` con
   los pitches mediante F5 porque aqui el grosor, los pies y la silueta son evidencia
   geometrica concreta. Ambos carteles deben compartir mask y suelo `G:1`.
2. Ejecuta `R4-OLDALE-HOUSE1-PLANTS` y `R4-MAUVILLE-HOUSE1-STOOLS`. Mueve la camara
   alrededor de los objetos; usa F5 solo si necesitas fijar una oclusion. Cada componente
   conserva sus propios pies y no se fusiona con el objeto vecino.
3. Ejecuta `R4-BRENDAN-HOUSE-TV-SUPPORT`. Revisa con F5 los pixels blancos/grises
   encerrados, la base a altura de mesa y la ausencia de suelo visible a traves del
   soporte. Sube y baja de planta para repetir la publicacion del IR.
4. Ejecuta `R4-DEWFORD-HALL-RELIEF`. La mesa edge-to-edge debe ser un relief horizontal
   poco profundo, nunca un billboard o caja vertical. Comprueba despues las casas que
   comparten `gTileset_GenericBuilding`.
5. Ejecuta `R4-ROUTE104-CUT-TREE-BILLBOARD` y
   `R4-FIERY-PATH-BOULDER-BILLBOARD`. Usa Cut y Strength mediante gameplay normal. El
   arbol desaparece y la roca se mueve como object billboards sin geometria estatica
   residual.
6. Ejecuta `R4-CLASSIC-DIORAMA-REGRESSION-SMOKE` sin NVDA. Prueba movimiento, audio,
   save/load, resize, fullscreen, menus, combate y fallback 2D.

F6 solo se usa para documentar un defecto concreto. Si algo falla, indica scenario ID,
coordenada, pitch y `PX/G`; R4 solo se aprueba tras declarar explicitamente que masks,
soporte y ground replacement son correctos.

## R5

F3 muestra `VOL H:<bandas> P:<periodo> <eje> F:<flags>` para una celda aceptada por el
detector generico. Cada banda representa 8 pixels del dibujo, es decir, media celda de
altura; `H` nunca supera seis. La vista `flat` es la autoridad para decidir cuanto arte
pertenece a la cara, pero la correccion 3D solo puede validarse moviendo la camara.

1. Ejecuta `R5-ROUTE115-CLIFF-RUNS` en Ruta 115, (18,41), mirando al norte. Comprueba
   primero a pitch 35 y 50 que cada metatile `MB_MOUNTAIN_TOP` alcanza exactamente una
   celda de altura, independientemente de collision y elevation. Su contorno elevado debe
   seguir la silueta pixelada del foreground o de la cima opaca; nunca debe verse como un
   cuadrado completo elevado ni dejar negro en los pixels recortados.
2. Ejecuta `R5-MT-CHIMNEY-REPEATED-RIDGES` en Monte Cenizo, (20,23), mirando al sur.
   Las filas repetidas deben conservar la misma altura de una celda y unirse sin grietas,
   escalones accidentales ni slabs cuadrados entre variantes de borde y cima.
3. Ejecuta `R5-JAGGED-PASS-BIDIRECTIONAL-RUNS` en Desfiladero, (16,25), mirando al norte.
   Revisa con F3 ejemplos `X` y `Z`; la mascara debe conservar su curva en ambas direcciones
   y no cambiar al mover la camara o durante fades.
4. Ejecuta `R5-FORTREE-FOREST-REPETITION` en Arborada, (5,7), mirando al norte. El bosque
   debe conservar unidades repetidas acotadas y de altura uniforme dentro de una plantacion,
   no una pared monolitica o columnas altas. Cada copa debe conservar literalmente su silueta
   frontal pixelada y adquirir profundidad redondeada al girar la camara, sin parecer un
   sprite pegado a un cilindro. La hierba y sombra dibujada del fondo no forman geometria;
   frente/reverso conservan el dibujo y laterales/tapa usan colores interiores de la copa.
   Debajo solo aparece el ground plano circundante: nunca debe quedar una copia 2D del arbol.
   El movimiento de camara por una plantacion no debe producir caidas fuertes de rendimiento.
   El arbol grande debe coincidir con `tree_model/emerald_tree.vox`, estar centrado sobre el
   cuerpo bloqueante 2x2 y tocar el suelo. La fila norte atravesable del dibujo 2x3 debe quedar
   como cesped normal; las dos filas bajo el modelo pueden verse algo mas oscuras como sombra.
   No debe quedar ningun hull cortado a media altura. El patron repetido fuera del borde este
   de Oldale y de los bordes sin conexion de Littleroot debe usar el mismo modelo; una conexion
   real nunca debe convertirse en decoracion de borde. Recorrer y girar la camara por Oldale
   debe mantener movimiento fluido y los arboles no deben aparecer/desaparecer en seams de chunk.
   Las pasarelas y edificios reclamados no deben recibir `VOL`.
   En Villa Raiz/Pueblo Escaso comprueba ademas que un arbol grande forma un solo hull 2x2
   de dos celdas de altura, incluida su fila norte atravesable, y que un arbol pequeno forma
   un solo hull 2x1. No debe verse negro en las esquinas recortadas ni entre hulls vecinos.
5. Ejecuta `R5-GRANITE-CAVE-B1F-WALL-EXTENT`: entra por (25,13), avanza a (25,14) y mira
   al norte. Las paredes coherentes usan bandas distintas sin estirar una unica textura;
   no debe haber roofs en el interior de la cueva y la roca ambigua queda plana.
6. Ejecuta `R5-CLAIMED-CONTENT-EXCLUSION` en el Centro Pokemon de Pueblo Escaso, (7,7),
   mirando al norte. El mostrador conserva `OWN:template` y nunca muestra `VOL`.
7. En los cinco escenarios de terreno compara pitches 15, 35, 50 y 75, y vuelve al mapa
   mediante una transicion normal. Altura, periodo, eje, flags y geometria deben mantenerse.

Para iterar basta empezar por Ruta 115 y Monte Cenizo. Indica escenario, coordenada,
pitch y si falla altura, eje, repeticion, textura de banda, roof o exclusion. Los escenarios
restantes se usan cuando el primer candidato visual ya permite revisar cada clase.
