/*
 * Control de brazo robótico (hombro + codo + garra) con servomotores - ESP32
 *
 * Librería necesaria: "ESP32Servo" (Kevin Harrington / John K. Bennett)
 *   Arduino IDE -> Herramientas -> Administrar bibliotecas -> buscar "ESP32Servo"
 *   (la librería "Servo" estándar de Arduino NO funciona en ESP32)
 *
 * Conexión de cada servo:
 *   Marrón  -> GND (común con el GND del ESP32)
 *   Rojo    -> 5V  (fuente externa, no el pin 3V3 del ESP32)
 *   Naranja -> pin de señal (ver PIN_... abajo)
 *
 * Comandos por Monitor Serie (115200 baudios, "Nueva línea"):
 *   a / c          -> abrir / cerrar garra
 *   garra 45       -> garra a 45 grados
 *   hombro 90      -> hombro a 90 grados
 *   codo 60        -> codo a 60 grados
 *   brazo 90 60    -> hombro a 90 y codo a 60 AL MISMO TIEMPO
 *   ?              -> ayuda
 */

#include <ESP32Servo.h>

// ---------------- Configuración ----------------
// Pines de señal (G13 = GPIO 13, etc.)
const int PIN_GARRA  = 13;
const int PIN_HOMBRO = 14;
const int PIN_CODO   = 27;

// Ángulos de la garra (calibrados)
const int ANGULO_GARRA_ABIERTA = 10;
const int ANGULO_GARRA_CERRADA = 89;

// Límites del hombro y del codo. AJUSTAR con el brazo real:
// muévelos de a poco desde el Monitor Serie y anota hasta dónde
// llegan sin chocar ni forzar la estructura.
const int HOMBRO_MIN     = 0;
const int HOMBRO_MAX     = 180;
const int HOMBRO_INICIAL = 90;

const int CODO_MIN       = 0;
const int CODO_MAX       = 180;
const int CODO_INICIAL   = 90;

// Velocidad: milisegundos de espera por cada grado (0 = movimiento inmediato)
const int MS_POR_GRADO = 8;

// Pulsos típicos de servos SG90 / MG90S / MG996R (en microsegundos)
const int PULSO_MIN_US = 500;
const int PULSO_MAX_US = 2400;
// -----------------------------------------------

// Una articulación = un servo + sus límites + su posición actual
struct Articulacion {
  const char* nombre;
  int pin;
  int minimo;
  int maximo;
  int actual;
  Servo servo;
};

Articulacion garra  = {"Garra",  PIN_GARRA,
                       min(ANGULO_GARRA_ABIERTA, ANGULO_GARRA_CERRADA),
                       max(ANGULO_GARRA_ABIERTA, ANGULO_GARRA_CERRADA),
                       ANGULO_GARRA_ABIERTA};
Articulacion hombro = {"Hombro", PIN_HOMBRO, HOMBRO_MIN, HOMBRO_MAX, HOMBRO_INICIAL};
Articulacion codo   = {"Codo",   PIN_CODO,   CODO_MIN,   CODO_MAX,   CODO_INICIAL};

void iniciarArticulacion(Articulacion &art) {
  art.servo.setPeriodHertz(50);  // Los servos analógicos trabajan a 50 Hz
  art.servo.attach(art.pin, PULSO_MIN_US, PULSO_MAX_US);
  art.servo.write(art.actual);   // Ir a la posición inicial
}

// Mueve UNA articulación (garra, hombro o codo) a un ángulo, de forma gradual.
// El ángulo se recorta a los límites de esa articulación.
void moverArticulacion(Articulacion &art, int anguloDestino) {
  anguloDestino = constrain(anguloDestino, art.minimo, art.maximo);

  if (MS_POR_GRADO == 0) {
    art.actual = anguloDestino;
    art.servo.write(art.actual);
    return;
  }

  while (art.actual != anguloDestino) {
    art.actual += (anguloDestino > art.actual) ? 1 : -1;
    art.servo.write(art.actual);
    delay(MS_POR_GRADO);
  }
}

// Mueve hombro y codo AL MISMO TIEMPO: ambos empiezan y terminan juntos,
// aunque uno tenga que recorrer más grados que el otro.
void moverBrazo(int anguloHombro, int anguloCodo) {
  anguloHombro = constrain(anguloHombro, hombro.minimo, hombro.maximo);
  anguloCodo   = constrain(anguloCodo,   codo.minimo,   codo.maximo);

  int inicioHombro = hombro.actual;
  int inicioCodo   = codo.actual;
  int pasos = max(abs(anguloHombro - inicioHombro), abs(anguloCodo - inicioCodo));

  if (MS_POR_GRADO == 0 || pasos == 0) {
    pasos = 1;
  }

  for (int i = 1; i <= pasos; i++) {
    hombro.actual = inicioHombro + (anguloHombro - inicioHombro) * i / pasos;
    codo.actual   = inicioCodo   + (anguloCodo   - inicioCodo)   * i / pasos;
    hombro.servo.write(hombro.actual);
    codo.servo.write(codo.actual);
    delay(MS_POR_GRADO);
  }
}

void abrirGarra() {
  moverArticulacion(garra, ANGULO_GARRA_ABIERTA);
}

void cerrarGarra() {
  moverArticulacion(garra, ANGULO_GARRA_CERRADA);
}

// true = abrir, false = cerrar
void controlarGarra(bool abrir) {
  if (abrir) {
    abrirGarra();
  } else {
    cerrarGarra();
  }
}

// ---------------- Monitor Serie ----------------
void mostrarPosicion() {
  Serial.printf("Hombro: %d | Codo: %d | Garra: %d\n",
                hombro.actual, codo.actual, garra.actual);
}

void mostrarAyuda() {
  Serial.println("Brazo listo. Comandos:");
  Serial.println("  a / c        -> abrir / cerrar garra");
  Serial.println("  garra 45     -> garra a 45 grados");
  Serial.println("  hombro 90    -> hombro a 90 grados");
  Serial.println("  codo 60      -> codo a 60 grados");
  Serial.println("  brazo 90 60  -> hombro y codo juntos");
  Serial.println("  ?            -> esta ayuda");
  mostrarPosicion();
}

void procesarComando(String comando) {
  comando.trim();
  comando.toLowerCase();

  // Separar "palabra numero1 numero2"
  int espacio1 = comando.indexOf(' ');
  String palabra = (espacio1 == -1) ? comando : comando.substring(0, espacio1);
  String resto   = (espacio1 == -1) ? ""      : comando.substring(espacio1 + 1);
  resto.trim();
  int espacio2 = resto.indexOf(' ');
  int valor1 = resto.toInt();
  int valor2 = (espacio2 == -1) ? 0 : resto.substring(espacio2 + 1).toInt();

  if (palabra == "a") {
    abrirGarra();
  } else if (palabra == "c") {
    cerrarGarra();
  } else if (palabra == "garra" && resto.length() > 0) {
    moverArticulacion(garra, valor1);
  } else if (palabra == "hombro" && resto.length() > 0) {
    moverArticulacion(hombro, valor1);
  } else if (palabra == "codo" && resto.length() > 0) {
    moverArticulacion(codo, valor1);
  } else if (palabra == "brazo" && espacio2 != -1) {
    moverBrazo(valor1, valor2);
  } else {
    mostrarAyuda();
    return;
  }
  mostrarPosicion();
}
// -----------------------------------------------

void setup() {
  Serial.begin(115200);

  iniciarArticulacion(hombro);
  iniciarArticulacion(codo);
  iniciarArticulacion(garra);

  delay(1000);  // Da tiempo a que el Monitor Serie se conecte
  mostrarAyuda();
}

void loop() {
  if (Serial.available()) {
    procesarComando(Serial.readStringUntil('\n'));
  }
}
