<?php
echo "Método: " . $_SERVER['REQUEST_METHOD'] . "<br>";
echo "Content-Type: " . ($_SERVER['CONTENT_TYPE'] ?? 'no definido') . "<br>";
echo "Tamaño POST: " . ($_SERVER['CONTENT_LENGTH'] ?? 0) . " bytes<br>";

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    echo "Archivos recibidos: <pre>";
    print_r($_FILES);
    echo "</pre>";
    echo "POST: <pre>";
    print_r($_POST);
    echo "</pre>";
}
?>