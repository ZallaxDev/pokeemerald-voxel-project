# G0: prototipos voxel retirados

G0 impide que las reglas schema v1 incompatibles se mezclen con el futuro modelo
de ocupacion. El overworld sigue usando el renderer 3D generico; el arte retirado
se conserva como suelo plano original hasta disponer de un reemplazo aprobado.

| Prototipo retirado | Cantidad | Sustitucion prevista |
|---|---:|---|
| Overrides de edificios de Villa Raiz | 59 | Patrones exactos y edificios volumetricos de G11-G12 |
| Overrides de ledge de Ruta 101 | 4 | Reglas reutilizables `MB_JUMP_*` ya activas |
| Carteles expandidos desde reglas locales | 5 | Preset global de eventos y ocupacion de G3/G9 |
| Pins de vegetacion `cutout` en General | 23 | Hulls y agrupacion por firma de arte de G8 |
| Regla `cutout` de hierba alta | 1 | Base plana y mechones con orden de personaje de G8 |
| Plantillas de edificio | 2 | Catalogo exacto y shell volumetrico de G11-G12 |
| Colocaciones manuales de edificio | 3 | Coincidencia automatica de matrices de G11 |
| Muestras de perfil de tejado | 194 | Ocupacion por spans y procedencia de G4/G12 |

## Barreras

- El compilador rechaza `cutout`, `roof` y `building-part` en schema v1.
- El compilador rechaza plantillas y colocaciones schema v1 no vacias.
- `eventRules` ya no forma parte del documento de mapa aceptado.
- El editor no ofrece controles para volver a crear esos prototipos.
- Los arrays generados de pins, overrides, edificios, perfiles y colocaciones
  permanecen vacios y las pruebas lo verifican.
- La metadata de edificio ya no se adjunta a una regla ganadora independiente.

No se modificaron `data/layouts/`, `data/maps/*/map.json`, binarios de mapa,
colision, elevacion de gameplay, eventos, scripts, warps ni guardados.
