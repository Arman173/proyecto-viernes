<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <title>Audios Grabados</title>
    <style>
        body { font-family: sans-serif; margin: 2rem; }
        ul { list-style: none; padding: 0; }
        li { margin: 0.5rem 0; background: #f4f4f4; padding: 0.5rem; border-radius: 4px; }
        a { text-decoration: none; color: #007bff; }
    </style>
</head>
<body>
    <h1>Audios disponibles</h1>
    <ul>
        <?php
        $dir = 'uploads/';
        if (is_dir($dir)) {
            $files = array_diff(scandir($dir), array('.', '..'));
            rsort($files); // Más recientes primero
            if (empty($files)) {
                echo '<li>No hay audios grabados aún.</li>';
            } else {
                foreach ($files as $file) {
                    echo '<li><a href="download.php?file=' . urlencode($file) . '">' . htmlspecialchars($file) . '</a></li>';
                }
            }
        } else {
            echo '<li>La carpeta de subidas no existe.</li>';
        }
        ?>
    </ul>
</body>
</html>