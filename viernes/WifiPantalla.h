#include <TFT_eSPI.h> // Librería para la pantalla integrada de la LilyGO T-Display-S3
#include <Wire.h>
#include <WiFi.h>

// =================================================
// PINES LILYGO T-DISPLAY-S3 TOUCH
// =================================================
#define LCD_PWR    15  // Encendido de pantalla
#define LCD_BL     38  // Luz de fondo (Backlight)

// Pines I2C del panel táctil integrado en LilyGO T-Display-S3 Touch (CST816S)
#define TOUCH_SDA  18
#define TOUCH_SCL  17
#define TOUCH_INT  16
#define TOUCH_RST  21
#define CST816_ADDR 0x15

TFT_eSPI tft = TFT_eSPI();

// =================================================
// COLORES
// =================================================
#define FONDO       TFT_BLACK
#define BLANCO      TFT_WHITE
#define VERDE       TFT_GREEN
#define ROJO        TFT_RED
#define AZUL        TFT_BLUE
#define GRIS        0x7BEF
#define GRIS_OSCURO 0x3186
#define AMARILLO    TFT_YELLOW
#define CIAN        TFT_CYAN

// =================================================
// CREDENCIALES PREDEFINIDAS
// =================================================
struct CredencialConocida {
  const char* ssid;
  const char* password; // "" si no tiene contraseña
};

CredencialConocida redesConocidas[] = {};
const int NUM_CONOCIDAS = sizeof(redesConocidas) / sizeof(redesConocidas[0]);

// =================================================
// VARIABLES DE ESTADO
// =================================================
enum EstadoRobot {
  MODO_CARA,          // Viendo la cara normal
  MODO_LISTA_WIFI,    // Viendo Red 1, Red 2, Red 3...
  MODO_TECLADO,       // Teclado táctil para escribir contraseña
  MODO_CONECTANDO     // Conectando al Wi-Fi
};

EstadoRobot estadoActual = MODO_CARA;
uint16_t colorWiFi = GRIS;

// Zona táctil del icono Wi-Fi (Arriba a la derecha)
const int WIFI_X = 290; 
const int WIFI_Y = 18;
const int WIFI_RADIO_TOCABLE = 40;

// Variables para la lista de redes
String redesSSID[16];
bool redEsAbierta[16];
int numRedes = 0;
int paginaRedes = 0; // Para cambiar de página si hay más de 4 redes
const int MAX_REDES_MOSTRAR = 4;
const int ALTO_FILA = 34;
const int INICIO_Y_LISTA = 32;

// Variables para la conexión y el teclado táctil
String ssidSeleccionado = "";
String passwordIngresada = "";
unsigned long tiempoInicioConexion = 0;

// Mapas del teclado táctil (4 filas x 10 columnas = 40 botones)
const char* mapaMinusculas[40] = {
  "1","2","3","4","5","6","7","8","9","0",
  "q","w","e","r","t","y","u","i","o","p",
  "a","s","d","f","g","h","j","k","l","@",
  "z","x","c","v","b","n","m","MAY","DEL","OK"
};

const char* mapaMayusculas[40] = {
  "!","#","$","%","&","/","(",")","=","?",
  "Q","W","E","R","T","Y","U","I","O","P",
  "A","S","D","F","G","H","J","K","L","_",
  "Z","X","C","V","B","N","M","MAY","DEL","OK"
};

const char* mapaSimbolos[40] = {
  "-","+","*",".",",",":",";","'","\"","\\",
  "[","]","{","}","<",">","|","~","^","`",
  "1","2","3","4","5","6","7","8","9","0",
  "@","_","-",".","/","SPC","SPC","MAY","DEL","OK"
};

int capaTeclado = 0; // 0 = minúsculas, 1 = MAYÚSCULAS, 2 = símbolos

// =================================================
// DRIVER TÁCTIL INTEGRADO (CST816S para LilyGO S3)
// =================================================

void iniciarTouch() {
  pinMode(TOUCH_RST, OUTPUT);
  pinMode(TOUCH_INT, INPUT_PULLUP);
  
  // Secuencia de reset del chip táctil CST816S
  digitalWrite(TOUCH_RST, LOW);
  delay(20);
  digitalWrite(TOUCH_RST, HIGH);
  delay(50);

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
}

// Lee el toque y lo convierte automáticamente a coordenadas horizontales (320x170)
bool leerToquePantalla(int &x, int &y) {
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0x02); // Registro de número de puntos tocados
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom(CST816_ADDR, 5) != 5) return false;

  uint8_t dedos = Wire.read() & 0x0F;
  uint8_t xH = Wire.read();
  uint8_t xL = Wire.read();
  uint8_t yH = Wire.read();
  uint8_t yL = Wire.read();

  if (dedos == 0) return false;

  // Coordenadas físicas nativas en vertical (170x320)
  uint16_t rawX = ((xH & 0x0F) << 8) | xL;
  uint16_t rawY = ((yH & 0x0F) << 8) | yL;

  // Mapear a rotación 1 (Horizontal: 320 de ancho x 170 de alto)
  x = rawY;
  y = 170 - rawX;

  // Limitar dentro de los bordes de la pantalla
  if (x < 0) x = 0;
  if (x > 319) x = 319;
  if (y < 0) y = 0;
  if (y > 169) y = 169;

  return true;
}

// =================================================
// DIBUJOS DE LA CARA E INTERFAZ
// =================================================

void dibujarIconoWiFi(uint16_t color) {
  int cx = WIFI_X;
  int cy = WIFI_Y;

  tft.fillCircle(cx, cy + 12, 2, color);

  for (int r = 7; r <= 21; r += 7) {
    for (int a = -45; a <= 45; a += 3) {
      float rad = a * 0.0174533f;
      int x = cx + (int)(sin(rad) * r);
      int y = (cy + 12) - (int)(cos(rad) * r);
      tft.drawPixel(x, y, color);
      tft.drawPixel(x + 1, y, color);
      tft.drawPixel(x, y + 1, color);
    }
  }
}

void dibujarCaraNormal() {
  tft.fillScreen(FONDO);
  
  // Ojos
  tft.fillRoundRect(60, 40, 60, 100, 20, BLANCO);
  tft.fillRoundRect(200, 40, 60, 100, 20, BLANCO);
  
  // Sonrisa
  tft.drawCircle(160, 115, 15, BLANCO);
  tft.fillRect(145, 100, 30, 15, FONDO);
  
  // Icono Wi-Fi arriba a la derecha
  dibujarIconoWiFi(colorWiFi);
}

void textoCentrado(const String &texto, int y) {
  tft.setTextDatum(TC_DATUM);
  tft.drawString(texto, tft.width() / 2, y);
  tft.setTextDatum(TL_DATUM);
}

// =================================================
// LISTA DE REDES TÁCTIL (Red 1, Red 2, Red 3...)
// =================================================

bool buscarCredencialGuardada(const String &ssid, String &passOut) {
  for (int i = 0; i < NUM_CONOCIDAS; i++) {
    if (ssid.equalsIgnoreCase(redesConocidas[i].ssid)) {
      passOut = String(redesConocidas[i].password);
      return true;
    }
  }
  return false;
}

void mostrarListaRedes() {
  tft.fillScreen(FONDO);

  // Botón táctil "<" arriba a la izquierda para volver a la cara
  tft.fillRoundRect(4, 2, 36, 24, 4, GRIS_OSCURO);
  tft.setTextColor(BLANCO, GRIS_OSCURO);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("<", 22, 14);

  // Título centrado
  tft.setTextColor(CIAN, FONDO);
  tft.drawString("Toca una red", 160, 14);

  // Botón táctil "+" arriba a la derecha para ver más redes si hay más de 4
  if (numRedes > MAX_REDES_MOSTRAR) {
    tft.fillRoundRect(280, 2, 36, 24, 4, GRIS_OSCURO);
    tft.setTextColor(AMARILLO, GRIS_OSCURO);
    tft.drawString("v", 298, 14);
  }
  tft.setTextDatum(TL_DATUM);

  if (numRedes == 0) {
    tft.setTextColor(BLANCO, FONDO);
    textoCentrado("No se encontraron redes", 80);
    return;
  }

  int inicio = paginaRedes * MAX_REDES_MOSTRAR;

  for (int i = 0; i < MAX_REDES_MOSTRAR; i++) {
    int idx = inicio + i;
    if (idx >= numRedes) break;

    int y = INICIO_Y_LISTA + (i * ALTO_FILA);

    // Recuadro táctil de cada fila
    tft.fillRoundRect(4, y, 312, ALTO_FILA - 4, 6, GRIS_OSCURO);

    // Nombre: "Red 1: wl-fmat-ccei"
    String textoRed = "Red " + String(idx + 1) + ": " + redesSSID[idx];
    if (textoRed.length() > 19) {
      textoRed = textoRed.substring(0, 19);
    }

    tft.setTextSize(2);
    tft.setTextColor(BLANCO, GRIS_OSCURO);
    tft.setCursor(10, y + 7);
    tft.print(textoRed);

    // Etiqueta a la derecha: [Abierta], [Guardada] o [Clave]
    String passTemp = "";
    String estadoTag = "";
    uint16_t colorTag = AMARILLO;

    if (redEsAbierta[idx]) {
      estadoTag = "[Abierta]";
      colorTag = VERDE;
    } else if (buscarCredencialGuardada(redesSSID[idx], passTemp)) {
      estadoTag = "[Guardada]";
      colorTag = CIAN;
    } else {
      estadoTag = "[Clave]";
      colorTag = AMARILLO;
    }

    tft.setTextSize(1);
    tft.setTextColor(colorTag, GRIS_OSCURO);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(estadoTag, 310, y + 11);
    tft.setTextDatum(TL_DATUM);
  }
}

void escanearYMostrarRedes() {
  tft.fillScreen(FONDO);
  tft.setTextColor(BLANCO, FONDO);
  tft.setTextSize(2);
  textoCentrado("Buscando redes...", 75);

  int n = WiFi.scanNetworks();
  numRedes = 0;
  paginaRedes = 0;

  for (int i = 0; i < n && numRedes < 16; i++) {
    redesSSID[numRedes] = WiFi.SSID(i);
    redEsAbierta[numRedes] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    numRedes++;
  }

  mostrarListaRedes();
}

// =================================================
// CONEXIÓN WI-FI
// =================================================

void iniciarConexion(const String &ssid, const String &pass) {
  estadoActual = MODO_CONECTANDO;
  tft.fillScreen(FONDO);
  tft.setTextSize(2);
  tft.setTextColor(CIAN, FONDO);
  textoCentrado("Conectando a:", 35);
  tft.setTextColor(BLANCO, FONDO);
  textoCentrado(ssid.substring(0, 22), 70);

  if (pass.length() == 0) {
    textoCentrado("(Sin Contrasena)", 105);
    WiFi.begin(ssid.c_str());
  } else {
    textoCentrado("(Verificando clave)", 105);
    WiFi.begin(ssid.c_str(), pass.c_str());
  }

  tiempoInicioConexion = millis();
}

// =================================================
// TECLADO TÁCTIL EN PANTALLA
// =================================================

const char* obtenerTeclaActual(int index) {
  if (capaTeclado == 0) return mapaMinusculas[index];
  if (capaTeclado == 1) return mapaMayusculas[index];
  return mapaSimbolos[index];
}

void actualizarCajaPassword() {
  tft.fillRect(7, 17, 264, 22, FONDO);
  tft.drawRect(6, 16, 266, 24, BLANCO);
  tft.setTextSize(2);
  tft.setTextColor(AMARILLO, FONDO);
  tft.setCursor(12, 20);

  String passVisible = passwordIngresada + "_";
  if (passVisible.length() > 20) {
    passVisible = passVisible.substring(passVisible.length() - 20);
  }
  tft.print(passVisible);
}

void dibujarTeclado() {
  tft.fillScreen(FONDO);

  // Encabezado con el nombre de la red seleccionada
  tft.setTextSize(1);
  tft.setTextColor(CIAN, FONDO);
  tft.setCursor(6, 4);
  tft.print("Clave para: " + ssidSeleccionado.substring(0, 32));

  // Caja de contraseña + Botón táctil ESC para cancelar
  actualizarCajaPassword();
  tft.fillRoundRect(276, 16, 38, 24, 4, ROJO);
  tft.setTextSize(1);
  tft.setTextColor(BLANCO, ROJO);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("ESC", 295, 28);

  // Dibujar cuadrícula táctil (4 filas x 10 columnas)
  int startX = 5;
  int startY = 45;
  int anchoTecla = 31;
  int altoTecla = 30;

  for (int i = 0; i < 40; i++) {
    int fila = i / 10;
    int col  = i % 10;
    int x = startX + (col * anchoTecla);
    int y = startY + (fila * altoTecla);

    const char* Etiqueta = obtenerTeclaActual(i);

    uint16_t colorFondoTecla = GRIS_OSCURO;
    uint16_t colorTextoTecla = BLANCO;

    if (String(Etiqueta) == "OK") {
      colorFondoTecla = VERDE;
      colorTextoTecla = FONDO;
    } else if (String(Etiqueta) == "DEL") {
      colorFondoTecla = ROJO;
      colorTextoTecla = BLANCO;
    } else if (String(Etiqueta) == "MAY") {
      colorFondoTecla = AZUL;
      colorTextoTecla = BLANCO;
    }

    tft.fillRoundRect(x, y, anchoTecla - 2, altoTecla - 2, 4, colorFondoTecla);
    tft.setTextColor(colorTextoTecla, colorFondoTecla);

    if (strlen(Etiqueta) > 1) {
      tft.setTextSize(1);
    } else {
      tft.setTextSize(2);
    }

    tft.setTextDatum(MC_DATUM);
    tft.drawString(Etiqueta, x + (anchoTecla - 2) / 2, y + (altoTecla - 2) / 2);
  }
  tft.setTextDatum(TL_DATUM);
}

// =================================================
// PROCESAMIENTO DE TOQUES EN PANTALLA
// =================================================

void procesarToque(int x, int y) {
  // 1. MODO CARA -> Tocar el icono Wi-Fi arriba a la derecha
  if (estadoActual == MODO_CARA) {
    if (x > WIFI_X - WIFI_RADIO_TOCABLE && x < WIFI_X + WIFI_RADIO_TOCABLE &&
        y > WIFI_Y - WIFI_RADIO_TOCABLE && y < WIFI_Y + WIFI_RADIO_TOCABLE) {
      estadoActual = MODO_LISTA_WIFI;
      escanearYMostrarRedes();
    }
  }
  // 2. MODO LISTA DE REDES -> Tocar una red o los botones superiores
  else if (estadoActual == MODO_LISTA_WIFI) {
    // Botón "<" (Arriba a la izquierda) -> Volver a la cara
    if (x < 50 && y < 30) {
      estadoActual = MODO_CARA;
      dibujarCaraNormal();
      return;
    }

    // Botón "v" (Arriba a la derecha) -> Siguiente página de redes
    if (x > 270 && y < 30 && numRedes > MAX_REDES_MOSTRAR) {
      paginaRedes++;
      if (paginaRedes * MAX_REDES_MOSTRAR >= numRedes) {
        paginaRedes = 0;
      }
      mostrarListaRedes();
      return;
    }

    // Revisar cuál de las 4 filas de redes se tocó
    int inicio = paginaRedes * MAX_REDES_MOSTRAR;
    for (int i = 0; i < MAX_REDES_MOSTRAR; i++) {
      int idx = inicio + i;
      if (idx >= numRedes) break;

      int yFila = INICIO_Y_LISTA + (i * ALTO_FILA);
      if (y >= yFila && y <= yFila + ALTO_FILA) {
        ssidSeleccionado = redesSSID[idx];
        String passGuardada = "";

        // Si es abierta (ej. wl-fmat-ccei), conecta directo
        if (redEsAbierta[idx]) {
          iniciarConexion(ssidSeleccionado, "");
        }
        // Si está en nuestras credenciales conocidas, conecta directo
        else if (buscarCredencialGuardada(ssidSeleccionado, passGuardada)) {
          iniciarConexion(ssidSeleccionado, passGuardada);
        }
        // Si tiene clave y no la conocemos, abre el teclado táctil
        else {
          passwordIngresada = "";
          capaTeclado = 0;
          estadoActual = MODO_TECLADO;
          dibujarTeclado();
        }
        return;
      }
    }
  }
  // 3. MODO TECLADO TÁCTIL -> Tocar letras, DEL, MAY, ESC u OK
  else if (estadoActual == MODO_TECLADO) {
    // Botón ESC (volver a la lista de redes)
    if (x >= 272 && y >= 12 && y < 44) {
      estadoActual = MODO_LISTA_WIFI;
      mostrarListaRedes();
      return;
    }

    // Verificar si el toque cayó dentro de la cuadrícula de teclas (Y >= 45)
    int startX = 5;
    int startY = 45;
    int anchoTecla = 31;
    int altoTecla = 30;

    if (y >= startY && y < startY + (4 * altoTecla) && x >= startX && x < startX + (10 * anchoTecla)) {
      int col  = (x - startX) / anchoTecla;
      int fila = (y - startY) / altoTecla;
      int idxTecla = (fila * 10) + col;

      if (idxTecla >= 0 && idxTecla < 40) {
        String tecla = String(obtenerTeclaActual(idxTecla));

        if (tecla == "MAY") {
          capaTeclado = (capaTeclado + 1) % 3;
          dibujarTeclado();
        } 
        else if (tecla == "DEL") {
          if (passwordIngresada.length() > 0) {
            passwordIngresada.remove(passwordIngresada.length() - 1);
            actualizarCajaPassword();
          }
        } 
        else if (tecla == "SPC") {
          passwordIngresada += " ";
          actualizarCajaPassword();
        } 
        else if (tecla == "OK") {
          iniciarConexion(ssidSeleccionado, passwordIngresada);
        } 
        else {
          passwordIngresada += tecla;
          actualizarCajaPassword();
        }
      }
    }
  }
}

// También puedes seguir pegando la contraseña por el Monitor Serial si lo deseas
void revisarSerialWiFi() {
  if (estadoActual == MODO_TECLADO && Serial.available() > 0) {
    String passSerial = Serial.readStringUntil('\n');
    passSerial.trim();
    if (passSerial.length() > 0) {
      iniciarConexion(ssidSeleccionado, passSerial);
    }
  }
}

void gestionarConexion() {
  if (estadoActual == MODO_CONECTANDO) {
    if (WiFi.status() == WL_CONNECTED) {
      colorWiFi = VERDE;
      estadoActual = MODO_CARA;
      dibujarCaraNormal();
      
      Serial.println("\n>>> ¡CONEXION EXITOSA! <<<");
      Serial.print("IP asignada: "); Serial.println(WiFi.localIP());
    } 
    else if (millis() - tiempoInicioConexion > 15000) {
      colorWiFi = ROJO;
      estadoActual = MODO_CARA;
      WiFi.disconnect();
      dibujarCaraNormal();
      
      Serial.println("\n>>> ¡ERROR DE CONEXION! <<<");
      Serial.println("Clave incorrecta o red fuera de alcance.");
    }
  }
}

// =================================================
// SETUP Y LOOP
// =================================================

void configurarPantallaYRed() {
  delay(500);
  Serial.begin(115200);

  // Encender energía de la pantalla en la LilyGO T-Display-S3
  pinMode(LCD_PWR, OUTPUT);
  digitalWrite(LCD_PWR, HIGH);

  // Iniciar pantalla TFT_eSPI
  tft.init();
  tft.setRotation(1); // Horizontal (320x170)
  tft.fillScreen(FONDO);

  // Luz de fondo
  pinMode(LCD_BL, OUTPUT);
  analogWrite(LCD_BL, 200);

  // Iniciar el panel táctil integrado
  iniciarTouch();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  colorWiFi = GRIS;
  dibujarCaraNormal();
  
  Serial.println("Viernes Touch encendida. Toca el icono Wi-Fi en la esquina superior derecha.");
}
