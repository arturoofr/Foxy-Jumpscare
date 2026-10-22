# Foxy Jumpscare

Aplicacion ligera en C++ nativo (Win32 + Direct3D 11 + Media Foundation) para Windows que se ejecuta en segundo plano dentro de la bandeja del sistema (System Tray) y reproduce de forma aleatoria el jumpscare de Foxy como una superposicion transparente en la pantalla principal sobre cualquier juego o aplicacion activa.

---

## Descarga directa (listo para usar - v1.0.2)

Si no deseas compilar el proyecto por tu cuenta, puedes descargar y ejecutar directamente la version precompilada incluida en el repositorio o desde la seccion [Releases](https://github.com/arturoofr/Foxy-Jumpscare/releases/tag/v1.0.2):

- **Paquete portable completo (.zip):** [`release/FoxyJumpscare-Portable.zip`](release/FoxyJumpscare-Portable.zip)
- **Ejecutable directo (.exe):** [`release/FoxyJumpscare.exe`](release/FoxyJumpscare.exe)

> El ejecutable ya lleva embebidos tanto el icono como el video `.mp4`, por lo que puede ejecutarse de forma completamente independiente o junto a los archivos de la carpeta `release/`.

---

## Caracteristicas principales

- **Ejecucion en bandeja del sistema (Tray):** Consumo minimo de RAM y CPU en segundo plano sin mostrar ventanas en la barra de tareas.
- **Reproduccion en pantalla principal:** El jumpscare se superpone exclusivamente en el monitor principal (`SM_CXSCREEN` x `SM_CYSCREEN`).
- **Sistema de tiradas por doble numero (1/X):** Cada segundo se generan dos numeros aleatorios independientes de 64 bits entre `1` y el limite `X` configurado; si ambos numeros coinciden (`roll1 == roll2`), se ejecuta el jumpscare.
- **Eliminacion de fondo verde (Chroma Key):** Procesamiento acelerado con Direct3D 11 y filtrado de color para eliminar por completo el fondo verde del video `.mp4`.
- **Superposicion no intrusiva (Click-Through):** La ventana del jumpscare utiliza `WS_EX_TRANSPARENT` y `WS_EX_NOACTIVATE`. No roba el foco del teclado ni del raton, permitiendo seguir jugando o utilizando cualquier aplicacion mientras se reproduce la animacion.
- **Persistencia automatica:** Toda la configuracion se guarda automaticamente en `config.ini`.

---

## Uso y opciones del menu

Al iniciar `FoxyJumpscare.exe`, aparecera el icono de Foxy en la bandeja del sistema (junto al reloj de Windows). Haz clic derecho (o izquierdo) sobre el icono para abrir el menu con las siguientes opciones:

1. **Cambiar probabilidad:**
   Permite elegir la probabilidad por segundo de que ocurra el jumpscare:
   - `1/100`
   - `1/500`
   - `1/700`
   - `1/1000` (por defecto)
   - `1/10000`
   - `Personalizada...`: Permite introducir cualquier divisor $N$ para una probabilidad de $1/N$, con un limite minimo de `1/100` (divisor minimo `100`).
2. **Activado (Desactivar) / Desactivado (Activar):**
   Pausa o reanuda las tiradas aleatorias del programa sin necesidad de cerrarlo.
3. **Iniciar con Windows:**
   Activa o desactiva el arranque automatico del programa al iniciar sesion en Windows (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`).
4. **Volumen (0% - 100%):**
   Abre una ventana con un deslizador interactivo de `0%` a `100%` para ajustar el volumen del jumpscare en tiempo real.
5. **Cerrar:**
   Elimina el icono de la bandeja del sistema y cierra el programa por completo.

---

## Estructura del proyecto

```text
FoxyJumpscare/
├── assets/
│   ├── foxyicon.ico         # Icono de la aplicacion y bandeja del sistema
│   └── foxyjumpscare.mp4    # Video del jumpscare con fondo verde
├── release/
│   ├── FoxyJumpscare.exe    # Ejecutable optimizado listo para usar
│   ├── foxyicon.ico
│   ├── foxyjumpscare.mp4
│   └── FoxyJumpscare-Portable.zip
├── src/
│   ├── main.cpp             # Codigo fuente principal en C++
│   ├── resource.h           # Identificadores de recursos y controles
│   └── resource.rc          # Definicion de dialogos y recursos embebidos
├── build.bat                # Script de compilacion automatizada con MSVC
└── README.md
```

---

## Compilacion desde el codigo fuente

### Requisitos
- Windows 10 o Windows 11 (x64)
- Visual Studio (Desktop development with C++ / MSVC Build Tools)

### Compilar
Ejecuta el script `build.bat` desde la raiz del proyecto:

```bat
build.bat
```

El ejecutable optimizado se generara automaticamente en `release\FoxyJumpscare.exe`.
