<?php
/**
 * config.php
 * Cargador ligero de variables de entorno desde el archivo .env.
 * Compatible con PHP 7.0+ y PHP 8.x.
 */

function cargarEnv($rutaEnv) {
    $variables = array();

    if (!file_exists($rutaEnv) || !is_readable($rutaEnv)) {
        return $variables;
    }

    $lineas = file($rutaEnv, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
    if (!is_array($lineas)) {
        return $variables;
    }

    foreach ($lineas as $linea) {
        $linea = trim($linea);

        // Ignorar comentarios o líneas sin '='
        if ($linea === '' || strpos($linea, '#') === 0 || strpos($linea, '=') === false) {
            continue;
        }

        $partes = explode('=', $linea, 2);
        $clave  = trim($partes[0]);
        $valor  = trim($partes[1]);

        // Quitar comillas simples o dobles si existen
        $valor = trim($valor, "\"'");

        $variables[$clave] = $valor;
        @putenv("$clave=$valor");
        $_ENV[$clave] = $valor;
    }

    return $variables;
}

$env = cargarEnv(__DIR__ . '/.env');

return array(
    'groq' => array(
        'api_key'  => isset($env['GROQ_API_KEY'])      ? $env['GROQ_API_KEY']      : (getenv('GROQ_API_KEY')      ?: ''),
        'model'    => isset($env['GROQ_STT_MODEL'])    ? $env['GROQ_STT_MODEL']    : (getenv('GROQ_STT_MODEL')    ?: 'whisper-large-v3-turbo'),
        'language' => isset($env['GROQ_STT_LANGUAGE']) ? $env['GROQ_STT_LANGUAGE'] : (getenv('GROQ_STT_LANGUAGE') ?: 'es'),
        'timeout'  => isset($env['GROQ_TIMEOUT'])      ? (int)$env['GROQ_TIMEOUT'] : (int)(getenv('GROQ_TIMEOUT') ?: 30),
        'endpoint' => 'https://api.groq.com/openai/v1/audio/transcriptions',
    ),
    'storage' => array(
        'upload_dir' => __DIR__ . '/uploads/',
    )
);