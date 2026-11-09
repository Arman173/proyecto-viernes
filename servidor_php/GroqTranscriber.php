<?php
/**
 * GroqTranscriber.php
 * Servicio desacoplado para transcribir archivos de audio (Speech-to-Text)
 * utilizando la API de Groq. No depende de $_FILES ni de la capa HTTP de entrada.
 */

class GroqTranscriber {
    private string $apiKey;
    private string $model;
    private string $language;
    private string $endpoint;
    private int $timeout;

    public function __construct(array $groqConfig) {
        $this->apiKey   = trim($groqConfig['api_key'] ?? '');
        $this->model    = $groqConfig['model']    ?? 'whisper-large-v3-turbo';
        $this->language = $groqConfig['language'] ?? 'es';
        $this->endpoint = $groqConfig['endpoint'] ?? 'https://api.groq.com/openai/v1/audio/transcriptions';
        $this->timeout  = (int)($groqConfig['timeout'] ?? 30);
    }

    /**
     * Envía un archivo de audio local a Groq y devuelve un arreglo con:
     * ['success' => bool, 'text' => string, 'error' => string]
     */
    public function transcribir(string $rutaArchivoAudio, string $nombreOriginal = 'audio.wav'): array {
        if ($this->apiKey === '' || $this->apiKey === 'gsk_PEGA_AQUI_TU_API_KEY_DE_GROQ') {
            return [
                'success' => false,
                'text'    => '',
                'error'   => 'API Key de Groq no configurada en el archivo .env'
            ];
        }

        if (!file_exists($rutaArchivoAudio) || !is_readable($rutaArchivoAudio)) {
            return [
                'success' => false,
                'text'    => '',
                'error'   => 'El archivo de audio a transcribir no existe o no se puede leer'
            ];
        }

        $cfile = new CURLFile($rutaArchivoAudio, 'audio/wav', $nombreOriginal);

        $postFields = [
            'file'            => $cfile,
            'model'           => $this->model,
            'language'        => $this->language,
            'response_format' => 'json'
        ];

        $ch = curl_init();
        curl_setopt_array($ch, [
            CURLOPT_URL            => $this->endpoint,
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_POST           => true,
            CURLOPT_POSTFIELDS     => $postFields,
            CURLOPT_TIMEOUT        => $this->timeout,
            CURLOPT_HTTPHEADER     => [
                'Authorization: Bearer ' . $this->apiKey
            ],
        ]);

        $responseBody = curl_exec($ch);
        $curlErrno    = curl_errno($ch);
        $curlError    = curl_error($ch);
        $httpCode     = (int)curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);

        if ($curlErrno !== 0) {
            return [
                'success' => false,
                'text'    => '',
                'error'   => 'Error cURL al conectar con Groq: ' . $curlError
            ];
        }

        $decoded = json_decode((string)$responseBody, true);

        if ($httpCode < 200 || $httpCode >= 300) {
            $detalleError = $decoded['error']['message'] ?? ('Código HTTP ' . $httpCode);
            return [
                'success' => false,
                'text'    => '',
                'error'   => 'Error de API Groq: ' . $detalleError
            ];
        }

        if (!is_array($decoded) || !isset($decoded['text'])) {
            return [
                'success' => false,
                'text'    => '',
                'error'   => 'Respuesta inesperada de Groq (sin campo text)'
            ];
        }

        return [
            'success' => true,
            'text'    => trim((string)$decoded['text']),
            'error'   => ''
        ];
    }
}