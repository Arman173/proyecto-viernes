<?php
// upload.php — versión robusta sin finfo
header('Content-Type: application/json');

// Mostrar errores temporalmente para depurar (quitar en producción)
error_reporting(E_ALL);
ini_set('display_errors', 0); // 0 para no romper el JSON, pero revisa logs

$uploadDir = __DIR__ . '/uploads/';
if (!is_dir($uploadDir)) {
    if (!mkdir($uploadDir, 0755, true)) {
        http_response_code(500);
        echo json_encode(['error' => 'No se pudo crear la carpeta uploads']);
        exit;
    }
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(['error' => 'Método no permitido']);
    exit;
}

if (!isset($_FILES['audio'])) {
    http_response_code(400);
    echo json_encode(['error' => 'No se recibió el archivo de audio']);
    exit;
}

$file = $_FILES['audio'];

if ($file['error'] !== UPLOAD_ERR_OK) {
    http_response_code(500);
    echo json_encode(['error' => 'Error de subida código: ' . $file['error']]);
    exit;
}

// Validación simple por extensión
$ext = strtolower(pathinfo($file['name'], PATHINFO_EXTENSION));
if ($ext !== 'wav') {
    http_response_code(400);
    echo json_encode(['error' => 'Solo se permiten archivos .wav']);
    exit;
}

// Nombre único sin random_bytes (por si acaso)
$filename = 'audio_' . date('Ymd_His') . '_' . uniqid() . '.wav';
$destination = $uploadDir . $filename;

if (move_uploaded_file($file['tmp_name'], $destination)) {
    echo json_encode([
        'status'   => 'success',
        'filename' => $filename,
        'size'     => filesize($destination),
        'url'      => 'download.php?file=' . urlencode($filename)
    ]);
} else {
    http_response_code(500);
    echo json_encode(['error' => 'No se pudo mover el archivo. Revisa permisos de uploads/']);
}
?>