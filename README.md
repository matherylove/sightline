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
pertenece a un proyecto que lo sigue al día. Actualizar la extracción es reemplazar un archivo,
sin recompilar nada.

El principio se aplica hasta donde llega. Todo lo que yt-dlp o FFmpeg ya resuelven **no se
reimplementa aquí**:

| Trabajo | Quién lo hace |
|---|---|
| Firmas, `n`, clientes InnerTube, PO token | yt-dlp |
| Búsqueda, canales, listas, comentarios | yt-dlp |
| **Selección de formato** | el selector de yt-dlp; Sightline solo declara la política |
| **SponsorBlock** | `--sponsorblock-mark`, dentro de la misma extracción |
| Quitar patrocinios del archivo | `--sponsorblock-remove`, post-procesador de yt-dlp |
| Descarga y recorte | `--download-sections`, `--force-keyframes-at-cuts` |
| Demux, decodificación, remuestreo | FFmpeg |
| HTTP y TLS hacia googlevideo | FFmpeg (`avio`), Qt solo como reserva |
| Miniaturas | FFmpeg (`avio`) en un hilo aparte |

Lo que queda para Sightline es lo que nadie más puede hacer por él: la interfaz, la biblioteca
local, el reloj de reproducción y la presentación de los fotogramas.

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

## Qué pila de red se usa, y por qué importa

Los builds estáticos de Qt 5.6 que aún apuntan a XP casi nunca llevan OpenSSL enlazado, y Qt
no tuvo backend de Schannel hasta la 5.14. El resultado es que **toda petición `https://` desde
Qt falla sin llegar a hacer handshake** — y eso se lleva por delante miniaturas, googlevideo,
SponsorBlock y OAuth de una vez.

Por eso Sightline decide el transporte **una sola vez al arrancar** y lo enseña en la barra de
estado, en vez de dejarte redescubrirlo como cuatro misterios distintos:

| Celda | Significa |
|---|---|
| `TLS Qt` | Qt tiene OpenSSL. Todo funciona por la ruta normal. |
| `TLS FFmpeg` | Qt no puede, pero FFmpeg sí. Miniaturas y streams van por `avio`. |
| `sin TLS` | Ninguno de los dos. Solo yt-dlp llega a la red; no hay reproducción. |

Las miniaturas se bajan en un hilo aparte (`ThumbnailFetcher`) por el transporte que haya, y
siempre como `hqdefault.jpg`: Qt 5.6 no trae plugin de WebP y una compilación estática no lo va
a ganar. SponsorBlock, si Qt no puede consultar, sigue aplicando los segmentos que ya tenga en
disco — la reproducción nunca se bloquea por un servicio externo.

## Cómo llega el vídeo a la pantalla

YouTube sirve vídeo y audio como archivos separados, así que corren **dos `MediaDecoder`**,
cada uno con su hilo y su `AVFormatContext`.

Hay **dos transportes** y se elige el que funcione. Por defecto `MediaDecoder` deja que
libavformat abra la URL con su propio HTTP y TLS, que es lo que traen estos builds XP de FFmpeg
y lo que sigue funcionando cuando Qt no tiene OpenSSL. Si esa apertura falla, reintenta por
`MediaSource`, un `AVIOContext` propio alimentado desde `QNetworkAccessManager` con peticiones
`Range`. Ninguna de las dos rutas se anuncia al usuario hasta haber probado las dos.

En la ruta nativa se le pasan a FFmpeg `reconnect`, `reconnect_streamed` y `rw_timeout`: estos
archivos se sirven por trozos y la conexión se corta entre ellos, así que un stream que se para
tiene que reconectar en vez de darse por terminado. Y el 403 por caducidad del enlace se
distingue de un fallo real comparando la posición con la duración — si se corta muy lejos del
final, se vuelve a extraer y se retoma donde ibas en lugar de mostrarte un error de E/S.

El **audio manda el reloj**: `position()` sale del PTS de audio menos lo que sigue en el búfer
de DirectSound, o sea lo que el oyente está oyendo de verdad. El vídeo solo lleva el reloj
cuando no hay pista de audio.

El escalador entrega BGRA al tamaño exacto de la superficie, así que el `paintEvent` es un blit
directo sin reescalar dos veces. Sin DXVA2 en XP la decodificación es por CPU, con hilos por
fotograma y por slice: en un Pentium 4 eso es un núcleo y no cuesta nada, pero en los Core 2 y
posteriores sobre los que suele correr XP Integral Edition es la diferencia entre 720p fluido y
720p a tirones.

## Aceleración por hardware: qué hay y qué no

**No hay decodificación por hardware en XP, y no es rodeable.** DXVA2 necesita WDDM, o sea
Vista. DXVA 1.0 sí existe en XP pero solo se expone por DirectShow, y FFmpeg nunca lo ha
implementado. NVDEC exige una API de driver muy por encima del 369.09, el último que NVIDIA
publicó para XP. QuickSync pide drivers de Win7+. La decodificación se queda en la CPU.

Lo que sí se puede mover a la GPU es la otra mitad del coste por fotograma, y eso es lo que
hace `D3D9Presenter`. Hay tres rutas y la barra de estado dice cuál está activa:

| Ruta | Qué hace la GPU |
|---|---|
| `D3D9 YV12` | Conversión de color **y** escalado. `sws_scale` desaparece del presupuesto. |
| `D3D9 BGRA` | Solo el escalado; swscale escribe directo en memoria de vídeo, sin copia intermedia. |
| Software | Todo en CPU. Solo si el driver rechaza ambas superficies. |

La primera es lo que hacía VMR-9 y lo que cualquier GPU de la era XP tiene en silicio. La
segunda existe porque algunos integrados viejos (SiS, VIA) rechazan superficies YUV, y ahí
el escalado sigue siendo gratis aunque la conversión no lo sea.

Además, si el decodificador acumula más de 30 fotogramas tarde, activa
`skip_loop_filter = AVDISCARD_NONREF`: recupera cerca de un cuarto del tiempo de decodificación
a cambio de algo de nitidez, que es mejor trato que descartar uno de cada tres fotogramas.

## Estado

Interfaz, biblioteca, extracción, SponsorBlock, estadísticas, diálogos, decodificación y audio
están implementados. Falta el camino **Direct3D 9**: hoy los fotogramas se pintan con `QPainter`
sobre la superficie, que ya tiene su `HWND` nativa esperando el swap chain. Eso pasará el
YUV→RGB al pixel shader y quitará el `sws_scale` a BGRA del presupuesto de CPU.
