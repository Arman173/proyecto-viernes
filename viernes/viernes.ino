#include "WifiPantalla.h"
#include "DetectorVoz.h"

void setup() {
    Serial.begin(115200);

    // Inicializas tus demás sensores, pantallas o WiFi aquí...
    configurarPantallaYRed();

    // Inicializas el micrófono y el modelo
    if (!iniciarDetectorVoz()) {
        // mostrarMensajeEstado("Error", "Error al iniciar el detector de voz", TFT_RED);
        Serial.println("Error al iniciar el detector de voz");
        while (1);
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

    // Cambia "Wake" por el nombre exacto de la etiqueta que le pusiste en Edge Impulse
    if (escucharWakeWord("Wake", 0.80)) {
        // mostrarMensajeEstado("Wake Word", "Grabando 5 segundos...", TFT_GREEN);
        Serial.println("Escuchando y Grabando 5 segundos....");

        if (grabarYEnviar()) {
            // mostrarMensajeEstado("Exito", "Audio enviado al servidor!", TFT_GREEN);
            Serial.println("Audio de 5s enviado correctamente.");
        } else {
            // mostrarMensajeEstado("Error", "Fallo al enviar audio", TFT_RED);
            Serial.println("Error al subir el audio.");
        }
    } else {
        // mostrarMensajeEstado("Escuchando", "Detectando audio...", TFT_BLUE);
        Serial.println("Escuchando....");
    }
}