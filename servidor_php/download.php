<?php
// download.php
$file = isset($_GET['file']) ? basename($_GET['file']) : '';
$dir = 'uploads/';
$path = $dir . $file;

if (!empty($file) && is_file($path)) {
    // Forzar la descarga
    header('Content-Description: File Transfer');
    header('Content-Type: audio/wav');
    header('Content-Disposition: attachment; filename="' . $file . '"');
    header('Content-Transfer-Encoding: binary');
    header('Expires: 0');
    header('Cache-Control: must-revalidate');
    header('Pragma: public');
    header('Content-Length: ' . filesize($path));
    
    readfile($path);
    exit;
} else {
    http_response_code(404);
    echo 'Archivo no encontrado.';
}
?>