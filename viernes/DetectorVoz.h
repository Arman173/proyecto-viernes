#ifndef DETECTOR_VOZ_H
#define DETECTOR_VOZ_H

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <driver/i2s.h>

// 1. PON AQUÍ EL INCLUDE QUE TE GENERÓ EDGE IMPULSE
#include <Viernes_inferencing.h>

// ================== CONFIGURACIÓN I2S Y GRABACIÓN ==================
#define I2S_WS   2
#define I2S_SCK  1
#define I2S_SD   3
#define I2S_PORT I2S_NUM_0

// Duración de grabación después de detectar el Wake Word (5 segundos)
#define RECORD_SECONDS   5
#define WAV_HEADER_SIZE  44
#define AUDIO_GAIN       8   // Ganancia por software para el INMP441

// URL por defecto de tu servidor PHP (puedes cambiarla o pasar otra por parámetro)
static const char* DEFAULT_SERVER_URL = "http://ip_computadora/carpeta_proyecto/upload.php";

// Buffers en memoria
static int16_t *audio_buffer = nullptr;
static uint8_t *wavBuffer    = nullptr;
static size_t   wavDataSize  = 0;

// ================== FUNCIONES INTERNAS ==================

// Callback interno que pide Edge Impulse
static int obtener_datos_audio(size_t offset, size_t length, float *out_ptr) {
    numpy::int16_to_float(&audio_buffer[offset], out_ptr, length);
    return 0;
}

// Genera los 44 bytes de cabecera estándar RIFF/WAV (PCM 16-bit Mono)
static void crearCabeceraWAV(uint8_t* header, uint32_t dataSize, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample) {
    uint32_t fileSize   = dataSize + 36;
    uint32_t byteRate   = sampleRate * channels * (bitsPerSample / 8);
    uint16_t blockAlign = channels * (bitsPerSample / 8);
    uint32_t fmtSize    = 16;
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

// ================== FUNCIONES PÚBLICAS ==================

// Función 1: Llamar una sola vez en tu setup()
bool iniciarDetectorVoz() {
    // 1. Reservar buffer para inferencia de Edge Impulse
    audio_buffer = (int16_t *)malloc(EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t));
    if (!audio_buffer) {
        Serial.println("Error: No se pudo reservar memoria para audio_buffer");
        return false;
    }

    // 2. Reservar buffer en PSRAM para grabar los 5 segundos en formato WAV (16-bit)
    wavDataSize = EI_CLASSIFIER_FREQUENCY * RECORD_SECONDS * sizeof(int16_t);
    wavBuffer = (uint8_t *)ps_malloc(WAV_HEADER_SIZE + wavDataSize);
    if (!wavBuffer) {
        // Fallback a RAM normal si PSRAM no está habilitada (aunque para 5s se recomienda PSRAM)
        wavBuffer = (uint8_t *)malloc(WAV_HEADER_SIZE + wavDataSize);
    }
    if (!wavBuffer) {
        Serial.println("Error: No hay suficiente PSRAM/RAM para el buffer WAV de 5s. Activa OPI PSRAM en Tools.");
        return false;
    }

    // 3. Configurar el driver I2S (unificado para Wake Word y grabación WAV)
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

// Función 2: Llamar en tu loop(). Devuelve true si detecta tu palabra clave
bool escucharWakeWord(const char* palabra_objetivo, float umbral = 0.80f) {
    size_t bytes_leidos = 0;
    size_t bytes_totales = EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t);

    i2s_read(I2S_PORT, (void *)audio_buffer, bytes_totales, &bytes_leidos, portMAX_DELAY);

    // Ganancia por software para el INMP441
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
                return true; // ¡Superó el umbral!
            }
        }
    }
    return false;
}

// Función 3: Envía el contenido de wavBuffer al servidor PHP mediante HTTP POST multipart
bool enviarWAV(const char* serverUrl = DEFAULT_SERVER_URL) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Error: WiFi desconectado, no se puede enviar el audio.");
        return false;
    }

    WiFiClient client;
    // Si tu servidor usa HTTPS, descomenta las siguientes 2 líneas y comenta WiFiClient client:
    // WiFiClientSecure client;
    // client.setInsecure();

    HTTPClient http;
    if (!http.begin(client, serverUrl)) {
        Serial.println("Error: No se pudo iniciar la conexion HTTP");
        return false;
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
        Serial.println("Error: Sin memoria PSRAM/RAM para armar el payload HTTP");
        http.end();
        return false;
    }

    size_t offset = 0;
    memcpy(payload + offset, head.c_str(), head.length());
    offset += head.length();
    memcpy(payload + offset, wavBuffer, WAV_HEADER_SIZE + wavDataSize);
    offset += WAV_HEADER_SIZE + wavDataSize;
    memcpy(payload + offset, tail.c_str(), tail.length());

    Serial.printf("Enviando %u bytes a %s...\n", (unsigned int)totalSize, serverUrl);
    int httpResponseCode = http.POST(payload, totalSize);

    Serial.printf("Codigo HTTP: %d\n", httpResponseCode);
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("--- Respuesta del servidor ---");
        Serial.println(response);
        Serial.println("------------------------------");
    } else {
        Serial.printf("Fallo en HTTP POST: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
    free(payload);

    return (httpResponseCode > 0 && httpResponseCode < 400);
}

// Función 4: Graba 5 segundos de audio desde el INMP441 y lo sube al servidor PHP
bool grabarYEnviar(const char* serverUrl = DEFAULT_SERVER_URL) {
    if (!wavBuffer) {
        Serial.println("Error: wavBuffer no inicializado. ¿Llamaste a iniciarDetectorVoz() en setup()?");
        return false;
    }

    Serial.printf("Grabando %d segundos de audio...\n", RECORD_SECONDS);

    // 1. Escribir cabecera WAV al inicio del buffer
    crearCabeceraWAV(wavBuffer, wavDataSize, EI_CLASSIFIER_FREQUENCY, 1, 16);

    // 2. Limpiar el buffer DMA para descartar audio viejo previo a la grabación
    i2s_zero_dma_buffer(I2S_PORT);

    // 3. Capturar muestras desde el mismo puerto I2S configurado
    size_t bytesLeidos = 0;
    size_t totalLeidos = 0;
    int16_t* pcmDest = (int16_t*)(wavBuffer + WAV_HEADER_SIZE);

    while (totalLeidos < wavDataSize) {
        size_t bytesPorLeer = wavDataSize - totalLeidos;
        if (bytesPorLeer > 1024) {
            bytesPorLeer = 1024; // Leer en bloques de 512 muestras (1024 bytes)
        }

        i2s_read(I2S_PORT, (void*)((uint8_t*)pcmDest + totalLeidos), bytesPorLeer, &bytesLeidos, portMAX_DELAY);

        if (bytesLeidos > 0) {
            // Aplicar ganancia por software a las muestras recién leídas
            size_t indiceInicio = totalLeidos / sizeof(int16_t);
            size_t muestrasLeidas = bytesLeidos / sizeof(int16_t);

            for (size_t i = 0; i < muestrasLeidas; i++) {
                int32_t muestra = (int32_t)pcmDest[indiceInicio + i] * AUDIO_GAIN;
                if (muestra > 32767) muestra = 32767;
                if (muestra < -32768) muestra = -32768;
                pcmDest[indiceInicio + i] = (int16_t)muestra;
            }

            totalLeidos += bytesLeidos;
        }
    }

    // Limpiar el DMA después de grabar para que el Wake Word retome audio fresco
    i2s_zero_dma_buffer(I2S_PORT);

    Serial.println("Grabacion de 5s completada. Subiendo al servidor...");
    return enviarWAV(serverUrl);
}

#endif