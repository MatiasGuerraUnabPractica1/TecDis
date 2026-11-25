/*
 * Firmware para la ESP32-CAM (AI Thinker) con el FLASH siempre encendido.
 *
 * - Se conecta al WiFi y muestra la cámara en  http://IP/
 * - Entrega una foto JPEG en                   http://IP/foto
 *   (la misma dirección que usa clasificador_colores.py)
 * - Enciende el flash (LED blanco, GPIO 4) al arrancar y lo deja prendido.
 * - Permite cambiar la intensidad del flash:   http://IP/flash?nivel=0..255
 *
 * Arduino IDE:
 *   Placa  -> esp32 -> "AI Thinker ESP32-CAM"
 *   Se programa con la placa ESP32-CAM-MB (USB) o un adaptador USB-TTL.
 */

#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"

// ---------------- Configuración ----------------
const char* WIFI_NOMBRE     = "NOMBRE_DE_LA_RED";   // red WiFi de 2,4 GHz
const char* WIFI_CONTRASENA = "CONTRASENA";

// IP fija (opcional). Si USAR_IP_FIJA es false, el router asigna la IP
// y se muestra en el Monitor Serie al arrancar.
const bool USAR_IP_FIJA = true;
IPAddress IP_FIJA(10, 227, 104, 225);
IPAddress PUERTA_ENLACE(10, 227, 104, 1);   // normalmente la IP del router
IPAddress MASCARA(255, 255, 255, 0);

// Intensidad del flash al arrancar (0 = apagado, 255 = máximo).
// Al máximo se calienta mucho y consume bastante: empieza con un valor medio.
const int FLASH_NIVEL_INICIAL = 120;
// -----------------------------------------------

// Pines de la cámara para el modelo AI Thinker
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

#define FLASH_GPIO_NUM   4    // LED blanco del flash

WebServer servidor(80);
int flashNivel = FLASH_NIVEL_INICIAL;

void ponerFlash(int nivel) {
  flashNivel = constrain(nivel, 0, 255);
  ledcWrite(FLASH_GPIO_NUM, flashNivel);
}

bool iniciarCamara() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_VGA;      // 640x480
  config.jpeg_quality = 12;                 // 10-63 (menor = mejor calidad)
  config.fb_count     = 1;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST; // siempre la foto más reciente

  if (esp_camera_init(&config) != ESP_OK) {
    return false;
  }
  return true;
}

// http://IP/foto -> una foto JPEG
void manejarFoto() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    servidor.send(500, "text/plain", "No se pudo tomar la foto");
    return;
  }
  servidor.sendHeader("Cache-Control", "no-cache");
  servidor.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// http://IP/flash?nivel=0..255 -> cambia la intensidad del flash
void manejarFlash() {
  if (servidor.hasArg("nivel")) {
    ponerFlash(servidor.arg("nivel").toInt());
  }
  servidor.send(200, "text/plain", "Flash: " + String(flashNivel));
}

// http://IP/ -> página simple que muestra la cámara
void manejarInicio() {
  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32-CAM</title></head>"
    "<body style='background:#111;color:#eee;font-family:sans-serif;text-align:center'>"
    "<h2>ESP32-CAM funcionando (flash encendido)</h2>"
    "<img id='f' src='/foto' style='max-width:100%;border-radius:8px'><br><br>"
    "<a style='color:#4af' href='/foto'>Tomar una foto</a><br><br>"
    "Flash: <a style='color:#4af' href='/flash?nivel=0'>apagado</a> | "
    "<a style='color:#4af' href='/flash?nivel=60'>bajo</a> | "
    "<a style='color:#4af' href='/flash?nivel=120'>medio</a> | "
    "<a style='color:#4af' href='/flash?nivel=255'>máximo</a>"
    "<script>setInterval(()=>{document.getElementById('f').src='/foto?t='+Date.now()},1000)</script>"
    "</body></html>";
  servidor.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  if (!iniciarCamara()) {
    Serial.println("ERROR: no se pudo iniciar la cámara. Revisa el modelo y la cinta.");
    while (true) delay(1000);
  }

  // Flash con PWM (canal 7, para no chocar con el reloj de la cámara)
  ledcAttachChannel(FLASH_GPIO_NUM, 5000, 8, 7);
  ponerFlash(FLASH_NIVEL_INICIAL);

  if (USAR_IP_FIJA) {
    WiFi.config(IP_FIJA, PUERTA_ENLACE, MASCARA, PUERTA_ENLACE);
  }
  WiFi.begin(WIFI_NOMBRE, WIFI_CONTRASENA);
  WiFi.setSleep(false);
  Serial.print("Conectando al WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Listo. Abre http://");
  Serial.println(WiFi.localIP());

  servidor.on("/", manejarInicio);
  servidor.on("/foto", manejarFoto);
  servidor.on("/flash", manejarFlash);
  servidor.begin();
}

void loop() {
  servidor.handleClient();
  // Si se corta el WiFi, se reconecta solo
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(1000);
  }
}
