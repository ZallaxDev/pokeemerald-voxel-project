# Pruebas manuales de paridad Diorama

Las pruebas manuales las ejecuta exclusivamente el usuario. Al terminar cada ciclo, el
agente debe indicar en espanol una lista breve con lugares, resultado esperado y
comprobaciones. No se piden capturas, hashes, comandos auxiliares ni manifests locales.

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
