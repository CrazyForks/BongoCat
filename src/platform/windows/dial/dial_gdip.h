#ifndef BONGO_CAT_DIAL_GDIP_H
#define BONGO_CAT_DIAL_GDIP_H
/* GDI+ flat ABI: no C++ headers, wrappers or browser runtime. */
#include <windows.h>
typedef struct GpBitmap GpBitmap;
typedef struct GpGraphics GpGraphics;
typedef struct GpPath GpPath;
typedef struct GpBrush GpBrush;
typedef struct GpPen GpPen;
typedef struct GpFontFamily GpFontFamily;
typedef struct GpFont GpFont;
typedef struct GpStringFormat GpStringFormat;
typedef struct DialPoint { float x, y; } DialPoint;
typedef struct DialRect { float x, y, w, h; } DialRect;
typedef struct DialStartup {
    UINT32 version;
    void *callback;
    BOOL no_thread, no_codecs;
} DialStartup;
int WINAPI GdiplusStartup(ULONG_PTR *, const DialStartup *, void *);
void WINAPI GdiplusShutdown(ULONG_PTR);
int WINAPI GdipCreateBitmapFromScan0(INT, INT, INT, INT, BYTE *, GpBitmap **);
int WINAPI GdipGetImageGraphicsContext(void *, GpGraphics **);
int WINAPI GdipDisposeImage(void *);
int WINAPI GdipCreateBitmapFromFile(const WCHAR *, GpBitmap **);
int WINAPI GdipGetImageWidth(void *, UINT *);
int WINAPI GdipGetImageHeight(void *, UINT *);
int WINAPI GdipSetInterpolationMode(GpGraphics *, int);
int WINAPI GdipDrawImageRect(GpGraphics *, void *, float, float, float, float);
int WINAPI GdipDeleteGraphics(GpGraphics *);
int WINAPI GdipGraphicsClear(GpGraphics *, DWORD);
int WINAPI GdipSetSmoothingMode(GpGraphics *, int);
int WINAPI GdipSetTextRenderingHint(GpGraphics *, int);
int WINAPI GdipSetWorldTransform(GpGraphics *, void *);
int WINAPI GdipCreateMatrix2(float, float, float, float, float, float, void **);
int WINAPI GdipDeleteMatrix(void *);
int WINAPI GdipCreatePath(int, GpPath **);
int WINAPI GdipDeletePath(GpPath *);
int WINAPI GdipAddPathLine(GpPath *, float, float, float, float);
int WINAPI GdipAddPathArc(GpPath *, float, float, float, float, float, float);
int WINAPI GdipAddPathBezier(GpPath *, float, float, float, float,
    float, float, float, float);
int WINAPI GdipClosePathFigure(GpPath *);
int WINAPI GdipStartPathFigure(GpPath *);
int WINAPI GdipFillPath(GpGraphics *, GpBrush *, GpPath *);
int WINAPI GdipDrawPath(GpGraphics *, GpPen *, GpPath *);
int WINAPI GdipGetPathWorldBounds(GpPath *, DialRect *, const void *, const GpPen *);
int WINAPI GdipFlush(GpGraphics *, int);
int WINAPI GdipCreateSolidFill(DWORD, GpBrush **);
int WINAPI GdipCreateLineBrush(const DialPoint *, const DialPoint *,
    DWORD, DWORD, int, GpBrush **);
int WINAPI GdipSetLinePresetBlend(GpBrush *, const DWORD *, const float *, int);
int WINAPI GdipDeleteBrush(GpBrush *);
int WINAPI GdipCreatePen1(DWORD, float, int, GpPen **);
int WINAPI GdipSetPenStartCap(GpPen *, int);
int WINAPI GdipSetPenEndCap(GpPen *, int);
int WINAPI GdipSetPenLineJoin(GpPen *, int);
int WINAPI GdipDeletePen(GpPen *);
int WINAPI GdipDrawLines(GpGraphics *, GpPen *, const DialPoint *, int);
int WINAPI GdipFillEllipse(GpGraphics *, GpBrush *, float, float, float, float);
int WINAPI GdipDrawEllipse(GpGraphics *, GpPen *, float, float, float, float);
int WINAPI GdipCreateFontFamilyFromName(const WCHAR *, void *, GpFontFamily **);
int WINAPI GdipGetGenericFontFamilySansSerif(GpFontFamily **);
int WINAPI GdipDeleteFontFamily(GpFontFamily *);
int WINAPI GdipCreateFont(const GpFontFamily *, float, int, int, GpFont **);
int WINAPI GdipDeleteFont(GpFont *);
int WINAPI GdipCreateStringFormat(int, WORD, GpStringFormat **);
int WINAPI GdipSetStringFormatAlign(GpStringFormat *, int);
int WINAPI GdipSetStringFormatLineAlign(GpStringFormat *, int);
int WINAPI GdipSetStringFormatTrimming(GpStringFormat *, int);
int WINAPI GdipDeleteStringFormat(GpStringFormat *);
int WINAPI GdipDrawString(GpGraphics *, const WCHAR *, int, const GpFont *,
    const DialRect *, const GpStringFormat *, const GpBrush *);
#endif
