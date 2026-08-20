#include "d3d9_presenter.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <d3d9.h>
#endif

#include <string.h>

namespace {
#ifdef Q_OS_WIN
// YV12 as a FourCC. Every XP-era driver that could play a DVD accepts this
// as an offscreen plain surface and converts it on the blit.
const D3DFORMAT kFormatYv12 = static_cast<D3DFORMAT>(MAKEFOURCC('Y', 'V', '1', '2'));
const D3DFORMAT kFormatNv12 = static_cast<D3DFORMAT>(MAKEFOURCC('N', 'V', '1', '2'));
#endif
}

D3D9Presenter::D3D9Presenter()
    : d3d_(0), device_(0), surface_(0), windowHandle_(0),
      layout_(LayoutBgra), ready_(false), usingYv12_(false), deviceLost_(false)
{
}

D3D9Presenter::~D3D9Presenter()
{
    shutdown();
}

#ifdef Q_OS_WIN

bool D3D9Presenter::initialise(WId window, const QSize &videoSize, Layout layout, QString *error)
{
    QMutexLocker locker(&mutex_);
    shutdown();

    windowHandle_ = reinterpret_cast<void *>(window);
    videoSize_ = videoSize;
    layout_ = layout;

    RECT client;
    GetClientRect(static_cast<HWND>(windowHandle_), &client);
    clientSize_ = QSize(qMax<LONG>(1, client.right - client.left),
                        qMax<LONG>(1, client.bottom - client.top));

    if (!createDevice(error))
        return false;
    if (!createSurface(videoSize, error)) {
        shutdown();
        return false;
    }

    ready_ = true;
    return true;
}

bool D3D9Presenter::createDevice(QString *error)
{
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        if (error) *error = QString::fromUtf8("Direct3D 9 no está disponible.");
        return false;
    }
    d3d_ = d3d;

    D3DADAPTER_IDENTIFIER9 identifier;
    if (SUCCEEDED(d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &identifier)))
        adapterName_ = QString::fromLatin1(identifier.Description);

    D3DPRESENT_PARAMETERS parameters;
    ZeroMemory(&parameters, sizeof(parameters));
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_COPY;
    parameters.hDeviceWindow = static_cast<HWND>(windowHandle_);
    parameters.BackBufferWidth = clientSize_.width();
    parameters.BackBufferHeight = clientSize_.height();
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    parameters.BackBufferCount = 1;

    // No vsync wait: the presentation clock already paces frames, and asking
    // the driver to block as well means the decoder thread sits in Present
    // instead of decoding the next frame.
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    // Software vertex processing because nothing here needs a vertex pipeline,
    // and multithreaded because the decoder thread calls present() directly.
    DWORD flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED
                | D3DCREATE_FPU_PRESERVE;

    IDirect3DDevice9 *device = 0;
    HRESULT result = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                       static_cast<HWND>(windowHandle_),
                                       flags, &parameters, &device);
    if (FAILED(result)) {
        if (error) *error = QString::fromUtf8("No se pudo crear el dispositivo D3D9.");
        return false;
    }
    device_ = device;
    return true;
}

bool D3D9Presenter::createSurface(const QSize &videoSize, QString *error)
{
    IDirect3D9 *d3d = static_cast<IDirect3D9 *>(d3d_);
    IDirect3DDevice9 *device = static_cast<IDirect3DDevice9 *>(device_);

    // Ask the driver whether it can convert YV12 to the back buffer format on
    // a blit. If it can, the colour conversion and the scaling both land on
    // the GPU and swscale disappears from the CPU budget entirely.
    IDirect3DSurface9 *surface = 0;
    HRESULT result = E_FAIL;

    const D3DFORMAT wanted = (layout_ == LayoutNv12) ? kFormatNv12
                           : (layout_ == LayoutYv12) ? kFormatYv12
                                                     : D3DFMT_X8R8G8B8;

    if (layout_ != LayoutBgra) {
        const bool converts = SUCCEEDED(d3d->CheckDeviceFormatConversion(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wanted, D3DFMT_X8R8G8B8));
        if (converts) {
            result = device->CreateOffscreenPlainSurface(
                videoSize.width(), videoSize.height(), wanted,
                D3DPOOL_DEFAULT, &surface, 0);
        }
        usingYv12_ = SUCCEEDED(result);
        if (!usingYv12_)
            layout_ = LayoutBgra;
    }

    if (FAILED(result)) {
        // Old integrated parts and some SiS/VIA drivers refuse YV12. Falling
        // back to an ARGB surface still keeps the scaling on the GPU, which
        // is most of the win, and only the conversion stays on the CPU.
        usingYv12_ = false;
        result = device->CreateOffscreenPlainSurface(
            videoSize.width(), videoSize.height(), D3DFMT_X8R8G8B8,
            D3DPOOL_DEFAULT, &surface, 0);
    }

    if (FAILED(result)) {
        if (error) *error = QString::fromUtf8("El driver no acepta ninguna superficie usable.");
        return false;
    }

    surface_ = surface;
    videoSize_ = videoSize;
    return true;
}

void D3D9Presenter::releaseSurface()
{
    if (surface_) {
        static_cast<IDirect3DSurface9 *>(surface_)->Release();
        surface_ = 0;
    }
}

void D3D9Presenter::shutdown()
{
    releaseSurface();
    if (device_) {
        static_cast<IDirect3DDevice9 *>(device_)->Release();
        device_ = 0;
    }
    if (d3d_) {
        static_cast<IDirect3D9 *>(d3d_)->Release();
        d3d_ = 0;
    }
    ready_ = false;
    deviceLost_ = false;
}

bool D3D9Presenter::handleLostDevice()
{
    IDirect3DDevice9 *device = static_cast<IDirect3DDevice9 *>(device_);
    const HRESULT state = device->TestCooperativeLevel();

    if (state == D3DERR_DEVICELOST)
        return false;

    if (state == D3DERR_DEVICENOTRESET) {
        // A default-pool surface cannot survive a reset, so it goes first and
        // is rebuilt after. Skipping this is the classic way to get a device
        // that resets successfully and then renders nothing.
        releaseSurface();

        D3DPRESENT_PARAMETERS parameters;
        ZeroMemory(&parameters, sizeof(parameters));
        parameters.Windowed = TRUE;
        parameters.SwapEffect = D3DSWAPEFFECT_COPY;
        parameters.hDeviceWindow = static_cast<HWND>(windowHandle_);
        parameters.BackBufferWidth = clientSize_.width();
        parameters.BackBufferHeight = clientSize_.height();
        parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
        parameters.BackBufferCount = 1;
        parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

        if (FAILED(device->Reset(&parameters)))
            return false;
        if (!createSurface(videoSize_, 0))
            return false;
        deviceLost_ = false;
    }
    return true;
}

void D3D9Presenter::resize(const QSize &clientSize)
{
    QMutexLocker locker(&mutex_);
    if (clientSize.isEmpty() || clientSize == clientSize_)
        return;
    clientSize_ = clientSize;
    deviceLost_ = true;   // forces a reset on the next present
}

bool D3D9Presenter::present(const unsigned char *y, int yStride,
                            const unsigned char *u, int uStride,
                            const unsigned char *v, int vStride,
                            int width, int height)
{
    QMutexLocker locker(&mutex_);
    if (!ready_ || !device_ || !surface_)
        return false;

    IDirect3DDevice9 *device = static_cast<IDirect3DDevice9 *>(device_);
    IDirect3DSurface9 *surface = static_cast<IDirect3DSurface9 *>(surface_);

    if (deviceLost_ || FAILED(device->TestCooperativeLevel())) {
        if (!handleLostDevice())
            return false;
        surface = static_cast<IDirect3DSurface9 *>(surface_);
        if (!surface)
            return false;
    }

    if (width != videoSize_.width() || height != videoSize_.height()) {
        releaseSurface();
        if (!createSurface(QSize(width, height), 0))
            return false;
        surface = static_cast<IDirect3DSurface9 *>(surface_);
    }

    if (layout_ != LayoutYv12)
        return false;   // the caller uses presentNv12 or presentBgra instead

    D3DLOCKED_RECT locked;
    if (FAILED(surface->LockRect(&locked, 0, D3DLOCK_NOSYSLOCK)))
        return false;

    unsigned char *base = static_cast<unsigned char *>(locked.pBits);
    const int pitch = locked.Pitch;

    for (int row = 0; row < height; ++row)
        memcpy(base + row * pitch, y + row * yStride, width);

    // YV12 orders the chroma planes V then U, which is the opposite of what
    // FFmpeg calls data[1] and data[2]. Getting this backwards does not fail,
    // it just makes everyone look sunburnt.
    const int chromaWidth = width / 2;
    const int chromaHeight = height / 2;
    unsigned char *planeV = base + pitch * height;
    unsigned char *planeU = planeV + (pitch / 2) * chromaHeight;

    for (int row = 0; row < chromaHeight; ++row) {
        memcpy(planeV + row * (pitch / 2), v + row * vStride, chromaWidth);
        memcpy(planeU + row * (pitch / 2), u + row * uStride, chromaWidth);
    }

    surface->UnlockRect();

    return blitToScreen(width, height);
}

// The shared tail of both present paths: letterbox, clear, stretch, show.
bool D3D9Presenter::blitToScreen(int width, int height)
{
    IDirect3DDevice9 *device = static_cast<IDirect3DDevice9 *>(device_);
    IDirect3DSurface9 *surface = static_cast<IDirect3DSurface9 *>(surface_);

    IDirect3DSurface9 *backBuffer = 0;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)))
        return false;

    // Letterboxed inside the client area: the aspect ratio of the decoded
    // frame is authoritative and the bars are part of the picture.
    const double videoAspect = double(width) / double(height);
    const double clientAspect = double(clientSize_.width()) / double(qMax(1, clientSize_.height()));

    RECT target;
    if (clientAspect > videoAspect) {
        const int drawWidth = int(clientSize_.height() * videoAspect);
        target.left = (clientSize_.width() - drawWidth) / 2;
        target.right = target.left + drawWidth;
        target.top = 0;
        target.bottom = clientSize_.height();
    } else {
        const int drawHeight = int(clientSize_.width() / videoAspect);
        target.left = 0;
        target.right = clientSize_.width();
        target.top = (clientSize_.height() - drawHeight) / 2;
        target.bottom = target.top + drawHeight;
    }

    device->ColorFill(backBuffer, 0, D3DCOLOR_XRGB(0x0A, 0x0E, 0x0F));

    RECT source = { 0, 0, width, height };
    const HRESULT blit = device->StretchRect(surface, &source, backBuffer, &target,
                                             D3DTEXF_LINEAR);
    backBuffer->Release();

    if (FAILED(blit))
        return false;

    const HRESULT shown = device->Present(0, 0, 0, 0);
    if (shown == D3DERR_DEVICELOST)
        deviceLost_ = true;

    return SUCCEEDED(shown);
}

unsigned char *D3D9Presenter::beginBgraFrame(int *stride, int width, int height)
{
    mutex_.lock();
    if (!ready_ || !device_ || !surface_ || usingYv12_) {
        mutex_.unlock();
        return 0;
    }

    IDirect3DDevice9 *device = static_cast<IDirect3DDevice9 *>(device_);
    if (deviceLost_ || FAILED(device->TestCooperativeLevel())) {
        if (!handleLostDevice()) {
            mutex_.unlock();
            return 0;
        }
    }

    if (width != videoSize_.width() || height != videoSize_.height()) {
        releaseSurface();
        if (!createSurface(QSize(width, height), 0)) {
            mutex_.unlock();
            return 0;
        }
    }

    IDirect3DSurface9 *surface = static_cast<IDirect3DSurface9 *>(surface_);
    D3DLOCKED_RECT locked;
    if (FAILED(surface->LockRect(&locked, 0, D3DLOCK_NOSYSLOCK))) {
        mutex_.unlock();
        return 0;
    }

    *stride = locked.Pitch;
    return static_cast<unsigned char *>(locked.pBits);
}

bool D3D9Presenter::endBgraFrame()
{
    IDirect3DSurface9 *surface = static_cast<IDirect3DSurface9 *>(surface_);
    if (!surface) {
        mutex_.unlock();
        return false;
    }
    surface->UnlockRect();
    const bool shown = blitToScreen(videoSize_.width(), videoSize_.height());
    mutex_.unlock();
    return shown;
}

bool D3D9Presenter::presentNv12(const unsigned char *y, int yStride,
                                const unsigned char *uv, int uvStride,
                                int width, int height)
{
    QMutexLocker locker(&mutex_);
    if (!ready_ || !device_ || !surface_ || layout_ != LayoutNv12)
        return false;

    IDirect3DDevice9 *device = static_cast<IDirect3DDevice9 *>(device_);
    if (deviceLost_ || FAILED(device->TestCooperativeLevel())) {
        if (!handleLostDevice())
            return false;
    }

    if (width != videoSize_.width() || height != videoSize_.height()) {
        releaseSurface();
        if (!createSurface(QSize(width, height), 0))
            return false;
    }

    IDirect3DSurface9 *surface = static_cast<IDirect3DSurface9 *>(surface_);
    D3DLOCKED_RECT locked;
    if (FAILED(surface->LockRect(&locked, 0, D3DLOCK_NOSYSLOCK)))
        return false;

    unsigned char *base = static_cast<unsigned char *>(locked.pBits);
    const int pitch = locked.Pitch;

    for (int row = 0; row < height; ++row)
        memcpy(base + row * pitch, y + row * yStride, width);

    // NV12 keeps chroma interleaved directly after the luma plane, so it is
    // one copy rather than the two YV12 needs.
    unsigned char *chroma = base + pitch * height;
    for (int row = 0; row < height / 2; ++row)
        memcpy(chroma + row * pitch, uv + row * uvStride, width);

    surface->UnlockRect();
    return blitToScreen(width, height);
}

bool D3D9Presenter::presentBgra(const unsigned char *pixels, int stride, int width, int height)
{
    int surfaceStride = 0;
    unsigned char *destination = beginBgraFrame(&surfaceStride, width, height);
    if (!destination)
        return false;

    const int rowBytes = width * 4;
    for (int row = 0; row < height; ++row)
        memcpy(destination + row * surfaceStride, pixels + row * stride, rowBytes);

    return endBgraFrame();
}

QString D3D9Presenter::describe() const
{
    if (!ready_)
        return QString::fromUtf8("D3D9 no disponible");
    if (layout_ == LayoutYv12)
        return QString::fromUtf8("D3D9 YV12: color y escalado en GPU");
    if (layout_ == LayoutNv12)
        return QString::fromUtf8("D3D9 NV12: color y escalado en GPU");
    return QString::fromUtf8("D3D9 BGRA: escalado en GPU, color en CPU");
}

#else

bool D3D9Presenter::initialise(WId, const QSize &, Layout, QString *error)
{
    if (error) *error = QString::fromUtf8("Direct3D 9 solo existe en Windows.");
    return false;
}
bool D3D9Presenter::createDevice(QString *) { return false; }
bool D3D9Presenter::createSurface(const QSize &, QString *) { return false; }
void D3D9Presenter::releaseSurface() {}
bool D3D9Presenter::handleLostDevice() { return false; }
void D3D9Presenter::shutdown() { ready_ = false; }
void D3D9Presenter::resize(const QSize &) {}
bool D3D9Presenter::present(const unsigned char *, int, const unsigned char *, int,
                            const unsigned char *, int, int, int) { return false; }
bool D3D9Presenter::presentBgra(const unsigned char *, int, int, int) { return false; }
bool D3D9Presenter::presentNv12(const unsigned char *, int, const unsigned char *, int,
                                int, int) { return false; }
unsigned char *D3D9Presenter::beginBgraFrame(int *, int, int) { return 0; }
bool D3D9Presenter::endBgraFrame() { return false; }
bool D3D9Presenter::blitToScreen(int, int) { return false; }
QString D3D9Presenter::describe() const { return QString::fromUtf8("D3D9 no disponible"); }

#endif
