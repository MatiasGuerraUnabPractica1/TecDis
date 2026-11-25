/*
 * Control de brazo robótico (base + hombro + codo + garra) con servomotores - ESP32
 *
 * Librería necesaria: "ESP32Servo" (Kevin Harrington / John K. Bennett)
 *   Arduino IDE -> Herramientas -> Administrar bibliotecas -> buscar "ESP32Servo"
 *   (la librería "Servo" estándar de Arduino NO funciona en ESP32)
 *
 * Servos:
 *   Base           -> servo que gira todo el brazo (ver PULSO_..._BASE)
 *   Garra          -> micro servo 9g (SG90/MG90S)
 *   Hombro y codo  -> Hitec HS-475HB (cables negro/rojo/amarillo)
 *
 * Conexión de cada servo:
 *   Marrón  -> GND (común con el GND del ESP32)
 *   Rojo    -> 5V  (fuente externa, no el pin 3V3 del ESP32)
 *   Naranja -> pin de señal (ver PIN_... abajo)
 *
 * Comandos por Monitor Serie (115200 baudios, "Nueva línea"):
 *   agarrar derecha   -> rutina completa: agarra, lleva el objeto a la derecha,
 *                        lo suelta y vuelve a reposo
 *   agarrar izquierda -> lo mismo, pero hacia la izquierda
 *   agarrar / soltar  -> solo la secuencia de agarrar o de soltar
 *   reposo            -> posición de reposo (centro)
 *   izquierda / derecha / centro -> mover solo la base
 *
 *   b 90           -> base a 90 grados     (también: base 90)
 *   a / c          -> abrir / cerrar garra
 *   h 45           -> hombro a 45 grados   (también: hombro 45)
 *   c 33           -> codo a 33 grados     (también: codo 33)
 *   g 50           -> garra a 50 grados    (también: garra 50)
 *   brazo 90 60    -> hombro a 90 y codo a 60 AL MISMO TIEMPO
 *   x              -> soltar todos los servos (dejan de hacer fuerza y de zumbar)
 *   ?              -> ayuda
 */

#include <ESP32Servo.h>

// ---------------- Configuración ----------------
// Pines de señal (G13 = GPIO 13, etc.)
const int PIN_GARRA  = 13;
const int PIN_HOMBRO = 14;
const int PIN_CODO   = 27;
const int PIN_BASE   = 26;

// Ángulos de la garra (calibrados)
const int ANGULO_GARRA_ABIERTA = 10;
const int ANGULO_GARRA_CERRADA = 89;

// Límites del hombro y del codo. AJUSTAR con el brazo real:
// muévelos de a poco desde el Monitor Serie y anota hasta dónde
// llegan sin chocar ni forzar la estructura.
const int HOMBRO_MIN     = 0;
const int HOMBRO_MAX     = 180;
const int HOMBRO_INICIAL = 120;  // posición base del hombro

const int CODO_MIN       = 0;
const int CODO_MAX       = 180;
const int CODO_INICIAL   = 180;  // posición base del codo

// Límites de la base (giro de todo el brazo). AJUSTAR igual que el resto.
const int BASE_MIN       = 0;
const int BASE_MAX       = 180;
const int BASE_INICIAL   = 95;   // posición base: centro (origen)

// ---- Posiciones de las rutinas automáticas ----
const int BASE_IZQUIERDA = 0;
const int BASE_DERECHA   = 180;
const int BASE_CENTRO    = 95;

const int REPOSO_HOMBRO  = 80;   // posición de reposo del brazo
const int REPOSO_CODO    = 120;

const int HOMBRO_ARRIBA  = 130;  // brazo levantado (con o sin objeto)
const int CODO_ALCANCE   = 160;  // codo estirado hacia el objeto
const int HOMBRO_ABAJO   = 170;  // brazo bajado hasta el objeto

const int PAUSA_PASO_MS  = 300;  // pausa entre cada paso de una rutina
const int PAUSA_AGARRE_MS = 500; // pausa extra después de cerrar/abrir la garra

// Velocidad: milisegundos de espera por cada grado (0 = movimiento inmediato)
const int MS_POR_GRADO = 8;

// Rango de pulso de cada tipo de servo (en microsegundos).
// Micro servo 9g: 500-2400 us.
// Hitec HS-475HB: se usa 900-2100 us (rango seguro recomendado por Hitec).
// Pedirle más de lo que el servo da físicamente lo hace forzar y ZUMBAR.
const int PULSO_MIN_9G     = 500;
const int PULSO_MAX_9G     = 2400;
const int PULSO_MIN_HS475  = 900;
const int PULSO_MAX_HS475  = 2100;
// Servo de la base: por defecto se asume otro HS-475HB.
// Si es otro modelo, cambia estos dos valores (ej. 500-2400 para un MG996R).
const int PULSO_MIN_BASE   = PULSO_MIN_HS475;
const int PULSO_MAX_BASE   = PULSO_MAX_HS475;
// -----------------------------------------------

// Una articulación = un servo + sus límites + su posición actual
struct Articulacion {
  const char* nombre;
  int pin;
  int minimo;
  int maximo;
  int actual;
  int pulsoMin;
  int pulsoMax;
  Servo servo;
};

Articulacion garra  = {"Garra",  PIN_GARRA,
                       min(ANGULO_GARRA_ABIERTA, ANGULO_GARRA_CERRADA),
                       max(ANGULO_GARRA_ABIERTA, ANGULO_GARRA_CERRADA),
                       ANGULO_GARRA_ABIERTA, PULSO_MIN_9G, PULSO_MAX_9G};
Articulacion base   = {"Base",   PIN_BASE,   BASE_MIN,   BASE_MAX,   BASE_INICIAL,
                       PULSO_MIN_BASE, PULSO_MAX_BASE};
Articulacion hombro = {"Hombro", PIN_HOMBRO, HOMBRO_MIN, HOMBRO_MAX, HOMBRO_INICIAL,
                       PULSO_MIN_HS475, PULSO_MAX_HS475};
Articulacion codo   = {"Codo",   PIN_CODO,   CODO_MIN,   CODO_MAX,   CODO_INICIAL,
                       PULSO_MIN_HS475, PULSO_MAX_HS475};

void iniciarArticulacion(Articulacion &art) {
  art.servo.setPeriodHertz(50);  // Los servos analógicos trabajan a 50 Hz
  art.servo.attach(art.pin, art.pulsoMin, art.pulsoMax);
  art.servo.write(art.actual);   // Ir a la posición inicial
}

// Deja de mandar señal: el servo se suelta y deja de hacer fuerza (y de zumbar)
void soltarArticulacion(Articulacion &art) {
  art.servo.detach();
}

// Si el servo estaba suelto, lo vuelve a conectar en su posición actual
void asegurarConectado(Articulacion &art) {
  if (!art.servo.attached()) {
    iniciarArticulacion(art);
  }
}

// Mueve UNA articulación (base, garra, hombro o codo) a un ángulo, de forma gradual.
// El ángulo se recorta a los límites de esa articulación.
void moverArticulacion(Articulacion &art, int anguloDestino) {
  asegurarConectado(art);
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
  asegurarConectado(hombro);
  asegurarConectado(codo);
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

// ---------------- Rutinas automáticas ----------------
void pausa() {
  delay(PAUSA_PASO_MS);
}

void baseIzquierda() { moverArticulacion(base, BASE_IZQUIERDA); }
void baseDerecha()   { moverArticulacion(base, BASE_DERECHA); }
void baseCentro()    { moverArticulacion(base, BASE_CENTRO); }

// Reposo: h 80 -> c 120 -> b 95
void posicionReposo() {
  moverArticulacion(hombro, REPOSO_HOMBRO);
  pausa();
  moverArticulacion(codo, REPOSO_CODO);
  pausa();
  baseCentro();
}

// Levanta el brazo (con o sin objeto): h 80 -> c 120
void levantar() {
  moverArticulacion(hombro, REPOSO_HOMBRO);
  pausa();
  moverArticulacion(codo, REPOSO_CODO);
  pausa();
}

// Baja hasta el objeto: h 130 -> c 160 -> h 170
void bajarAlObjeto() {
  moverArticulacion(hombro, HOMBRO_ARRIBA);
  pausa();
  moverArticulacion(codo, CODO_ALCANCE);
  pausa();
  moverArticulacion(hombro, HOMBRO_ABAJO);
  pausa();
}

// Agarrar: h 130 -> c 160 -> h 170 -> c (cerrar garra)
void agarrar() {
  bajarAlObjeto();
  cerrarGarra();
  delay(PAUSA_AGARRE_MS);
}

// Soltar: h 130 -> c 160 -> h 170 -> a (abrir garra)
void soltar() {
  bajarAlObjeto();
  abrirGarra();
  delay(PAUSA_AGARRE_MS);
}

// Rutina completa: agarra el objeto al centro, lo lleva hacia
// anguloBase (izquierda o derecha), lo suelta y vuelve a reposo.
void agarrarYLlevar(int anguloBase, const char* lado) {
  Serial.printf(">> Agarrar y llevar a la %s\n", lado);

  Serial.println("1/7 Base al centro y garra abierta");
  baseCentro();
  abrirGarra();
  pausa();

  Serial.println("2/7 Agarrando el objeto");
  agarrar();

  Serial.println("3/7 Levantando el objeto");
  levantar();

  Serial.printf("4/7 Girando la base a la %s\n", lado);
  moverArticulacion(base, anguloBase);
  pausa();

  Serial.println("5/7 Bajando y soltando el objeto");
  soltar();

  Serial.println("6/7 Levantando la garra");
  levantar();

  Serial.println("7/7 Volviendo a reposo");
  posicionReposo();

  Serial.println(">> Rutina terminada");
}

// ---------------- Monitor Serie ----------------
void mostrarPosicion() {
  Serial.printf("Base: %d | Hombro: %d | Codo: %d | Garra: %d\n",
                base.actual, hombro.actual, codo.actual, garra.actual);
}

void mostrarAyuda() {
  Serial.println("Brazo listo. Comandos:");
  Serial.println("  agarrar derecha / agarrar izquierda -> rutina completa");
  Serial.println("  agarrar / soltar / reposo");
  Serial.println("  izquierda / derecha / centro        -> mover la base");
  Serial.println("  b 90         -> base a 90 grados");
  Serial.println("  a / c        -> abrir / cerrar garra");
  Serial.println("  h 45         -> hombro a 45 grados");
  Serial.println("  c 33         -> codo a 33 grados");
  Serial.println("  g 50         -> garra a 50 grados");
  Serial.println("  brazo 90 60  -> hombro y codo juntos");
  Serial.println("  x            -> soltar todos los servos");
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

  bool hayNumero = resto.length() > 0;

  if (palabra == "agarrar" && resto == "derecha") {
    agarrarYLlevar(BASE_DERECHA, "derecha");
  } else if (palabra == "agarrar" && resto == "izquierda") {
    agarrarYLlevar(BASE_IZQUIERDA, "izquierda");
  } else if (palabra == "agarrar" && !hayNumero) {
    agarrar();
  } else if (palabra == "soltar" && !hayNumero) {
    soltar();
  } else if (palabra == "reposo") {
    posicionReposo();
  } else if (palabra == "izquierda") {
    baseIzquierda();
  } else if (palabra == "derecha") {
    baseDerecha();
  } else if (palabra == "centro") {
    baseCentro();
  } else if (palabra == "a") {
    abrirGarra();
  } else if (palabra == "c" && !hayNumero) {
    cerrarGarra();                          // "c" solo = cerrar garra
  } else if ((palabra == "b" || palabra == "base") && hayNumero) {
    moverArticulacion(base, valor1);        // "b 90"
  } else if ((palabra == "h" || palabra == "hombro") && hayNumero) {
    moverArticulacion(hombro, valor1);      // "h 45"
  } else if ((palabra == "c" || palabra == "codo") && hayNumero) {
    moverArticulacion(codo, valor1);        // "c 33"
  } else if ((palabra == "g" || palabra == "garra") && hayNumero) {
    moverArticulacion(garra, valor1);       // "g 50"
  } else if (palabra == "brazo" && espacio2 != -1) {
    moverBrazo(valor1, valor2);
  } else if (palabra == "x") {
    soltarArticulacion(base);
    soltarArticulacion(hombro);
    soltarArticulacion(codo);
    soltarArticulacion(garra);
    Serial.println("Servos sueltos. Cualquier comando de movimiento los vuelve a activar.");
    return;
  } else {
    mostrarAyuda();
    return;
  }
  mostrarPosicion();
}
// -----------------------------------------------

void setup() {
  Serial.begin(115200);

  // Uno por uno, con una pausa, para no pedir mucha corriente de golpe
  iniciarArticulacion(base);
  delay(300);
  iniciarArticulacion(hombro);
  delay(300);
  iniciarArticulacion(codo);
  delay(300);
  iniciarArticulacion(garra);

  delay(1000);  // Da tiempo a que el Monitor Serie se conecte
  mostrarAyuda();
}

void loop() {
  if (Serial.available()) {
    procesarComando(Serial.readStringUntil('\n'));
  }
}
