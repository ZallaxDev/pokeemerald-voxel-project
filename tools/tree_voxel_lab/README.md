# Emerald Tree Voxel Lab 🌲🧊

Laboratorio independiente para montar, analizar, separar, voxelizar, editar manualmente y exportar árboles en 3D a partir de tilesets de Pokémon Esmeralda.

## Requisitos de Sistema

- **Sistema Operativo**: Windows 10 / Windows 11 (también ejecutable en Linux/macOS con Python).
- **Python**: Versión 3.11 o superior.
- **Navegador**: Google Chrome o Microsoft Edge actualizado.
- **Sin Dependencias Externas**: 100% ejecutable localmente en `127.0.0.1:8765` sin conexión a Internet, claves API ni bases de datos.

---

## Estructura del Proyecto

Todo el código y archivos generados residen exclusivamente en:

```
tools/tree_voxel_lab/
├── requirements.txt
├── install.bat
├── start.bat
├── start-dev.bat
├── run-tests.bat
├── app.py
├── README.md
├── app/
│   ├── main.py
│   ├── config.py
│   ├── models/
│   ├── api/
│   ├── services/
│   ├── static/
│   │   ├── index.html
│   │   ├── styles.css
│   │   ├── js/
│   │   └── vendor/
│   │       ├── three.min.js
│   │       └── OrbitControls.js
├── examples/
│   └── synthetic-tree.png
└── tests/
```

---

## Instrucciones de Instalación y Uso

### 1. Instalación (Primera vez)
Haz doble clic en `install.bat` (o ejecuta en consola):

```cmd
install.bat
```

Este script:
- Verifica Python 3.11+.
- Crea el entorno virtual en `tools/tree_voxel_lab/.venv/`.
- Instala todas las dependencias requeridas en `requirements.txt`.

### 2. Arranque Normal
Haz doble clic en `start.bat` (o ejecuta en consola):

```cmd
start.bat
```

Servidor iniciado en `http://127.0.0.1:8765`. Se abrirá automáticamente el navegador web predeterminado.

### 3. Modo Desarrollo
Para recarga automática en caliente ante cambios de código:

```cmd
start-dev.bat
```

### 4. Pruebas Automatizadas (Pytest)
Para verificar la integridad de todos los módulos y algoritmos de geometría:

```cmd
run-tests.bat
```

---

## Seguridad y Verificación de Red Local

1. El servidor FastAPI se ejecuta estrictamente en la interfaz de bucle invertido:
   `http://127.0.0.1:8765`
2. Para comprobar en consola que únicamente escucha en localhost (no expuesto a la red local):
   ```cmd
   netstat -ano | findstr 8765
   ```
   Debe mostrar únicamente `127.0.0.1:8765`.

---

## Limpieza y Desinstalación

- **Limpiar Archivos Temporales**: Borra el contenido de la carpeta `.tmp/`.
- **Eliminar por completo la herramienta**: Simplemente borra la carpeta `tools/tree_voxel_lab/`. No afectará en nada al proyecto ni al juego original.
