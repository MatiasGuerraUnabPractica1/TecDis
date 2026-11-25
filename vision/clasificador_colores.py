"""
Clasificador de cajitas por color para el brazo robótico.

- Lee la imagen de la ESP32-CAM por WiFi.
- Si ve una cajita VERDE en la zona de agarre  -> envía "agarrar izquierda" al brazo.
- Si ve una cajita ROJA en la zona de agarre   -> envía "agarrar derecha".
- Espera a que el brazo termine la rutina y a que la zona quede vacía
  antes de buscar el siguiente objeto.

Requisitos (una sola vez, en una terminal):
    pip install opencv-python numpy pyserial

Uso:
    python clasificador_colores.py

Teclas en la ventana de video:
    q -> salir      p -> pausar/reanudar la detección
"""

import time
import urllib.request

import cv2
import numpy as np

# ===================== CONFIGURACIÓN =====================
# Dirección de la cámara (la página que abriste en el navegador)
CAMARA_IP = "10.227.104.225"

# Cómo leer la imagen:
#   "foto"   -> pide una foto a la vez a  http://IP/foto
#   "stream" -> lee el video continuo de  http://IP:81/stream
# Si una no funciona, prueba con la otra.
MODO_CAMARA = "foto"
URL_FOTO = f"http://{CAMARA_IP}/foto"       # enlace "Tomar una foto" de la página
URL_STREAM = f"http://{CAMARA_IP}:81/stream"
TIMEOUT_CAMARA = 10   # segundos

# Puerto del ESP32 del brazo (mira en Arduino IDE -> Herramientas -> Puerto).
# Pon None para probar solo la cámara, sin mover el brazo.
PUERTO_BRAZO = "COM7"
BAUDIOS = 115200

# Zona de agarre: rectángulo de la imagen donde se pone la cajita.
# Valores en fracción del ancho/alto (0.0 a 1.0). Ajusta según tu cámara.
ZONA_X1, ZONA_Y1 = 0.30, 0.30
ZONA_X2, ZONA_Y2 = 0.70, 0.70

# Fracción mínima de la zona que debe ocupar el color para contar como "cajita"
AREA_MINIMA = 0.08

# Cuántas fotos seguidas tiene que ver el mismo color antes de actuar
# (evita reaccionar a un reflejo o a la mano pasando)
FOTOS_PARA_CONFIRMAR = 4

# Rangos de color en HSV (H: 0-179, S: 0-255, V: 0-255).
# El rojo está en los dos extremos del círculo de colores, por eso tiene 2 rangos.
ROJO_BAJO_1, ROJO_ALTO_1 = (0, 120, 70), (10, 255, 255)
ROJO_BAJO_2, ROJO_ALTO_2 = (170, 120, 70), (179, 255, 255)
VERDE_BAJO, VERDE_ALTO = (40, 80, 50), (85, 255, 255)

# Comando que se envía al brazo según el color
COMANDO_POR_COLOR = {
    "verde": "agarrar izquierda",
    "rojo": "agarrar derecha",
}

# Tiempo máximo que se espera a que el brazo termine una rutina (segundos)
TIMEOUT_RUTINA = 60
# =========================================================


_stream = None


def leer_foto():
    """Obtiene una imagen de la ESP32-CAM como imagen de OpenCV."""
    global _stream
    if MODO_CAMARA == "stream":
        if _stream is None or not _stream.isOpened():
            _stream = cv2.VideoCapture(URL_STREAM)
        ok, imagen = _stream.read()
        if not ok:
            _stream.release()
            _stream = None
            raise IOError(f"no llega video desde {URL_STREAM}")
        return imagen

    with urllib.request.urlopen(URL_FOTO, timeout=TIMEOUT_CAMARA) as respuesta:
        datos = np.frombuffer(respuesta.read(), dtype=np.uint8)
    return cv2.imdecode(datos, cv2.IMREAD_COLOR)


def recortar_zona(imagen):
    alto, ancho = imagen.shape[:2]
    x1, y1 = int(ZONA_X1 * ancho), int(ZONA_Y1 * alto)
    x2, y2 = int(ZONA_X2 * ancho), int(ZONA_Y2 * alto)
    return imagen[y1:y2, x1:x2], (x1, y1, x2, y2)


def detectar_color(zona):
    """Devuelve ("rojo" | "verde" | None, fraccion_rojo, fraccion_verde)."""
    hsv = cv2.cvtColor(zona, cv2.COLOR_BGR2HSV)
    hsv = cv2.GaussianBlur(hsv, (5, 5), 0)

    mascara_rojo = cv2.inRange(hsv, ROJO_BAJO_1, ROJO_ALTO_1) | \
                   cv2.inRange(hsv, ROJO_BAJO_2, ROJO_ALTO_2)
    mascara_verde = cv2.inRange(hsv, VERDE_BAJO, VERDE_ALTO)

    total = zona.shape[0] * zona.shape[1]
    frac_rojo = cv2.countNonZero(mascara_rojo) / total
    frac_verde = cv2.countNonZero(mascara_verde) / total

    if max(frac_rojo, frac_verde) < AREA_MINIMA:
        return None, frac_rojo, frac_verde
    return ("rojo" if frac_rojo > frac_verde else "verde"), frac_rojo, frac_verde


def abrir_brazo():
    # Acepta None, "None" o "" como "sin brazo" (modo prueba)
    if PUERTO_BRAZO is None or str(PUERTO_BRAZO).strip().lower() in ("", "none"):
        print("Modo prueba: no se conecta el brazo.")
        return None
    import serial
    brazo = serial.Serial(PUERTO_BRAZO, BAUDIOS, timeout=1)
    print(f"Conectado al brazo en {PUERTO_BRAZO}. Esperando que arranque...")
    time.sleep(3)                 # abrir el puerto reinicia el ESP32
    brazo.reset_input_buffer()
    return brazo


def ejecutar_rutina(brazo, comando):
    """Envía el comando y espera a que el brazo diga que terminó."""
    print(f">>> Enviando: {comando}")
    if brazo is None:
        time.sleep(2)             # modo prueba: simula la rutina
        return
    brazo.write((comando + "\n").encode())
    limite = time.time() + TIMEOUT_RUTINA
    while time.time() < limite:
        linea = brazo.readline().decode(errors="ignore").strip()
        if linea:
            print(f"    brazo: {linea}")
        if "Rutina terminada" in linea:
            return
    print("!!! El brazo no respondió a tiempo. Revisa la conexión.")


def dibujar(imagen, rect, color, frac_rojo, frac_verde, estado):
    x1, y1, x2, y2 = rect
    tono = {"rojo": (0, 0, 255), "verde": (0, 200, 0)}.get(color, (255, 255, 255))
    cv2.rectangle(imagen, (x1, y1), (x2, y2), tono, 2)
    textos = [
        f"Estado: {estado}",
        f"Rojo: {frac_rojo:.0%}  Verde: {frac_verde:.0%}",
        f"Detectado: {color or '-'}",
    ]
    for i, texto in enumerate(textos):
        cv2.putText(imagen, texto, (10, 25 + 25 * i),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 0), 2)


def main():
    brazo = abrir_brazo()
    ultimo_color, repeticiones = None, 0
    esperando_zona_vacia = False
    pausado = False

    print("Mirando la cámara... (q = salir, p = pausar)")
    while True:
        try:
            imagen = leer_foto()
        except Exception as error:
            print(f"No pude leer la cámara ({error}). Reintentando...")
            time.sleep(1)
            continue
        if imagen is None:
            print(f"La cámara respondió, pero no con una imagen. Revisa URL_FOTO ({URL_FOTO}).")
            time.sleep(1)
            continue

        zona, rect = recortar_zona(imagen)
        color, frac_rojo, frac_verde = detectar_color(zona)

        if pausado:
            estado = "PAUSADO"
        elif esperando_zona_vacia:
            estado = "Retira el objeto de la zona"
            if color is None:
                esperando_zona_vacia = False
        else:
            estado = "Buscando cajita"
            if color is not None and color == ultimo_color:
                repeticiones += 1
            else:
                repeticiones = 1 if color else 0
            ultimo_color = color

            if color and repeticiones >= FOTOS_PARA_CONFIRMAR:
                estado = f"Moviendo cajita {color}"
                dibujar(imagen, rect, color, frac_rojo, frac_verde, estado)
                cv2.imshow("Camara", imagen)
                cv2.waitKey(1)
                ejecutar_rutina(brazo, COMANDO_POR_COLOR[color])
                ultimo_color, repeticiones = None, 0
                esperando_zona_vacia = True
                continue

        dibujar(imagen, rect, color, frac_rojo, frac_verde, estado)
        cv2.imshow("Camara", imagen)
        tecla = cv2.waitKey(1) & 0xFF
        if tecla == ord("q"):
            break
        if tecla == ord("p"):
            pausado = not pausado

    cv2.destroyAllWindows()
    if brazo is not None:
        brazo.close()


if __name__ == "__main__":
    main()
