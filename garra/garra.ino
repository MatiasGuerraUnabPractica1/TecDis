/*
 * Control de garra mecánica con servomotor - ESP32
 *
 * Librería necesaria: "ESP32Servo" (Kevin Harrington / John K. Bennett)
 *   Arduino IDE -> Herramientas -> Administrar bibliotecas -> buscar "ESP32Servo"
 *   (la librería "Servo" estándar de Arduino NO funciona en ESP32)
 *
 * Conexión del servo:
 *   Marrón  -> GND (común con el GND del ESP32)
 *   Rojo    -> 5V  (idealmente fuente externa, no el pin 3V3 del ESP32)
 *   Naranja -> PIN_SERVO_GARRA (señal)
 */

#include <ESP32Servo.h>

// ---------------- Configuración ----------------
const int PIN_SERVO_GARRA = 13;   // GPIO de señal del servo

// Ángulos límite de la garra. AJUSTAR con la garra real:
// prueba valores con moverGarra() hasta encontrar los extremos
// sin que el servo fuerce contra la estructura.
const int ANGULO_ABIERTA = 90;
const int ANGULO_CERRADA = 10;

// Velocidad: milisegundos de espera por cada grado (0 = movimiento inmediato)
const int MS_POR_GRADO = 8;

// Pulsos típicos de un SG90/MG90S (en microsegundos)
const int PULSO_MIN_US = 500;
const int PULSO_MAX_US = 2400;
// -----------------------------------------------

Servo servoGarra;
int anguloActual = ANGULO_ABIERTA;

// Mueve la garra a un ángulo de forma gradual (evita golpes y picos de corriente)
void moverGarra(int anguloDestino) {
  // Mantener el ángulo dentro del rango seguro de la garra
  int minimo = min(ANGULO_ABIERTA, ANGULO_CERRADA);
  int maximo = max(ANGULO_ABIERTA, ANGULO_CERRADA);
  anguloDestino = constrain(anguloDestino, minimo, maximo);

  if (MS_POR_GRADO == 0) {
    servoGarra.write(anguloDestino);
  } else {
    int paso = (anguloDestino > anguloActual) ? 1 : -1;
    while (anguloActual != anguloDestino) {
      anguloActual += paso;
      servoGarra.write(anguloActual);
      delay(MS_POR_GRADO);
    }
  }
  anguloActual = anguloDestino;
}

void abrirGarra() {
  moverGarra(ANGULO_ABIERTA);
}

void cerrarGarra() {
  moverGarra(ANGULO_CERRADA);
}

// Función única: true = abrir, false = cerrar
void controlarGarra(bool abrir) {
  if (abrir) {
    abrirGarra();
  } else {
    cerrarGarra();
  }
}

void mostrarAyuda() {
  Serial.println("Garra lista. Comandos por Monitor Serie:");
  Serial.println("  a = abrir | c = cerrar | numero (ej. 45) = ir a ese angulo | h = ayuda");
}

void setup() {
  Serial.begin(115200);

  servoGarra.setPeriodHertz(50);  // Los servos analógicos trabajan a 50 Hz
  servoGarra.attach(PIN_SERVO_GARRA, PULSO_MIN_US, PULSO_MAX_US);
  servoGarra.write(anguloActual); // Posición inicial: abierta

  delay(1000);                    // Da tiempo a que el Monitor Serie se conecte
  mostrarAyuda();
}

void loop() {
  // Prueba manual desde el Monitor Serie (115200 baudios)
  if (Serial.available()) {
    String comando = Serial.readStringUntil('\n');
    comando.trim();

    if (comando == "a") {
      abrirGarra();
      Serial.println("Garra abierta");
    } else if (comando == "c") {
      cerrarGarra();
      Serial.println("Garra cerrada");
    } else if (comando.length() > 0 && isDigit(comando[0])) {
      moverGarra(comando.toInt());
      Serial.print("Angulo: ");
      Serial.println(anguloActual);
    } else {
      mostrarAyuda();               // "h" o cualquier comando desconocido
    }
  }
}
