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

1. Ve a Villa Raiz, ponte en (10,10) mirando al sur y pulsa F5 para recorrer `flat`,
   `v15`, `v35`, `v50` y `v75`; pulsa F6 en cada vista.
2. Ve a Ruta 115, ponte en (18,41) mirando al norte y recorre las mismas cinco vistas.
3. En ambos lugares, comprueba que la vista plana coincide con el renderer classic y que
   mapa, posicion, direccion y contenido no cambian entre vistas.
4. Sal y vuelve a cada mapa, repite las cinco vistas y confirma que no hay deriva.
5. F5 congela el frame y fija la ventana a 960x640; pulsa Ctrl+P al terminar para reanudar
   el juego y restaurar la ventana y el renderer anteriores.

Si algo falla, indica el mapa, la vista y lo observado. Si todo pasa, aprueba R1
explicitamente.
