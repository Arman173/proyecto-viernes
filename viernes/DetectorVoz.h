#ifndef DETECTOR_VOZ_H
#define DETECTOR_VOZ_H

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include "Credenciales.h"

// Biblioteca de viernes de edge pulse
#include <Viernes_inferencing.h>

// CONFIGURACIÓN I2S Y GRABACIÓN
#define I2S_WS   2
#define I2S_SCK  1
#define I2S_SD   3
#define I2S_PORT I2S_NUM_0

// Duración de grabación después de detectar el Wake Word
#define RECORD_SECONDS   3
#define WAV_HEADER_SIZE  44
#define AUDIO_GAIN       8   // Ganancia por software para el INMP441

// TIPOS Y ESTRUCTURAS DESACOPLADAS

// Tipo de callback para reportar el progreso de grabación sin acoplar la pantalla a este archivo
typedef void (*CallbackProgresoGrabacion)(int porcentaje, float segundosTranscurridos, int segundosTotales);

struct RespuestaTranscripcion {
    bool   exito;       // true si status == "success"
    int    codigoHttp;  // Código HTTP devuelto por el servidor
    String status;      // "success" o "error"
    String text;        // Texto transcrito del audio (ej. "hola viernes")
    String error;       // Descripción del error en caso de fallo
    String rawJson;     // JSON crudo recibido (para depuración si se requiere)
};

// Buffers internos del módulo de audio
static int16_t *audio_buffer = nullptr;
static uint8_t *wavBuffer    = nullptr;
static size_t   wavDataSize  = 0;

// FUNCIONES INTERNAS (PRIVADAS AL MÓDULO)

// Callback interno que pide Edge Impulse
static int obtener_datos_audio(size_t offset, size_t length, float *out_ptr) {
    numpy::int16_to_float(&audio_buffer[offset], out_ptr, length);
    return 0;
}

// Genera los 44 bytes de cabecera estándar RIFF/WAV (PCM 16-bit Mono)
static void crearCabeceraWAV(uint8_t* header, uint32_t dataSize, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample) {
    uint32_t fileSize    = dataSize + 36;
    uint32_t byteRate    = sampleRate * channels * (bitsPerSample / 8);
    uint16_t blockAlign  = channels * (bitsPerSample / 8);
    uint32_t fmtSize     = 16;
    uint16_t audioFormat = 1; // 1 = PCM

    memcpy(header,      "RIFF", 4);
    memcpy(header + 4,  &fileSize, 4);
    memcpy(header + 8,  "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    memcpy(header + 16, &fmtSize, 4);
    memcpy(header + 20, &audioFormat, 2);
    memcpy(header + 22, &channels, 2);
    memcpy(header + 24, &sampleRate, 4);
    memcpy(header + 28, &byteRate, 4);
    memcpy(header + 32, &blockAlign, 2);
    memcpy(header + 34, &bitsPerSample, 2);
    memcpy(header + 36, "data", 4);
    memcpy(header + 40, &dataSize, 4);
}

// Extrae el valor de una clave string en un JSON simple sin requerir librerías externas
static String extraerCampoJson(const String& json, const char* clave) {
    String patron = "\"" + String(clave) + "\"";
    int posClave = json.indexOf(patron);
    if (posClave == -1) return "";

    int posDosPuntos = json.indexOf(':', posClave + patron.length());
    if (posDosPuntos == -1) return "";

    int posComillaInicio = json.indexOf('"', posDosPuntos + 1);
    if (posComillaInicio == -1) return "";

    String resultado = "";
    bool escapado = false;

    for (unsigned int i = posComillaInicio + 1; i < json.length(); i++) {
        char c = json[i];
        if (escapado) {
            if (c == 'n') resultado += '\n';
            else if (c == 'r') resultado += '\r';
            else if (c == 't') resultado += '\t';
            else resultado += c;
            escapado = false;
        } else if (c == '\\') {
            escapado = true;
        } else if (c == '"') {
            break; // Fin del valor string
        } else {
            resultado += c;
        }
    }
    return resultado;
}

// Convierte el cuerpo HTTP recibido en una estructura RespuestaTranscripcion limpia
static RespuestaTranscripcion parsearRespuestaServidor(int httpCode, const String& body) {
    RespuestaTranscripcion resp;
    resp.codigoHttp = httpCode;
    resp.rawJson    = body;
    resp.status     = extraerCampoJson(body, "status");
    resp.text       = extraerCampoJson(body, "text");
    resp.error      = extraerCampoJson(body, "error");

    if (resp.status == "success") {
        resp.exito = true;
    } else {
        resp.exito = false;
        if (resp.status.length() == 0) {
            resp.status = "error";
        }
        if (resp.error.length() == 0) {
            if (httpCode <= 0) {
                resp.error = "Fallo de conexion HTTP (codigo " + String(httpCode) + ")";
            } else {
                resp.error = "Respuesta invalida del servidor (HTTP " + String(httpCode) + "): " + body;
            }
        }
    }
    return resp;
}

// FUNCIONES PÚBLICAS DESACOPLADAS

// Inicializa I2S y reserva los buffers en memoria
bool iniciarDetectorVoz() {
    audio_buffer = (int16_t *)malloc(EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t));
    if (!audio_buffer) {
        Serial.println("Error: No se pudo reservar memoria para audio_buffer");
        return false;
    }

    wavDataSize = EI_CLASSIFIER_FREQUENCY * RECORD_SECONDS * sizeof(int16_t);
    wavBuffer = (uint8_t *)ps_malloc(WAV_HEADER_SIZE + wavDataSize);
    if (!wavBuffer) {
        wavBuffer = (uint8_t *)malloc(WAV_HEADER_SIZE + wavDataSize);
    }
    if (!wavBuffer) {
        Serial.println("Error: No hay suficiente PSRAM/RAM para el buffer WAV de 5s. Activa OPI PSRAM en Tools.");
        return false;
    }

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = EI_CLASSIFIER_FREQUENCY,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD
    };

    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
    return true;
}

// Escucha un fragmento y evalúa si se pronunció el Wake Word
bool escucharWakeWord(const char* palabra_objetivo, float umbral = 0.80f) {
    size_t bytes_leidos = 0;
    size_t bytes_totales = EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t);

    i2s_read(I2S_PORT, (void *)audio_buffer, bytes_totales, &bytes_leidos, portMAX_DELAY);

    for (uint32_t i = 0; i < EI_CLASSIFIER_RAW_SAMPLE_COUNT; i++) {
        int32_t muestra = (int32_t)audio_buffer[i] * AUDIO_GAIN;
        if (muestra > 32767) muestra = 32767;
        if (muestra < -32768) muestra = -32768;
        audio_buffer[i] = (int16_t)muestra;
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &obtener_datos_audio;

    ei_impulse_result_t result = { 0 };
    if (run_classifier(&signal, &result, false) != EI_IMPULSE_OK) {
        return false;
    }

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (strcmp(result.classification[ix].label, palabra_objetivo) == 0) {
            if (result.classification[ix].value >= umbral) {
                return true;
            }
        }
    }
    return false;
}

// Captura los segundos de RECORD_SECONDS de audio desde I2S y notifica el avance mediante callback opcional
bool capturarAudioWAV(CallbackProgresoGrabacion onProgreso = nullptr) {
    if (!wavBuffer) return false;

    crearCabeceraWAV(wavBuffer, wavDataSize, EI_CLASSIFIER_FREQUENCY, 1, 16);
    i2s_zero_dma_buffer(I2S_PORT);

    // Notificar inicio (0%) para que la interfaz prepare la barra si hay callback conectado
    if (onProgreso != nullptr) {
        onProgreso(0, 0.0f, RECORD_SECONDS);
    }

    size_t bytesLeidos = 0;
    size_t totalLeidos = 0;
    int ultimoPorcentaje = 0;
    int16_t* pcmDest = (int16_t*)(wavBuffer + WAV_HEADER_SIZE);

    while (totalLeidos < wavDataSize) {
        size_t bytesPorLeer = wavDataSize - totalLeidos;
        if (bytesPorLeer > 1024) {
            bytesPorLeer = 1024;
        }

        i2s_read(I2S_PORT, (void*)((uint8_t*)pcmDest + totalLeidos), bytesPorLeer, &bytesLeidos, portMAX_DELAY);

        if (bytesLeidos > 0) {
            size_t indiceInicio   = totalLeidos / sizeof(int16_t);
            size_t muestrasLeidas = bytesLeidos / sizeof(int16_t);

            for (size_t i = 0; i < muestrasLeidas; i++) {
                int32_t muestra = (int32_t)pcmDest[indiceInicio + i] * AUDIO_GAIN;
                if (muestra > 32767) muestra = 32767;
                if (muestra < -32768) muestra = -32768;
                pcmDest[indiceInicio + i] = (int16_t)muestra;
            }

            totalLeidos += bytesLeidos;

            // Actualizar callback cada incremento de 2% para no bloquear la lectura DMA del micrófono
            if (onProgreso != nullptr) {
                int porcentajeActual = (int)((totalLeidos * 100) / wavDataSize);
                if (porcentajeActual >= ultimoPorcentaje + 2 || porcentajeActual == 100) {
                    ultimoPorcentaje = porcentajeActual;
                    float segundos = ((float)totalLeidos / (float)wavDataSize) * RECORD_SECONDS;
                    onProgreso(porcentajeActual, segundos, RECORD_SECONDS);
                }
            }
        }
    }

    i2s_zero_dma_buffer(I2S_PORT);
    return true;
}

// Envía el buffer WAV al servidor PHP y devuelve la estructura RespuestaTranscripcion
RespuestaTranscripcion enviarWAV(const char* serverUrl) {

    imprimirTextoPantalla("Procesando solicitud...");
    RespuestaTranscripcion fallo;
    fallo.exito      = false;
    fallo.codigoHttp = -1;
    fallo.status     = "error";
    fallo.text       = "";

    if (WiFi.status() != WL_CONNECTED) {
        fallo.error = "WiFi desconectado, no se puede enviar el audio.";
        return fallo;
    }

    WiFiClient client;
    HTTPClient http;
    if (!http.begin(client, serverUrl)) {
        fallo.error = "No se pudo iniciar la conexion HTTP hacia el servidor.";
        return fallo;
    }

    const String boundary = "----ESP32Boundary7d8f3a2b1c";
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http.setTimeout(30000);

    String head = "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"audio\"; filename=\"audio.wav\"\r\n";
    head += "Content-Type: audio/wav\r\n\r\n";

    String tail = "\r\n--" + boundary + "--\r\n";

    size_t totalSize = head.length() + WAV_HEADER_SIZE + wavDataSize + tail.length();

    uint8_t* payload = (uint8_t*)ps_malloc(totalSize);
    if (!payload) {
        payload = (uint8_t*)malloc(totalSize);
    }
    if (!payload) {
        http.end();
        fallo.error = "Sin memoria PSRAM/RAM para armar el payload HTTP.";
        return fallo;
    }

    size_t offset = 0;
    memcpy(payload + offset, head.c_str(), head.length());
    offset += head.length();
    memcpy(payload + offset, wavBuffer, WAV_HEADER_SIZE + wavDataSize);
    offset += WAV_HEADER_SIZE + wavDataSize;
    memcpy(payload + offset, tail.c_str(), tail.length());

    int httpResponseCode = http.POST(payload, totalSize);
    String responseBody  = (httpResponseCode > 0) ? http.getString() : http.errorToString(httpResponseCode);

    http.end();
    free(payload);

    return parsearRespuestaServidor(httpResponseCode, responseBody);
}

// Orquesta la grabación de los x segundos (con callback opcional de barra de progreso) y el envío al servidor
RespuestaTranscripcion grabarYEnviar(CallbackProgresoGrabacion onProgreso = nullptr, const char* serverUrl = DEFAULT_SERVER_URL) {
    if (!capturarAudioWAV(onProgreso)) {
        RespuestaTranscripcion err;
        err.exito      = false;
        err.codigoHttp = -1;
        err.status     = "error";
        err.text       = "";
        err.error      = "Buffer WAV no inicializado. Llama a iniciarDetectorVoz() en setup().";
        return err;
    }
    return enviarWAV(serverUrl);
}

#endif