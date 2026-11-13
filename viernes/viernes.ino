#include "Credenciales.h"
#include "WifiPantalla.h"
#include "DetectorVoz.h"

// Variables globales para guardar la respuesta del servidor
String ultimoStatus = "";
String ultimoTexto  = "";
String ultimoError  = "";

void ejecutarComandosViernes(String texto) {
  texto.toLowerCase(); // esto es para convertir a minusculas y sea mas fácil
  tft.fillScreen(FONDO); /*limpiar pantalla*/

  // Primer comando - Prender LED
  if (texto.indexOf("prende") >= 0 && texto.indexOf("led") >= 0) {
    // Dibujo del LED encendido
    mostrarLedVirtual(true);

    delay(3000);
  }
  // segundo comando - Apagar LED
  else if (texto.indexOf("apaga") >= 0 && texto.indexOf("led") >= 0) {
    // Dibujo del LED apagado
    mostrarLedVirtual(false);

    delay(3000);
  }
  // tercer comando - saludo
  else if (texto.indexOf("saluda") >= 0 || texto.indexOf("hola") >= 0) {
    // dibujarCaraNormal();
    tft.setTextColor(TFT_PINK, FONDO);
    imprimirTextoPantalla("¡Hola! Soy Viernes", false);

    delay(3000);
  }
  
  // cuarto comando - Hora (Mérida, Yucatán)
  else if ((texto.indexOf("tiempo") >= 0) || (texto.indexOf("hora") >= 0)) {
    HTTPClient http;
    
    http.begin(DEFAULT_SERVER_URL "/time.php");
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      String horaActual = http.getString(); // Devuelve directamente la hora, ej: "11:09"
      
      Serial.println(horaActual);
      imprimirTextoPantalla(horaActual, false);
    } else {
      Serial.print("Error HTTP: ");
      Serial.println(httpCode);
    }
    http.end();

    delay(3000);
  }
  
  // quinto comando - Clima
  else if ((texto.indexOf("clima") >= 0) || (texto.indexOf("Clima") >= 0)) {
    HTTPClient http;
    http.begin("https://api.open-meteo.com/v1/forecast?latitude=20.9753&longitude=-89.6169&current_weather=true");
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();

      // Buscamos primero el bloque exacto del clima actual para evitar confundirlo con las unidades
      int currentWeatherPos = payload.indexOf("\"current_weather\":{");
      
      // A partir de ese bloque, buscamos la temperatura real
      int tempIndex = payload.indexOf("\"temperature\":", currentWeatherPos) + 14;
      int commaIndex = payload.indexOf(",", tempIndex);
      
      String temperatura = payload.substring(tempIndex, commaIndex) + " C";
      
      tft.setTextSize(3);
      tft.setTextColor(TFT_YELLOW, FONDO);
      textoCentrado("Clima en Merida:", 50);
      tft.setTextSize(4);
      textoCentrado(temperatura, 90);
      Serial.print("Temperatura: ");
      Serial.println(temperatura);
    }
    http.end();

    delay(3000);
  }
  
  // Si no reconoce el comando
  else {
    imprimirTextoPantalla("Comando no reconocido", false);
    delay(3000);
  }
}

void setup() {
    Serial.begin(115200);

    // Inicializar pantalla táctil y red Wi-Fi
    configurarPantallaYRed();

    // Inicializar el micrófono y el modelo de Edge Impulse
    if (!iniciarDetectorVoz()) {
        Serial.println("Error al iniciar el detector de voz");
        while (1) {
            delay(100);
        }
    }
}

void loop() {
    int tx, ty;
    if (leerToquePantalla(tx, ty)) {
        procesarToque(tx, ty);
        delay(260); // Anti-rebote para evitar dobles toques accidentales
    }

    revisarSerialWiFi();
    gestionarConexion();

    // Solo escuchar el Wake Word cuando está en la cara principal
    // para no hacer lento el teclado táctil al configurar el Wi-Fi
    if (estadoActual != MODO_CARA) {
        delay(10);
        return;
    }

    if (escucharWakeWord("Wake", 0.75)) {
        Serial.println("Escuchando y Grabando 5 segundos....");

        // Recibimos el struct con toda la información desacoplada
        RespuestaTranscripcion respuesta = grabarYEnviar(mostrarBarraProgresoGrabacion, DEFAULT_SERVER_URL "/upload.php");

        // Guardamos la respuesta en los strings globales
        ultimoStatus = respuesta.status;
        ultimoTexto  = respuesta.text;
        ultimoError  = respuesta.error;

        if (respuesta.exito) {
            Serial.println("=== TRANSCRIPCION EXITOSA ===");
            Serial.println("Status: " + ultimoStatus);
            Serial.println("Texto : " + ultimoTexto);
            Serial.println("=============================");

            ejecutarComandosViernes(ultimoTexto);
        } else {
            Serial.println("=== ERROR EN EL PROCESO ===");
            Serial.println("Status: " + ultimoStatus);
            Serial.println("Error : " + ultimoError);
            Serial.println("Codigo HTTP: " + String(respuesta.codigoHttp));
            Serial.println("===========================");
        }

        dibujarCaraNormal();

    } else {
        Serial.println("Escuchando....");
    }
}