# Sightline

Cliente nativo de YouTube y YouTube Music para **Windows XP SP3 x86** (probado contra XP Integral Edition).

Sin navegador y sin cuenta obligatoria: las suscripciones, las listas, el historial y las
estadísticas de escucha viven en un JSON de tu disco. Vincular una cuenta de Google solo sirve
para **copiar datos hacia dentro**; nada se escribe de vuelta.

| | |
|---|---|
| Toolchain | Qt 5.6.3 estático · MSVC 2017 toolset `v141_xp` · qmake |
| Decodificación | FFmpeg 7.1 (`N-116828-g6aafe61-Reino`, XP mod SSE) enlazado dinámicamente |
| Audio | DirectSound (XP no tiene WASAPI; waveOut no da cursor de reproducción) |
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

**`yt-dlp.exe` va junto a `Sightline.exe`.** Si no está, la app lo detecta al arrancar,
identifica tu versión de Windows y ofrece descargar el binario correcto. Los cinco de la
release `2026.08.16.082019` del fork de [nicolaasjan](https://github.com/nicolaasjan/yt-dlp):

| Sistema | Archivo |
|---|---|
| Windows XP 32 bits | `yt-dlp_x86_winXP.exe` |
| Windows 7 32 / 64 bits | `yt-dlp_x86_win7.exe` / `yt-dlp_win7.exe` |
| Windows 8+ 32 / 64 bits | `yt-dlp_x86.exe` / `yt-dlp.exe` |

Elegir mal no falla suave: un binario de Windows 8 en XP muere con «no es una aplicación
Win32 válida» y no dice nada útil, así que la elección se hace desde la versión real del SO
en vez de dejártela a ti. Se descarga con nombre `.part` y se renombra solo al terminar, para
que una descarga cortada nunca deje un ejecutable truncado.

**Además, junto al ejecutable:**

- **Las DLL de FFmpeg 7.1** (`avcodec-61.dll`, `avformat-61.dll`, `avutil-59.dll`,
  `swscale-8.dll`, `swresample-5.dll`), del paquete `-shared` del mismo build. En el repo
  viven en `third_party/ffmpeg/`, junto a `include/` y `lib/`. `avcodec-61.dll` pasa de los
  100 MB que admite GitHub, así que se guarda comprimido como `.7z` en esa misma carpeta y
  el workflow lo extrae antes de compilar; si falta alguna de las cinco, el build falla ahí
  con el nombre exacto en vez de producir un `.exe` que muere al arrancar.
- **`qjs.exe`** (quickjs-ng), opcional pero importante. Node y Deno no arrancan en XP;
  QuickJS es C99 y sí compila. Sin él, yt-dlp cae a su intérprete interno: más lento y más
  frágil, pero funcional. La barra de estado te dice cuál está en uso.
- **Proveedor de PO tokens** para calidad completa: levanta `bgutil-ytdlp-pot-provider` en
  modo servidor HTTP en cualquier máquina moderna de tu red y apunta ahí en *Herramientas →
  Cadena de extracción*. BotGuard no correrá nunca en XP, pero no tiene por qué correr ahí.
  Sin proveedor, la reproducción sigue funcionando limitada al subconjunto de formatos que
  no exige token.

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

## Cómo llega el vídeo a la pantalla

YouTube sirve vídeo y audio como archivos separados, así que corren **dos `MediaDecoder`**,
cada uno con su hilo y su `AVFormatContext`.

Lo que no es obvio: **FFmpeg no abre las URLs**. Cada decodificador va sobre un `AVIOContext`
propio alimentado por `MediaSource`, que baja los bytes con `QNetworkAccessManager` y peticiones
`Range`. No es un rodeo — es la única ruta viable en XP. El HTTPS de FFmpeg iría por Schannel,
que se queda en TLS 1.0, y googlevideo exige 1.2 desde hace años. Qt ya está enlazado contra
un OpenSSL que controlamos, así que los bytes entran por Qt y FFmpeg solo ve un contexto de E/S
plano. De paso, un único sitio se encarga del 403 por caducidad: cuando el enlace muere a mitad
de reproducción, `MediaSource` lo distingue de un fallo de red y el reproductor vuelve a extraer
y retoma donde ibas, en lugar de mostrarte un error de E/S genérico.

El **audio manda el reloj**: `position()` sale del PTS de audio menos lo que sigue en el búfer
de DirectSound, o sea lo que el oyente está oyendo de verdad. El vídeo solo lleva el reloj
cuando no hay pista de audio.

El escalador entrega BGRA al tamaño exacto de la superficie, así que el `paintEvent` es un blit
directo sin reescalar dos veces. Sin DXVA2 en XP la decodificación es por CPU, con hilos por
fotograma y por slice: en un Pentium 4 eso es un núcleo y no cuesta nada, pero en los Core 2 y
posteriores sobre los que suele correr XP Integral Edition es la diferencia entre 720p fluido y
720p a tirones.

## Estado

Interfaz, biblioteca, extracción, SponsorBlock, estadísticas, diálogos, decodificación y audio
están implementados. Falta el camino **Direct3D 9**: hoy los fotogramas se pintan con `QPainter`
sobre la superficie, que ya tiene su `HWND` nativa esperando el swap chain. Eso pasará el
YUV→RGB al pixel shader y quitará el `sws_scale` a BGRA del presupuesto de CPU.
