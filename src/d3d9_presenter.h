#ifndef SIGHTLINE_D3D9_PRESENTER_H
#define SIGHTLINE_D3D9_PRESENTER_H

#include <QMutex>
#include <QSize>
#include <QString>
#include <QWidget>

// Hands decoded YUV straight to Direct3D 9 and lets the GPU do the colour
// conversion and the scaling.
//
// This is not hardware decoding. That is genuinely unavailable on XP: DXVA2
// needs WDDM, so Vista; DXVA 1.0 exists but is only reachable through
// DirectShow and FFmpeg has never implemented it; NVDEC wants a driver API
// far newer than 369.09, the last NVIDIA shipped for XP. The decode stays on
// the CPU and there is no way around that.
//
// What this does remove is the other half of the per-frame cost. Right now
// every frame goes through sws_scale into BGRA and then a memcpy into a
// QImage: at 1080p that is megabytes of CPU work thirty times a second, and
// on these machines it is the difference between smooth and not. A YV12
// offscreen surface blitted with StretchRect makes the driver do both the
// colour conversion and the resize, which is exactly what VMR-9 did and what
// every XP-era GPU has silicon for.
//
// When the device or the format is unavailable the presenter reports that and
// the caller falls back to the software path, which still works everywhere.
class D3D9Presenter
{
public:
    D3D9Presenter();
    ~D3D9Presenter();

    // Created against the surface's native window. Multithreaded because the
    // decoder thread uploads frames directly, without a trip through the
    // event loop and without a QImage in between.
    bool initialise(WId window, const QSize &videoSize, QString *error = 0);
    void shutdown();
    bool isReady() const { return ready_; }

    // Called from the decoder thread. Planar YUV 4:2:0 in, one blit out.
    // Only usable when the driver accepted a YV12 surface.
    bool present(const unsigned char *y, int yStride,
                 const unsigned char *u, int uStride,
                 const unsigned char *v, int vStride,
                 int width, int height);

    // The second-best path: the caller has already converted to BGRA, and
    // the GPU still does the scaling and the blit. Worth having because a
    // software resize of 1080p down to the window is itself expensive, and
    // this removes it even on drivers that refuse YUV surfaces.
    bool presentBgra(const unsigned char *pixels, int stride, int width, int height);

    // Locks the surface so the caller can scale straight into video memory
    // rather than into a staging buffer that then has to be copied again.
    unsigned char *beginBgraFrame(int *stride, int width, int height);
    bool endBgraFrame();

    // The window changed size; the swap chain has to follow it.
    void resize(const QSize &clientSize);

    QString adapterName() const { return adapterName_; }
    bool usingOverlayFormat() const { return usingYv12_; }
    bool usingGpuScaling() const { return ready_; }
    QString describe() const;

private:
    bool createDevice(QString *error);
    bool createSurface(const QSize &videoSize, QString *error);
    void releaseSurface();
    bool handleLostDevice();
    bool blitToScreen(int width, int height);

    void *d3d_;              // IDirect3D9 *
    void *device_;           // IDirect3DDevice9 *
    void *surface_;          // IDirect3DSurface9 *, YV12 or X8R8G8B8
    void *windowHandle_;     // HWND

    QSize videoSize_;
    QSize clientSize_;
    bool ready_;
    bool usingYv12_;
    bool deviceLost_;
    QString adapterName_;
    mutable QMutex mutex_;
};

#endif
