# Sightline

Cliente nativo de YouTube y YouTube Music para **Windows XP SP3 x86** (probado contra XP Integral Edition).

Sin navegador y sin cuenta obligatoria: las suscripciones, las listas, el historial y las
estadísticas de escucha viven en un JSON de tu disco. Vincular una cuenta de Google solo sirve
para **copiar datos hacia dentro**; nada se escribe de vuelta.

| | |
|---|---|
| Toolchain | Qt 5.6.3 estático · MSVC 2017 toolset `v141_xp` · qmake |
| Render | Direct3D 9 sobre `HWND` hija (XP no expone DXVA2 → decode por CPU) |
| Códecs | H.264 + AAC/Opus. VP9 y AV1 se omiten por política de CPU |
| Extracción | `yt-dlp.exe` como proceso externo — nunca InnerTube desde C++ |

## Arquitectura

La frontera es deliberada: **Sightline no habla con YouTube**. Las firmas, el parámetro `n`,
la suplantación de cliente y el PO token cambian cada pocas semanas, así que ese mantenimiento
pertenece a un proyecto que lo sigue al día. Actualizar la extracción es reemplazar una carpeta,
sin recompilar nada.

```
src/
  sightline_style.h      Paleta y hoja de estilo (derivadas del mockup HTML)
  sightline_paint.*      Hatch, miniaturas, chips, fuentes, formateo de tiempos
  sightline_window.*     Barra de título sin marco, diálogo base
  media_types.*          VideoItem, MediaFormat, SponsorSegment, LyricLine…
  library.*              Suscripciones, listas, historial, miniaturas, import Takeout
  listening_stats.*      Registro de escucha y agregación para el recap en vivo
  ytdlp.*                QProcess: cola, UTF-8, --ignore-config, parseo JSON
  sponsorblock.*         API con prefijo SHA-256, caché en disco, degradación offline
  playback.*             Reloj de reproducción y lógica de saltos
  widgets.*              Barra de estado, sidebar, rejilla, seek bar, superficie, gráficos
  player_page.*          Reproductor: recomendados, comentarios, formatos
  music_page.*           Música y letras sincronizadas
  stats_page.*           Estadísticas en vivo
  dialogs.*              Descarga con recorte, vinculación, SponsorBlock, herramientas
  pip_window.*           Picture in Picture
  main_window.*          Ensamblaje, menús, enrutado de vistas
design/
  sightline-ui-mockup.html   La maqueta que el código implementa al pie de la letra
```

## Antes de ejecutarlo

1. Descarga **yt-dlp para XP** de [nicolaasjan/yt-dlp](https://github.com/nicolaasjan/yt-dlp/releases)
   y ponlo en `tools\yt-dlp.exe`. Usa la variante **onedir** si está disponible: el paquete
   onefile se descomprime entero en `%TEMP%` en cada llamada y en XP eso son varios segundos
   por acción.
2. **Opcional pero importante:** compila `qjs.exe` (quickjs-ng) y ponlo en `tools\`. Node y Deno
   no arrancan en XP; QuickJS es C99 y sí compila. Sin él, yt-dlp cae a su intérprete interno:
   más lento y más frágil, pero funcional. La barra de estado te dirá cuál está en uso.
3. **Para calidad completa:** levanta `bgutil-ytdlp-pot-provider` en modo servidor HTTP en
   cualquier máquina moderna de tu red y apunta ahí en *Herramientas → Cadena de extracción*.
   BotGuard no correrá nunca en XP, pero no tiene por qué correr ahí. Sin proveedor, la
   reproducción sigue funcionando limitada al subconjunto de formatos que no exige token.

Un archivo `portable.txt` junto al ejecutable mueve todos los datos a `data\` en vez de
Application Data.

## Compilar

CI lo hace solo en cada push (`.github/workflows/build-xp.yml`): descarga Qt 5.6.3 estático,
instala las VS 2017 Build Tools con el componente `WinXP`, compila con `nmake` y **verifica
que el PE reporta subsistema 5.1** antes de publicar el artefacto — un binario que enlaza pero
declara 6.0 se niega a arrancar en XP con «no es una aplicación Win32 válida», y es mejor
descubrirlo en CI que en la máquina de destino.

En local, con Qt 5.6.3 para `win32-msvc2017`:

```cmd
call "C:\BuildTools2017\VC\Auxiliary\Build\vcvarsall.bat" x86
mkdir build && cd build
qmake -spec win32-msvc2017 ..\Sightline.pro CONFIG+=release
nmake
```

## Estado

La interfaz, la biblioteca, la extracción, SponsorBlock, las estadísticas y los diálogos están
implementados. **El decodificador todavía no**: `PlaybackController` lleva el reloj y toda la
UI cuelga de `position()` y `duration()`, así que enchufar FFmpeg 4.4 más adelante es
implementar una interfaz, no tocarla. `VideoSurface` ya tiene su `HWND` nativa esperando el
swap chain de D3D9.
