/*
 * CLCL
 *
 * gdip.cpp
 *
 * Copyright (C) 1996-2019 by Ohno Tomoaki. All rights reserved.
 *		https://www.nakka.com/
 *		nakka@nakka.com
 */

 /* Include Files */

#define _INC_OLE
#include <windows.h>
#undef  _INC_OLE
#include <gdiplus.h>

using namespace Gdiplus;

#include "gdip.h"
extern "C" {
#include "Memory.h"
}

#pragma comment(lib, "gdiplus.lib")

/* Define */

/* Global Variables */
static HMODULE hModuleThread;
static ULONG_PTR gdiplusToken;

typedef Status(WINAPI* pGdipCreateBitmapFromHBITMAP)(HBITMAP, HPALETTE, GpBitmap**);
typedef Status(WINAPI* pGdipSaveImageToFile)(GpBitmap*, const WCHAR*, const CLSID*, const Gdiplus::EncoderParameters*);
typedef Status(WINAPI* pGdipDisposeImage)(GpBitmap*);

static pGdipCreateBitmapFromHBITMAP _GdipCreateBitmapFromHBITMAP;
static pGdipSaveImageToFile _GdipSaveImageToFile;
static pGdipDisposeImage _GdipDisposeImage;

/* Local Function Prototypes */

/*
 * init_gdip - GDI+の初期化
 */
void init_gdip()
{
	hModuleThread = LoadLibrary(TEXT("gdiplus.dll"));
	_GdipCreateBitmapFromHBITMAP = (pGdipCreateBitmapFromHBITMAP)GetProcAddress(hModuleThread, "GdipCreateBitmapFromHBITMAP");
	_GdipSaveImageToFile = (pGdipSaveImageToFile)GetProcAddress(hModuleThread, "GdipSaveImageToFile");
	_GdipDisposeImage = (pGdipDisposeImage)GetProcAddress(hModuleThread, "GdipDisposeImage");

	GdiplusStartupInput gdiplusStartupInput;
	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, 0);
}

/*
 * shutdown_gdip - GDI+の終了処理
 */
void shutdown_gdip()
{
	GdiplusShutdown(gdiplusToken);
	FreeLibrary(hModuleThread);
}

/*
 * save_jpeg - HBITMAPをJPEGで保存
 */
int save_jpeg(HBITMAP hBmp, LPCWSTR lpszFilename, ULONG uQuality)
{
	GpBitmap* pBitmap = NULL;
	CLSID imageCLSID;
	Gdiplus::EncoderParameters encoderParams;

	if (_GdipCreateBitmapFromHBITMAP == NULL || _GdipSaveImageToFile == NULL || _GdipDisposeImage == NULL) {
		return 0;
	}
	_GdipCreateBitmapFromHBITMAP(hBmp, NULL, &pBitmap);
	CLSIDFromString(L"{557CF401-1A04-11D3-9A73-0000F81EF32E}", &imageCLSID);	//jpeg
	encoderParams.Count = 1;
	encoderParams.Parameter[0].NumberOfValues = 1;
	encoderParams.Parameter[0].Guid = Gdiplus::EncoderQuality;
	encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
	encoderParams.Parameter[0].Value = &uQuality;
	_GdipSaveImageToFile(pBitmap, lpszFilename, &imageCLSID, &encoderParams);
	_GdipDisposeImage(pBitmap);
	return 1;
}

/*
 * save_png - HBITMAPをPNGで保存
 */
int save_png(HBITMAP hBmp, LPCWSTR lpszFilename)
{
	GpBitmap* pBitmap = NULL;
	CLSID imageCLSID;
	Status status;

	if (_GdipCreateBitmapFromHBITMAP == NULL || _GdipSaveImageToFile == NULL || _GdipDisposeImage == NULL) {
		return 0;
	}
	status = _GdipCreateBitmapFromHBITMAP(hBmp, NULL, &pBitmap);
	if (status != Ok || pBitmap == NULL) return 0;
	CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &imageCLSID);	//png
	status = _GdipSaveImageToFile(pBitmap, lpszFilename, &imageCLSID, NULL);
	_GdipDisposeImage(pBitmap);
	return status == Ok;
}

/* PNG storage uses memory streams; callers own the returned memory/bitmap. */
BYTE *bitmap_to_png(HBITMAP bitmap, DWORD *size)
{
	if (bitmap == NULL || size == NULL) return NULL;
	*size = 0;
	BITMAP source = {};
	if (!GetObject(bitmap, sizeof(source), &source) || source.bmWidth <= 0 ||
		source.bmHeight <= 0 || source.bmPlanes != 1 ||
		(source.bmBitsPixel != 24 && source.bmBitsPixel != 32)) return NULL;
	PixelFormat format = source.bmBitsPixel == 32 ? PixelFormat32bppARGB : PixelFormat24bppRGB;
	Bitmap image(source.bmWidth, source.bmHeight, format);
	if (image.GetLastStatus() != Ok) return NULL;
	Rect rect(0, 0, source.bmWidth, source.bmHeight);
	BitmapData pixels = {};
	if (image.LockBits(&rect, ImageLockModeWrite, format, &pixels) != Ok) return NULL;
	BITMAPINFO info = {};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = source.bmWidth;
	info.bmiHeader.biHeight = -source.bmHeight;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = source.bmBitsPixel;
	HDC dc = GetDC(NULL);
	int rows = dc != NULL ? GetDIBits(dc, bitmap, 0, source.bmHeight,
		pixels.Scan0, &info, DIB_RGB_COLORS) : 0;
	if (dc != NULL) ReleaseDC(NULL, dc);
	image.UnlockBits(&pixels);
	if (rows != source.bmHeight) return NULL;
	IStream *stream = NULL;
	if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &stream))) return NULL;
	CLSID encoder;
	CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &encoder);
	BYTE *result = NULL;
	STATSTG stat = {};
	if (image.Save(stream, &encoder, NULL) == Ok &&
		SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) &&
		stat.cbSize.HighPart == 0 && stat.cbSize.LowPart > 0 &&
		stat.cbSize.LowPart <= MAXLONG) {
		LARGE_INTEGER start = {};
		ULONG read = 0;
		result = (BYTE *)mem_alloc(stat.cbSize.LowPart);
		if (result != NULL) {
			if (SUCCEEDED(stream->Seek(start, STREAM_SEEK_SET, NULL)) &&
				SUCCEEDED(stream->Read(result, stat.cbSize.LowPart, &read)) &&
				read == stat.cbSize.LowPart) *size = read;
			else mem_free((void **)&result);
		}
	}
	stream->Release();
	return result;
}

HBITMAP png_to_bitmap(const BYTE *data, DWORD size)
{
	if (data == NULL || size == 0) return NULL;
	IStream *stream = NULL;
	if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &stream))) return NULL;
	HBITMAP result = NULL;
	ULONG written = 0;
	LARGE_INTEGER start = {};
	if (SUCCEEDED(stream->Write(data, size, &written)) && written == size &&
		SUCCEEDED(stream->Seek(start, STREAM_SEEK_SET, NULL))) {
		// GDI+ requires the stream to outlive the image.
		Bitmap image(stream);
		if (image.GetLastStatus() == Ok && image.GetWidth() <= MAXLONG &&
			image.GetHeight() <= MAXLONG) {
			// GetHBITMAP composites alpha, changing bytes used by duplicate detection.
			// Copy decoded pixels directly, including the reserved byte in CF_BITMAP.
			PixelFormat format = image.GetPixelFormat() == PixelFormat24bppRGB ?
				PixelFormat24bppRGB : PixelFormat32bppARGB;
			Rect rect(0, 0, image.GetWidth(), image.GetHeight());
			BitmapData pixels = {};
			if (image.LockBits(&rect, ImageLockModeRead, format, &pixels) == Ok) {
				BITMAPINFO info = {};
				info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				info.bmiHeader.biWidth = rect.Width;
				info.bmiHeader.biHeight = -rect.Height;
				info.bmiHeader.biPlanes = 1;
				info.bmiHeader.biBitCount = format == PixelFormat24bppRGB ? 24 : 32;
				void *bits = NULL;
				result = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0);
				if (result != NULL) {
					SIZE_T stride = (((SIZE_T)rect.Width * info.bmiHeader.biBitCount + 31) / 32) * 4;
					for (int y = 0; y < rect.Height; y++)
						CopyMemory((BYTE *)bits + y * stride,
							(BYTE *)pixels.Scan0 + (ptrdiff_t)y * pixels.Stride, stride);
				}
				image.UnlockBits(&pixels);
			}
		}
	}
	stream->Release();
	return result;
}

/*
 * draw_image_file - 画像ファイルをHBITMAPに変換
 */
HBITMAP image_to_bitmap(HDC hdc, LPCWSTR lpszFilename)
{
	Gdiplus::Image* image = new Gdiplus::Image(lpszFilename);

	HDC mdc = ::CreateCompatibleDC(hdc);
	HBITMAP hbmp = ::CreateCompatibleBitmap(hdc, image->GetWidth(), image->GetHeight());
	HBITMAP hOldBmp = (HBITMAP)::SelectObject(mdc, hbmp);

	Gdiplus::Graphics* graphics = new Gdiplus::Graphics(mdc);
	graphics->DrawImage(image, 0, 0, image->GetWidth(), image->GetHeight());
	delete(image);
	delete(graphics);

	::SelectObject(mdc, hOldBmp);
	::DeleteDC(mdc);
	return hbmp;
}
/* End of source */
