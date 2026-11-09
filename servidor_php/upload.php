<?php
/**
 * upload.php
 * Controlador de entrada para recibir el archivo .wav desde el ESP32-S3,
 * guardarlo opcionalmente en disco y transcribirlo mediante GroqTranscriber.
 */

header('Content-Type: application/json; charset=utf-8');

error_reporting(E_ALL);
ini_set('display_errors', '0');

$GLOBALS['respuesta_enviada'] = false;

/**
 * Envía una respuesta JSON estandarizada y termina la ejecución.
 */
function responderJson($codigoHttp, $status, $texto = '', $error = '') {
    $GLOBALS['respuesta_enviada'] = true;
    http_response_code($codigoHttp);
    if ($status === 'success') {
        echo json_encode(array(
            'status' => 'success',
            'text'   => $texto
        ), JSON_UNESCAPED_UNICODE);
    } else {
        echo json_encode(array(
            'status' => 'error',
            'text'   => '',
            'error'  => $error
        ), JSON_UNESCAPED_UNICODE);
    }
    exit;
}

// Capturar cualquier error fatal de PHP para devolverlo siempre como JSON al Monitor Serial del ESP32
register_shutdown_function(function () {
    if (!empty($GLOBALS['respuesta_enviada'])) {
        return;
    }
    $err = error_get_last();
    if ($err !== null && in_array($err['type'], array(E_ERROR, E_PARSE, E_CORE_ERROR, E_COMPILE_ERROR, E_USER_ERROR), true)) {
        http_response_code(500);
        echo json_encode(array(
            'status' => 'error',
            'text'   => '',
            'error'  => 'PHP Fatal: ' . $err['message'] . ' en ' . basename($err['file']) . ':' . $err['line']
        ), JSON_UNESCAPED_UNICODE);
    }
});

// Verificar que existan los archivos desacoplados en el mismo directorio
if (!file_exists(__DIR__ . '/config.php')) {
    responderJson(500, 'error', '', 'Falta el archivo config.php en la carpeta del servidor');
}
if (!file_exists(__DIR__ . '/GroqTranscriber.php')) {
    responderJson(500, 'error', '', 'Falta el archivo GroqTranscriber.php en la carpeta del servidor');
}

$config = require __DIR__ . '/config.php';
require_once __DIR__ . '/GroqTranscriber.php';

/**
 * Guarda una copia del archivo .wav recibido dentro de la carpeta uploads/.
 */
function guardarCopiaWav($rutaTemporal, $uploadDir) {
    if (!is_dir($uploadDir)) {
        if (!@mkdir($uploadDir, 0755, true)) {
            responderJson(500, 'error', '', 'No se pudo crear la carpeta uploads/ (revisa permisos)');
        }
    }

    $filename = 'audio_' . date('Ymd_His') . '_' . uniqid() . '.wav';
    $destino  = rtrim($uploadDir, '/\\') . DIRECTORY_SEPARATOR . $filename;

    if (!@copy($rutaTemporal, $destino)) {
        responderJson(500, 'error', '', 'No se pudo guardar en uploads/ (revisa permisos de escritura en Linux)');
    }

    return $destino;
}

// 1. Validar método HTTP
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    responderJson(405, 'error', '', 'Metodo no permitido. Usa POST.');
}

// 2. Validar presencia y estado del archivo de audio
if (!isset($_FILES['audio'])) {
    responderJson(400, 'error', '', 'No se recibio el archivo de audio.');
}

$file = $_FILES['audio'];

if ($file['error'] !== UPLOAD_ERR_OK) {
    responderJson(400, 'error', '', 'Error al subir archivo (codigo: ' . $file['error'] . ')');
}

$ext = strtolower(pathinfo($file['name'], PATHINFO_EXTENSION));
if ($ext !== 'wav') {
    responderJson(400, 'error', '', 'Solo se permiten archivos .wav');
}

$rutaAudioParaTranscribir = $file['tmp_name'];

// 3. Guardar el archivo .wav en uploads/ (comenta la siguiente línea para omitir el guardado)
$rutaAudioParaTranscribir = guardarCopiaWav($file['tmp_name'], $config['storage']['upload_dir']);

// 4. Transcribir el audio con el servicio desacoplado de Groq
$transcriber = new GroqTranscriber($config['groq']);
$resultado   = $transcriber->transcribir($rutaAudioParaTranscribir, $file['name']);

if (!$resultado['success']) {
    responderJson(502, 'error', '', $resultado['error']);
}

// 5. Responder al ESP32 con el formato: {"status": "success", "text": "..."}
responderJson(200, 'success', $resultado['text']);