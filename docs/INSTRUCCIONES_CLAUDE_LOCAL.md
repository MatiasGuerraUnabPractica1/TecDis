# Instrucciones para Claude (sesión local en el PC del usuario)

Hola, Claude. Este archivo lo escribió otra sesión de Claude que trabajó en la nube con el usuario.
Esa sesión no podía acceder al PC ni a la placa por USB. Tú sí puedes, porque corres en el PC del usuario.
Tu trabajo es **compilar y cargar el código en el ESP32 y ayudar a probarlo**.
Responde al usuario en **español** y con explicaciones simples: está aprendiendo.

---

## 1. Contexto del proyecto

- **Proyecto:** brazo robótico con 3 servomotores (hombro, codo y garra) controlado por un **ESP32**.
- **Repositorio:** https://github.com/MatiasGuerraUnabPractica1/TecDis
  - **Rama:** `claude/cool-ramanujan-w4wd0z`
  - **Sketch:** `garra/garra.ino`
  - Es posible que ya esté clonado en `C:\Users\Alumno\TecDis`. Si no, clónalo:
    ```powershell
    git clone -b claude/cool-ramanujan-w4wd0z https://github.com/MatiasGuerraUnabPractica1/TecDis.git
    ```
  - Si ya existe, actualízalo con `git pull` dentro de la carpeta.
- **Sistema:** Windows (PowerShell). Es un PC de laboratorio con usuario `Alumno`, así que puede no haber permisos de administrador.

### Hardware

| Elemento | Detalle |
|---|---|
| Placa | ESP32 clásico (chip **ESP32-D0WDQ6**). Usa la placa **ESP32 Dev Module**, FQBN `esp32:esp32:esp32`. **No** uses ESP32-P4 ni otras variantes, porque eso ya causó el error "This chip is ESP32, not ESP32-P4" |
| Chip USB | **CP210x** (Silicon Labs). Si no aparece ningún puerto COM, falta el driver CP210x |
| Puerto | Antes era **COM7**, pero puede cambiar. Detéctalo siempre |
| Garra | Servo SG90/MG90S en **GPIO 13**. Calibrada: abierta = **10°**, cerrada = **89°** |
| Hombro | Servo en **GPIO 14** (cables negro/rojo/amarillo). **Sin calibrar** |
| Codo | Servo en **GPIO 27** (cables negro/rojo/amarillo). **Sin calibrar** |
| Alimentación | Servos con fuente externa de 5V (o una LiPo 2S 7.4V **siempre a través de un regulador a 5V**), con GND común con el ESP32 |

En la placa, los pines están rotulados como `G13`, `G14` y `G27`.

### Librería necesaria

- **ESP32Servo** (de Kevin Harrington y John K. Bennett). La librería `Servo` estándar de Arduino **no** funciona en ESP32.
- En este PC se ha borrado varias veces, porque los perfiles del laboratorio se reinician. Si aparece el error `ESP32Servo.h: No such file or directory`, hay que reinstalarla.

---

## 2. Qué hacer, paso a paso

### Paso 1: encontrar arduino-cli

El Arduino IDE 2 trae `arduino-cli` incluido. Úsalo así, sin instalar nada:

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
& $cli version
```

Si no está ahí, búscalo en `C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe`, o pregúntale al usuario dónde instaló el Arduino IDE. Si no hay IDE, propón instalar arduino-cli con `winget install ArduinoSA.CLI`, pero pide permiso antes.

**El Arduino IDE debe estar cerrado** mientras usas el puerto COM. Si no, el puerto queda ocupado.

### Paso 2: preparar el entorno

```powershell
& $cli core update-index
& $cli core list                       # ¿está esp32:esp32?
& $cli core install esp32:esp32        # solo si falta (descarga pesada, varios minutos)
& $cli lib list                        # ¿está ESP32Servo?
& $cli lib install ESP32Servo          # solo si falta
```

### Paso 3: detectar el puerto

```powershell
& $cli board list
```

Busca el puerto que aparezca como CP210x o "Silicon Labs". Si no aparece ninguno:
1. Pídele al usuario que pruebe **otro cable USB** (muchos solo cargan y no transmiten datos).
2. Revisa el Administrador de dispositivos: `Get-PnpDevice -PresentOnly | Where-Object { $_.FriendlyName -match 'CP210|USB.*UART|Serial' }`
3. Si falta el driver, se instala desde el paquete "CP210x Universal Windows Driver" de Silicon Labs: clic derecho en `silabser.inf` → Instalar. Requiere permisos de administrador.

### Paso 4: compilar y cargar

```powershell
cd C:\Users\Alumno\TecDis
& $cli compile --fqbn esp32:esp32:esp32 garra
& $cli upload  --fqbn esp32:esp32:esp32 -p COM7 garra   # usa el puerto detectado
```

- Los mensajes `#pragma message: BT: forcing BR/EDR ...` **no son errores**. Ignóralos.
- Si la carga se queda en `Connecting....`, pídele al usuario que **mantenga presionado el botón BOOT** de la placa hasta que empiece a escribir.
- Una carga correcta termina con `Hash of data verified.` y `Hard resetting via RTS pin...`.

### Paso 5: probar por el puerto serie

El sketch usa **115200 baudios** y recibe comandos terminados en salto de línea (`\n`):

| Comando | Acción |
|---|---|
| `?` | Muestra la ayuda y la posición actual |
| `a` / `c` | Abrir / cerrar garra |
| `garra 45` | Garra a 45° |
| `hombro 90` | Hombro a 90° |
| `codo 60` | Codo a 60° |
| `brazo 90 60` | Hombro y codo al mismo tiempo |

Después de cada comando, el ESP32 responde con `Hombro: X | Codo: Y | Garra: Z`.

Para enviar un comando y leer la respuesta desde PowerShell, sin monitor interactivo:

```powershell
function Send-Brazo([string]$cmd, [string]$port = "COM7") {
  $sp = New-Object System.IO.Ports.SerialPort $port, 115200
  $sp.NewLine = "`n"; $sp.ReadTimeout = 3000
  $sp.Open()
  Start-Sleep -Milliseconds 2500   # abrir el puerto reinicia el ESP32; esperar el arranque
  $sp.ReadExisting() | Out-Null    # descartar mensajes de arranque
  $sp.WriteLine($cmd)
  Start-Sleep -Milliseconds 1500   # dar tiempo al movimiento
  $out = $sp.ReadExisting()
  $sp.Close()
  $out
}
Send-Brazo "?"
```

Al abrir el puerto, el ESP32 se reinicia y los servos vuelven a su posición inicial: garra a 10°, hombro y codo a 90°.

---

## 3. Reglas de seguridad (importante)

- **Avisa al usuario y espera su confirmación antes de cada movimiento de servo.** El brazo puede chocar, y el usuario tiene que mirar el brazo, no la pantalla.
- **El hombro y el codo no están calibrados** (sus límites en el código son 0–180). Para calibrarlos:
  - Muévelos en **pasos pequeños** (5–10°) desde 90°.
  - Pregúntale al usuario después de cada paso si el brazo choca, zumba o fuerza.
  - Anota con el usuario los límites seguros y la posición inicial, y actualiza en `garra/garra.ino` las constantes `HOMBRO_MIN`, `HOMBRO_MAX`, `HOMBRO_INICIAL`, `CODO_MIN`, `CODO_MAX` y `CODO_INICIAL`.
- Si un servo zumba o se calienta, aléjalo de inmediato del extremo.
- Si en el monitor aparece `Brownout detector was triggered`, el ESP32 se está quedando sin energía. Los servos tienen que ir a la fuente externa de 5V, no al USB.
- No modifiques los ángulos de la garra (10 / 89) salvo que el usuario lo pida.

---

## 4. Al terminar

- Si cambiaste el código, haz commit en la rama `claude/cool-ramanujan-w4wd0z` y súbelo con `git push`. Antes, pregúntale al usuario.
- Resume para el usuario qué funcionó, qué ángulos quedaron calibrados y qué falta.
