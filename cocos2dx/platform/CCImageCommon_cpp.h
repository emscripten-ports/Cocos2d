/****************************************************************************
Copyright (c) 2010 cocos2d-x.org

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/


#include "CCImage.h"

#include <string>
#include <ctype.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#endif // __EMSCRIPTEN__

extern "C"
{
#include "png.h"
#include "tiffio.h"
#include "etc1.h"
#include "jpeglib.h"
}
#if defined(__native_client__) || defined(EMSCRIPTEN)
// TODO(sbc): I'm pretty sure all platforms should be including
// webph headers in this way.
#include "webp/decode.h"
#else
#include "decode.h"
#endif

#include "ccMacros.h"
#include "CCCommon.h"
#include "CCStdC.h"
#include "CCFileUtils.h"
#include "CCConfiguration.h"
#include "support/ccUtils.h"
#include "support/zip_support/ZipUtils.h"
#if (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
#include "platform/android/CCFileUtilsAndroid.h"
#endif

NS_CC_BEGIN

//////////////////////////////////////////////////////////////////////////
//struct and data for pvr structure
namespace
{
    static const int PVR_TEXTURE_FLAG_TYPE_MASK = 0xff;
    
    // Values taken from PVRTexture.h from http://www.imgtec.com
    enum class PVR2TextureFlag{
        Mipmap         = (1<<8),        // has mip map levels
        Twiddle        = (1<<9),        // is twiddled
        Bumpmap        = (1<<10),       // has normals encoded for a bump map
        Tiling         = (1<<11),       // is bordered for tiled pvr
        Cubemap        = (1<<12),       // is a cubemap/skybox
        FalseMipCol    = (1<<13),       // are there false colored MIP levels
        Volume         = (1<<14),       // is this a volume texture
        Alpha          = (1<<15),       // v2.1 is there transparency info in the texture
        VerticalFlip   = (1<<16),       // v2.1 is the texture vertically flipped
    };
    
    enum class PVR3TextureFlag
    {
        PremultipliedAlpha	= (1<<1)	// has premultiplied alpha
    };
    
    static const char gPVRTexIdentifier[5] = "PVR!";
    
    // v2
    enum class PVR2TexturePixelFormat : unsigned char
    {
        RGBA4444 = 0x10,
        RGBA5551,
        RGBA8888,
        RGB565,
        RGB555,          // unsupported
        RGB888,
        I8,
        AI88,
        PVRTC2BPP_RGBA,
        PVRTC4BPP_RGBA,
        BGRA8888,
        A8,
        };
        
        // v3
        enum class PVR3TexturePixelFormat : uint64_t
        {
            PVRTC2BPP_RGB  = 0ULL,
            PVRTC2BPP_RGBA = 1ULL,
            PVRTC4BPP_RGB  = 2ULL,
            PVRTC4BPP_RGBA = 3ULL,
            
            BGRA8888       = 0x0808080861726762ULL,
            RGBA8888       = 0x0808080861626772ULL,
            RGBA4444       = 0x0404040461626772ULL,
            RGBA5551       = 0x0105050561626772ULL,
            RGB565         = 0x0005060500626772ULL,
            RGB888         = 0x0008080800626772ULL,
            A8             = 0x0000000800000061ULL,
            L8             = 0x000000080000006cULL,
            LA88           = 0x000008080000616cULL,
        };
        
        
        // v2
        typedef const std::map<PVR2TexturePixelFormat, Texture2D::PixelFormat> _pixel2_formathash;
        
        static const _pixel2_formathash::value_type v2_pixel_formathash_value[] =
        {
            _pixel2_formathash::value_type(PVR2TexturePixelFormat::BGRA8888,	    Texture2D::PixelFormat::BGRA8888),
            _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGBA8888,	    Texture2D::PixelFormat::RGBA8888),
            _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGBA4444,	    Texture2D::PixelFormat::RGBA4444),
            _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGBA5551,	    Texture2D::PixelFormat::RGB5A1),
            _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGB565,	    Texture2D::PixelFormat::RGB565),
            _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGB888,	    Texture2D::PixelFormat::RGB888),
            _pixel2_formathash::value_type(PVR2TexturePixelFormat::A8,	        Texture2D::PixelFormat::A8),
            _pixel2_formathash::value_type(PVR2TexturePixelFormat::I8,	        Texture2D::PixelFormat::I8),
            _pixel2_formathash::value_type(PVR2TexturePixelFormat::AI88,	        Texture2D::PixelFormat::AI88),
            
#ifdef GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG
            _pixel2_formathash::value_type(PVR2TexturePixelFormat::PVRTC2BPP_RGBA,	    Texture2D::PixelFormat::PVRTC2A),
            _pixel2_formathash::value_type(PVR2TexturePixelFormat::PVRTC4BPP_RGBA,	    Texture2D::PixelFormat::PVRTC4A),
#endif
        };
        
        static const int PVR2_MAX_TABLE_ELEMENTS = sizeof(v2_pixel_formathash_value) / sizeof(v2_pixel_formathash_value[0]);
        static const _pixel2_formathash v2_pixel_formathash(v2_pixel_formathash_value, v2_pixel_formathash_value + PVR2_MAX_TABLE_ELEMENTS);
        
        // v3
        typedef const std::map<PVR3TexturePixelFormat, Texture2D::PixelFormat> _pixel3_formathash;
        static _pixel3_formathash::value_type v3_pixel_formathash_value[] =
        {
            _pixel3_formathash::value_type(PVR3TexturePixelFormat::BGRA8888,	Texture2D::PixelFormat::BGRA8888),
            _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGBA8888,	Texture2D::PixelFormat::RGBA8888),
            _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGBA4444,	Texture2D::PixelFormat::RGBA4444),
            _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGBA5551,	Texture2D::PixelFormat::RGB5A1),
            _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGB565,	    Texture2D::PixelFormat::RGB565),
            _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGB888,	    Texture2D::PixelFormat::RGB888),
            _pixel3_formathash::value_type(PVR3TexturePixelFormat::A8,	        Texture2D::PixelFormat::A8),
            _pixel3_formathash::value_type(PVR3TexturePixelFormat::L8,	        Texture2D::PixelFormat::I8),
            _pixel3_formathash::value_type(PVR3TexturePixelFormat::LA88,	    Texture2D::PixelFormat::AI88),
            
#ifdef GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG
            _pixel3_formathash::value_type(PVR3TexturePixelFormat::PVRTC2BPP_RGB,	    Texture2D::PixelFormat::PVRTC2),
            _pixel3_formathash::value_type(PVR3TexturePixelFormat::PVRTC2BPP_RGBA,	    Texture2D::PixelFormat::PVRTC2A),
            _pixel3_formathash::value_type(PVR3TexturePixelFormat::PVRTC4BPP_RGB,	    Texture2D::PixelFormat::PVRTC4),
            _pixel3_formathash::value_type(PVR3TexturePixelFormat::PVRTC4BPP_RGBA,	    Texture2D::PixelFormat::PVRTC4A),
#endif
        };
        
        static const int PVR3_MAX_TABLE_ELEMENTS = sizeof(v3_pixel_formathash_value) / sizeof(v3_pixel_formathash_value[0]);
        
        static const _pixel3_formathash v3_pixel_formathash(v3_pixel_formathash_value, v3_pixel_formathash_value + PVR3_MAX_TABLE_ELEMENTS);
        
        typedef struct _PVRTexHeader
        {
            unsigned int headerLength;
            unsigned int height;
            unsigned int width;
            unsigned int numMipmaps;
            unsigned int flags;
            unsigned int dataLength;
            unsigned int bpp;
            unsigned int bitmaskRed;
            unsigned int bitmaskGreen;
            unsigned int bitmaskBlue;
            unsigned int bitmaskAlpha;
            unsigned int pvrTag;
            unsigned int numSurfs;
        } PVRv2TexHeader;
        
#ifdef _MSC_VER
#pragma pack(push,1)
#endif
        typedef struct {
            uint32_t version;
            uint32_t flags;
            uint64_t pixelFormat;
            uint32_t colorSpace;
            uint32_t channelType;
            uint32_t height;
            uint32_t width;
            uint32_t depth;
            uint32_t numberOfSurfaces;
            uint32_t numberOfFaces;
            uint32_t numberOfMipmaps;
            uint32_t metadataLength;
#ifdef _MSC_VER
        } PVRv3TexHeader;
#pragma pack(pop)
#else
    } __attribute__((packed)) PVRv3TexHeader;
#endif
}
//pvr structure end
//////////////////////////////////////////////////////////////////////////

namespace {
    typedef struct 
    {
        unsigned char* data;
        int size;
        int offset;
    }tImageSource;
    
    static void pngReadCallback(png_structp png_ptr, png_bytep data, png_size_t length)
    {
        tImageSource* isource = (tImageSource*)png_get_io_ptr(png_ptr);
        
        if((int)(isource->offset + length) <= isource->size)
        {
            memcpy(data, isource->data+isource->offset, length);
            isource->offset += length;
        }
        else
        {
            png_error(png_ptr, "pngReaderCallback failed");
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Implement Image
//////////////////////////////////////////////////////////////////////////

Image::Image()
: _data(0)
, _dataLen(0)
, _width(0)
, _height(0)
, _fileType(Format::UNKOWN)
, _renderFormat(Texture2D::PixelFormat::NONE)
, _preMulti(false)
, _hasPremultipliedAlpha(true)
, _numberOfMipmaps(0)
{

}

Image::~Image()
{
    CC_SAFE_DELETE_ARRAY(_data);
}

bool Image::initWithImageFile(const char * strPath)
{
    bool bRet = false;
    std::string fullPath = FileUtils::getInstance()->fullPathForFilename(strPath);

    if (access(fullPath.c_str(), F_OK ) == -1) {
        printf("WARN: Image file '%s' doesn`t exists\n", fullPath.c_str());
        return false;
    }

// @caiiiycuk: libpng is better, because it can handle webp
#ifdef _EMSCRIPTEN
    // Emscripten includes a re-implementation of SDL that uses HTML5 canvas
    // operations underneath. Consequently, loading images via IMG_Load (an SDL
    // API) will be a lot faster than running libpng et al as compiled with
    // Emscripten.
    SDL_Surface *iSurf = IMG_Load(fullPath.c_str());

    if (!iSurf) {
        printf("ERROR: Unabel to load image '%s' (unable to IMG_Load)\n", fullPath.c_str());
        return false;
    }

    int size = 4 * (iSurf->w * iSurf->h);
    bRet = initWithRawData((void*)iSurf->pixels, size, iSurf->w, iSurf->h, 8, true);

    unsigned int *tmp = (unsigned int *)_data;
    int nrPixels = iSurf->w * iSurf->h;
    for(int i = 0; i < nrPixels; i++)
    {
        unsigned char *p = _data + i * 4;
        tmp[i] = CC_RGB_PREMULTIPLY_ALPHA( p[0], p[1], p[2], p[3] );
    }

    SDL_FreeSurface(iSurf);
#else
    unsigned long bufferLen = 0;
    unsigned char* buffer = nullptr;
    
    //detecgt and unzip the compress file
    if (ZipUtils::ccIsCCZFile(fullPath.c_str()))
    {
        bufferLen = ZipUtils::ccInflateCCZFile(fullPath.c_str(), &buffer);
    }else if (ZipUtils::ccIsGZipFile(fullPath.c_str()))
    {
        bufferLen = ZipUtils::ccInflateGZipFile(fullPath.c_str(), &buffer);
    }
    else
    {
        buffer = FileUtils::getInstance()->getFileData(fullPath.c_str(), "rb", &bufferLen);
    }

    if (buffer != NULL && bufferLen > 0)
    {
        bRet = initWithImageData(buffer, bufferLen);
    }

    CC_SAFE_DELETE_ARRAY(buffer);
#endif // __EMSCRIPTEN__

    return bRet;
}

bool Image::initWithImageFileThreadSafe(const char *fullpath)
{
    bool bRet = false;
    unsigned long dataLen = 0;
#if (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
    FileUtilsAndroid *fileUitls = (FileUtilsAndroid*)FileUtils::getInstance();
    unsigned char *pBuffer = fileUitls->getFileDataForAsync(fullpath, "rb", &dataLen);
#else
    unsigned char *pBuffer = FileUtils::getInstance()->getFileData(fullpath, "rb", &dataLen);
#endif
    if (pBuffer != NULL && dataLen > 0)
    {
        bRet = initWithImageData(pBuffer, dataLen);
    }
    CC_SAFE_DELETE_ARRAY(pBuffer);
    return bRet;
}

bool Image::initWithImageData(const void * data, int dataLen)
{
    do 
    {
        CC_BREAK_IF(! data || dataLen <= 0);

        _fileType = detectFormat(data, dataLen);

        switch (_fileType)
        {
        case Format::PNG:
            return initWithPngData(data, dataLen);
        case Format::JPG:
            return initWithJpgData(data, dataLen);
        case Format::TIFF:
            return initWithTiffData(data, dataLen);
        case Format::WEBP:
        case Format::WEBP_HALF:
            return initWithWebpData(data, dataLen, _fileType == Format::WEBP_HALF);
        case Format::PVR:
            return initWithPVRData(data, dataLen);
        case Format::ETC:
            return initWithETCData(data, dataLen);
        default:
            CCAssert(false, "unsupport image format!");
            return false;
        }
    } while (0);
    
    return false;
}

bool Image::isPng(const void *data, int dataLen)
{
    if (dataLen <= 8)
    {
        return false;
    }

    static const unsigned char PNG_SIGNATURE[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

    return memcmp(PNG_SIGNATURE, data, sizeof(PNG_SIGNATURE)) == 0;
}


bool Image::isEtc(const void *data, int dataLen)
{
    return etc1_pkm_is_valid((etc1_byte*)data) ? true : false;
}

bool Image::isJpg(const void *data, int dataLen)
{
    if (dataLen <= 4)
    {
        return false;
    }

    static const unsigned char JPG_SOI[] = {0xFF, 0xD8};

    return memcmp(data, JPG_SOI, 2) == 0;
}

bool Image::isTiff(const void *data, int dataLen)
{
    if (dataLen <= 4)
    {
        return false;
    }

    static const char* TIFF_II = "II";
    static const char* TIFF_MM = "MM";

    return (memcmp(data, TIFF_II, 2) == 0 && *(static_cast<const unsigned char*>(data) + 2) == 42 && *(static_cast<const unsigned char*>(data) + 3) == 0) ||
        (memcmp(data, TIFF_MM, 2) == 0 && *(static_cast<const unsigned char*>(data) + 2) == 0 && *(static_cast<const unsigned char*>(data) + 3) == 42);
}

bool Image::isWebp(const void *data, int dataLen)
{
    if (dataLen <= 12)
    {
        return false;
    }

    static const char* WEBP_RIFF = "RIFF";
    static const char* WEBP_WEBP = "WEBP";

    return memcmp(data, WEBP_RIFF, 4) == 0 
        && memcmp(static_cast<const unsigned char*>(data) + 8, WEBP_WEBP, 4) == 0;
}

bool Image::isPvr(const void *data, int dataLen)
{
    if (dataLen < sizeof(PVRv2TexHeader) || dataLen < sizeof(PVRv3TexHeader))
    {
        return false;
    }
    
    const PVRv2TexHeader* headerv2 = static_cast<const PVRv2TexHeader*>(data);
    const PVRv3TexHeader* headerv3 = static_cast<const PVRv3TexHeader*>(data);
    
    return memcmp(&headerv2->pvrTag, gPVRTexIdentifier, strlen(gPVRTexIdentifier)) == 0 || CC_SWAP_INT32_BIG_TO_HOST(headerv3->version) == 0x50565203;
}


Image::Format Image::detectFormat(const void* data, int dataLen)
{
    if (isPng(data, dataLen))
    {
        return Format::PNG;
    }else if (isJpg(data, dataLen))
    {
        return Format::JPG;
    }else if (isTiff(data, dataLen))
    {
        return Format::TIFF;
    }else if (isWebp(data, dataLen))
    {
        if (((const char*) data)[dataLen - 2] == 'h' &&
                ((const char*) data)[dataLen - 1] == 's') {
            return Format::WEBP_HALF;
        }
        return Format::WEBP;
    }else if (isPvr(data, dataLen))
    {
        return Format::PVR;
    }else if (isEtc(data, dataLen))
    {
        return Format::ETC;
    }
    
    else
    {
        return Format::UNKOWN;
    }
}

int Image::getBitPerPixel()
{
    return Texture2D::getPixelFormatInfoMap().at(_renderFormat).bpp;
}

bool Image::hasAlpha()
{
    return Texture2D::getPixelFormatInfoMap().at(_renderFormat).alpha;
}

bool Image::isCompressed()
{
    return Texture2D::getPixelFormatInfoMap().at(_renderFormat).compressed;
}

namespace
{
/*
 * ERROR HANDLING:
 *
 * The JPEG library's standard error handler (jerror.c) is divided into
 * several "methods" which you can override individually.  This lets you
 * adjust the behavior without duplicating a lot of code, which you might
 * have to update with each future release.
 *
 * We override the "error_exit" method so that control is returned to the
 * library's caller when a fatal error occurs, rather than calling exit()
 * as the standard error_exit method does.
 *
 * We use C's setjmp/longjmp facility to return control.  This means that the
 * routine which calls the JPEG library must first execute a setjmp() call to
 * establish the return point.  We want the replacement error_exit to do a
 * longjmp().  But we need to make the setjmp buffer accessible to the
 * error_exit routine.  To do this, we make a private extension of the
 * standard JPEG error handler object.  (If we were using C++, we'd say we
 * were making a subclass of the regular error handler.)
 *
 * Here's the extended error handler struct:
 */
    struct MyErrorMgr
    {
        struct jpeg_error_mgr pub;	/* "public" fields */
        jmp_buf setjmp_buffer;	/* for return to caller */
    };
    
    typedef struct MyErrorMgr * MyErrorPtr;
    
    /*
     * Here's the routine that will replace the standard error_exit method:
     */
    
    METHODDEF(void)
    myErrorExit(j_common_ptr cinfo)
    {
        /* cinfo->err really points to a MyErrorMgr struct, so coerce pointer */
        MyErrorPtr myerr = (MyErrorPtr) cinfo->err;
        
        /* Always display the message. */
        /* We could postpone this until after returning, if we chose. */
        /* internal message function cann't show error message in some platforms, so we rewrite it here.
         * edit it if has version confilict.
         */
        //(*cinfo->err->output_message) (cinfo);
        char buffer[JMSG_LENGTH_MAX];
        (*cinfo->err->format_message) (cinfo, buffer);
        CCLOG("jpeg error: %s", buffer);
        
        /* Return control to the setjmp point */
        longjmp(myerr->setjmp_buffer, 1);
    }
}

bool Image::initWithJpgData(const void * data, int dataLen)
{
    /* these are standard libjpeg structures for reading(decompression) */
    struct jpeg_decompress_struct cinfo;
    /* We use our private extension JPEG error handler.
	 * Note that this struct must live as long as the main JPEG parameter
	 * struct, to avoid dangling-pointer problems.
	 */
	struct MyErrorMgr jerr;
    /* libjpeg data structure for storing one row, that is, scanline of an image */
    JSAMPROW row_pointer[1] = {0};
    unsigned long location = 0;
    unsigned int i = 0;

    bool bRet = false;
    do 
    {
        /* We set up the normal JPEG error routines, then override error_exit. */
		cinfo.err = jpeg_std_error(&jerr.pub);
		jerr.pub.error_exit = myErrorExit;
		/* Establish the setjmp return context for MyErrorExit to use. */
		if (setjmp(jerr.setjmp_buffer)) {
			/* If we get here, the JPEG code has signaled an error.
			 * We need to clean up the JPEG object, close the input file, and return.
			 */
			jpeg_destroy_decompress(&cinfo);
			break;
		}

        /* setup decompression process and source, then read JPEG header */
        jpeg_create_decompress( &cinfo );

#ifndef CC_TARGET_QT5
        jpeg_mem_src( &cinfo, (unsigned char *) data, dataLen );
#endif /* CC_TARGET_QT5 */

        /* reading the image header which contains image information */
#if (JPEG_LIB_VERSION >= 90)
        // libjpeg 0.9 adds stricter types.
        jpeg_read_header( &cinfo, TRUE );
#else
        jpeg_read_header( &cinfo, true );
#endif

        // we only support RGB or grayscale
        if (cinfo.jpeg_color_space == JCS_GRAYSCALE)
        {
            _renderFormat = Texture2D::PixelFormat::I8;
        }else
        {
            cinfo.out_color_space = JCS_RGB;
            _renderFormat = Texture2D::PixelFormat::RGB888;
        }

        /* Start decompression jpeg here */
        jpeg_start_decompress( &cinfo );

        /* init image info */
        _width  = (short)(cinfo.output_width);
        _height = (short)(cinfo.output_height);
        _preMulti = false;
        row_pointer[0] = new unsigned char[cinfo.output_width*cinfo.output_components];
        CC_BREAK_IF(! row_pointer[0]);

        _dataLen = cinfo.output_width*cinfo.output_height*cinfo.output_components;
        _data = new unsigned char[_dataLen];
        CC_BREAK_IF(! _data);

        /* now actually read the jpeg into the raw buffer */
        /* read one scan line at a time */
        while( cinfo.output_scanline < cinfo.output_height )
        {
            jpeg_read_scanlines( &cinfo, row_pointer, 1 );
            for( i=0; i<cinfo.output_width*cinfo.output_components;i++) 
            {
                _data[location++] = row_pointer[0][i];
            }
        }

		/* When read image file with broken data, jpeg_finish_decompress() may cause error.
		 * Besides, jpeg_destroy_decompress() shall deallocate and release all memory associated
		 * with the decompression object.
		 * So it doesn't need to call jpeg_finish_decompress().
		 */
		//jpeg_finish_decompress( &cinfo );
        jpeg_destroy_decompress( &cinfo );
        /* wrap up decompression, destroy objects, free pointers and close open files */        
        bRet = true;
    } while (0);

    CC_SAFE_DELETE_ARRAY(row_pointer[0]);
    return bRet;
}

bool Image::initWithPngData(const void * data, int dataLen)
{
    // length of bytes to check if it is a valid png file
#define PNGSIGSIZE  8
    bool bRet = false;
    png_byte        header[PNGSIGSIZE]   = {0}; 
    png_structp     png_ptr     =   0;
    png_infop       info_ptr    = 0;

    do 
    {
        // png header len is 8 bytes
        CC_BREAK_IF(dataLen < PNGSIGSIZE);

        // check the data is png or not
        memcpy(header, data, PNGSIGSIZE);
        CC_BREAK_IF(png_sig_cmp(header, 0, PNGSIGSIZE));

        // init png_struct
        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
        CC_BREAK_IF(! png_ptr);

        // init png_info
        info_ptr = png_create_info_struct(png_ptr);
        CC_BREAK_IF(!info_ptr);

#if (CC_TARGET_PLATFORM != CC_PLATFORM_BADA && CC_TARGET_PLATFORM != CC_PLATFORM_NACL)
        CC_BREAK_IF(setjmp(png_jmpbuf(png_ptr)));
#endif

        // set the read call back function
        tImageSource imageSource;
        imageSource.data    = (unsigned char*)data;
        imageSource.size    = dataLen;
        imageSource.offset  = 0;
        png_set_read_fn(png_ptr, &imageSource, pngReadCallback);

        // read png header info

        // read png file info
        png_read_info(png_ptr, info_ptr);

        _width = png_get_image_width(png_ptr, info_ptr);
        _height = png_get_image_height(png_ptr, info_ptr);
        png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
        png_uint_32 color_type = png_get_color_type(png_ptr, info_ptr);

        //CCLOG("color type %u", color_type);

        // force palette images to be expanded to 24-bit RGB
        // it may include alpha channel
        if (color_type == PNG_COLOR_TYPE_PALETTE)
        {
            png_set_palette_to_rgb(png_ptr);
        }
        // low-bit-depth grayscale images are to be expanded to 8 bits
        if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        {
            bit_depth = 8;
            png_set_expand_gray_1_2_4_to_8(png_ptr);
        }
        // expand any tRNS chunk data into a full alpha channel
        if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        {
            png_set_tRNS_to_alpha(png_ptr);
        }  
        // reduce images with 16-bit samples to 8 bits
        if (bit_depth == 16)
        {
            png_set_strip_16(png_ptr);            
        } 

        // Expanded earlier for grayscale, now take care of palette and rgb
        if (bit_depth < 8) {
            png_set_packing(png_ptr);
        }
        // update info
        png_read_update_info(png_ptr, info_ptr);
        bit_depth = png_get_bit_depth(png_ptr, info_ptr);
        color_type = png_get_color_type(png_ptr, info_ptr);

        switch (color_type)
        {
        case PNG_COLOR_TYPE_GRAY:
            _renderFormat = Texture2D::PixelFormat::I8;
            break;
        case PNG_COLOR_TYPE_GRAY_ALPHA:
            _renderFormat = Texture2D::PixelFormat::AI88;
            break;
        case PNG_COLOR_TYPE_RGB:
            _renderFormat = Texture2D::PixelFormat::RGB888;
            break;
        case PNG_COLOR_TYPE_RGB_ALPHA:
            _renderFormat = Texture2D::PixelFormat::RGBA8888;
            break;
        default:
            break;
        }

        // read png data
        png_uint_32 rowbytes;
        png_bytep* row_pointers = (png_bytep*)malloc( sizeof(png_bytep) * _height );

        rowbytes = png_get_rowbytes(png_ptr, info_ptr);

        _dataLen = rowbytes * _height;
        _data = new unsigned char[_dataLen];
        CC_BREAK_IF(!_data);

        for (unsigned short i = 0; i < _height; ++i)
        {
            row_pointers[i] = _data + i*rowbytes;
        }
        png_read_image(png_ptr, row_pointers);

        png_read_end(png_ptr, NULL);

        _preMulti = false;

        CC_SAFE_FREE(row_pointers);

        bRet = true;
    } while (0);

    if (png_ptr)
    {
        png_destroy_read_struct(&png_ptr, (info_ptr) ? &info_ptr : 0, 0);
    }
    return bRet;
}

namespace
{
    static tmsize_t tiffReadProc(thandle_t fd, void* buf, tmsize_t size)
    {
        tImageSource* isource = (tImageSource*)fd;
        uint8* ma;
        uint64 mb;
        unsigned long n;
        unsigned long o;
        tmsize_t p;
        ma=(uint8*)buf;
        mb=size;
        p=0;
        while (mb>0)
        {
            n=0x80000000UL;
            if ((uint64)n>mb)
            n=(unsigned long)mb;
            
            
            if((int)(isource->offset + n) <= isource->size)
            {
                memcpy(ma, isource->data+isource->offset, n);
                isource->offset += n;
                o = n;
            }
            else
            {
                return 0;
            }
            
            ma+=o;
            mb-=o;
            p+=o;
            if (o!=n)
            {
                break;
            }
        }
        return p;
    }
    
    static tmsize_t tiffWriteProc(thandle_t fd, void* buf, tmsize_t size)
    {
        CC_UNUSED_PARAM(fd);
        CC_UNUSED_PARAM(buf);
        CC_UNUSED_PARAM(size);
        return 0;
    }
    
    
    static uint64 tiffSeekProc(thandle_t fd, uint64 off, int whence)
    {
        tImageSource* isource = (tImageSource*)fd;
        uint64 ret = -1;
        do
        {
            if (whence == SEEK_SET)
            {
                CC_BREAK_IF(off >= (uint64)isource->size);
                ret = isource->offset = (uint32)off;
            }
            else if (whence == SEEK_CUR)
            {
                CC_BREAK_IF(isource->offset + off >= (uint64)isource->size);
                ret = isource->offset += (uint32)off;
            }
            else if (whence == SEEK_END)
            {
                CC_BREAK_IF(off >= (uint64)isource->size);
                ret = isource->offset = (uint32)(isource->size-1 - off);
            }
            else
            {
                CC_BREAK_IF(off >= (uint64)isource->size);
                ret = isource->offset = (uint32)off;
            }
        } while (0);
        
        return ret;
    }
    
    static uint64 tiffSizeProc(thandle_t fd)
    {
        tImageSource* pImageSrc = (tImageSource*)fd;
        return pImageSrc->size;
    }
    
    static int tiffCloseProc(thandle_t fd)
    {
        CC_UNUSED_PARAM(fd);
        return 0;
    }
    
    static int tiffMapProc(thandle_t fd, void** pbase, toff_t* psize)
    {
        CC_UNUSED_PARAM(fd);
        CC_UNUSED_PARAM(pbase);
        CC_UNUSED_PARAM(psize);
        return 0;
    }
    
    static void tiffUnmapProc(thandle_t fd, void* base, toff_t size)
    {
        CC_UNUSED_PARAM(fd);
        CC_UNUSED_PARAM(base);
        CC_UNUSED_PARAM(size);
    }
}

bool Image::initWithTiffData(const void* data, int dataLen)
{
    bool bRet = false;
    do 
    {
        // set the read call back function
        tImageSource imageSource;
        imageSource.data    = (unsigned char*)data;
        imageSource.size    = dataLen;
        imageSource.offset  = 0;

        TIFF* tif = TIFFClientOpen("file.tif", "r", (thandle_t)&imageSource, 
            tiffReadProc, tiffWriteProc,
            tiffSeekProc, tiffCloseProc, tiffSizeProc,
            tiffMapProc,
            tiffUnmapProc);

        CC_BREAK_IF(NULL == tif);

        uint32 w = 0, h = 0;
        uint16 bitsPerSample = 0, samplePerPixel = 0, planarConfig = 0;
        size_t npixels = 0;
        
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
        TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplePerPixel);
        TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &planarConfig);

        npixels = w * h;
        
        _renderFormat = Texture2D::PixelFormat::RGBA8888;
        _width = w;
        _height = h;

        _dataLen = npixels * sizeof (uint32);
        _data = new unsigned char[_dataLen];

        uint32* raster = (uint32*) _TIFFmalloc(npixels * sizeof (uint32));
        if (raster != NULL) 
        {
           if (TIFFReadRGBAImageOriented(tif, w, h, raster, ORIENTATION_TOPLEFT, 0))
           {
                /* the raster data is pre-multiplied by the alpha component 
                   after invoking TIFFReadRGBAImageOriented*/
                _preMulti = true;

               memcpy(_data, raster, npixels*sizeof (uint32));
           }

          _TIFFfree(raster);
        }
        

        TIFFClose(tif);

        bRet = true;
    } while (0);
    return bRet;
}

namespace
{
    bool testFormatForPvr2TCSupport(PVR2TexturePixelFormat format)
    {
        if (!Configuration::getInstance()->supportsPVRTC())
        {
            if (format == PVR2TexturePixelFormat::PVRTC2BPP_RGBA ||
                format == PVR2TexturePixelFormat::PVRTC4BPP_RGBA)
            {
                return false;
            }
        }
        
        return true;
    }
    
    bool testFormatForPvr3TCSupport(PVR3TexturePixelFormat format)
    {
        if (!Configuration::getInstance()->supportsPVRTC())
        {
            if (format == PVR3TexturePixelFormat::PVRTC2BPP_RGB  ||
                format == PVR3TexturePixelFormat::PVRTC2BPP_RGBA ||
                format == PVR3TexturePixelFormat::PVRTC4BPP_RGB  ||
                format == PVR3TexturePixelFormat::PVRTC4BPP_RGBA)
            {
                return false;
            }
        }
        
        return true;
    }
}

bool Image::initWithPVRv2Data(const void *data, int dataLen)
{
    int dataLength = 0, dataOffset = 0, dataSize = 0;
    int blockSize = 0, widthBlocks = 0, heightBlocks = 0;
    int width = 0, height = 0;
    
    //Cast first sizeof(PVRTexHeader) bytes of data stream as PVRTexHeader
    const PVRv2TexHeader *header = static_cast<const PVRv2TexHeader *>(data);
    
    //Make sure that tag is in correct formatting
    if (memcmp(&header->pvrTag, gPVRTexIdentifier, strlen(gPVRTexIdentifier)) != 0)
    {
        return false;
    }
    
    Configuration *configuration = Configuration::getInstance();
    
    _hasPremultipliedAlpha = false;
    unsigned int flags = CC_SWAP_INT32_LITTLE_TO_HOST(header->flags);
    PVR2TexturePixelFormat formatFlags = static_cast<PVR2TexturePixelFormat>(flags & PVR_TEXTURE_FLAG_TYPE_MASK);
    bool flipped = (flags & (unsigned int)PVR2TextureFlag::VerticalFlip) ? true : false;
    if (flipped)
    {
        CCLOG("cocos2d: WARNING: Image is flipped. Regenerate it using PVRTexTool");
    }
    
    if (! configuration->supportsNPOT() &&
        (header->width != ccNextPOT(header->width) || header->height != ccNextPOT(header->height)))
    {
        CCLOG("cocos2d: ERROR: Loading an NPOT texture (%dx%d) but is not supported on this device", header->width, header->height);
        return false;
    }
    
    if (!testFormatForPvr2TCSupport(formatFlags))
    {
        CCLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%02X. Re-encode it with a OpenGL pixel format variant", formatFlags);
        return false;
    }

    if (v2_pixel_formathash.find(formatFlags) == v2_pixel_formathash.end())
    {
        CCLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%02X. Re-encode it with a OpenGL pixel format variant", formatFlags);
        return false;
    }
    
    auto it = Texture2D::getPixelFormatInfoMap().find(v2_pixel_formathash.at(formatFlags));

    if (it == Texture2D::getPixelFormatInfoMap().end())
    {
        CCLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%02X. Re-encode it with a OpenGL pixel format variant", formatFlags);
        return false;
    }

    _renderFormat = it->first;

    //Reset num of mipmaps
    _numberOfMipmaps = 0;

    //Get size of mipmap
    _width = width = CC_SWAP_INT32_LITTLE_TO_HOST(header->width);
    _height = height = CC_SWAP_INT32_LITTLE_TO_HOST(header->height);

    //Get ptr to where data starts..
    dataLength = CC_SWAP_INT32_LITTLE_TO_HOST(header->dataLength);

    //Move by size of header
    _dataLen = dataLen - sizeof(PVRv2TexHeader);
    _data = new unsigned char[_dataLen];
    memcpy(_data, (unsigned char*)data + sizeof(PVRv2TexHeader), _dataLen);

    // Calculate the data size for each texture level and respect the minimum number of blocks
    while (dataOffset < dataLength)
    {
        switch (formatFlags) {
        case PVR2TexturePixelFormat::PVRTC2BPP_RGBA:
            blockSize = 8 * 4; // Pixel by pixel block size for 2bpp
            widthBlocks = width / 8;
            heightBlocks = height / 4;
            break;
        case PVR2TexturePixelFormat::PVRTC4BPP_RGBA:
            blockSize = 4 * 4; // Pixel by pixel block size for 4bpp
            widthBlocks = width / 4;
            heightBlocks = height / 4;
            break;
        case PVR2TexturePixelFormat::BGRA8888:
            if (Configuration::getInstance()->supportsBGRA8888() == false)
            {
                CCLOG("cocos2d: Image. BGRA8888 not supported on this device");
                return false;
            }
        default:
            blockSize = 1;
            widthBlocks = width;
            heightBlocks = height;
            break;
        }

        // Clamp to minimum number of blocks
        if (widthBlocks < 2)
        {
            widthBlocks = 2;
        }
        if (heightBlocks < 2)
        {
            heightBlocks = 2;
        }

        dataSize = widthBlocks * heightBlocks * ((blockSize  * it->second.bpp) / 8);
        int packetLength = (dataLength - dataOffset);
        packetLength = packetLength > dataSize ? dataSize : packetLength;

        //Make record to the mipmaps array and increment counter
        _mipmaps[_numberOfMipmaps].address = _data + dataOffset;
        _mipmaps[_numberOfMipmaps].len = packetLength;
        _numberOfMipmaps++;

        dataOffset += packetLength;

        //Update width and height to the next lower power of two
        width = MAX(width >> 1, 1);
        height = MAX(height >> 1, 1);
    }

    return true;
}

bool Image::initWithPVRv3Data(const void *data, int dataLen)
{
    if (dataLen < sizeof(PVRv3TexHeader))
    {
		return false;
	}
	
	const PVRv3TexHeader *header = static_cast<const PVRv3TexHeader *>(data);
	
	// validate version
	if (CC_SWAP_INT32_BIG_TO_HOST(header->version) != 0x50565203)
    {
		CCLOG("cocos2d: WARNING: pvr file version mismatch");
		return false;
	}
	
	// parse pixel format
	PVR3TexturePixelFormat pixelFormat = static_cast<PVR3TexturePixelFormat>(header->pixelFormat);
    
    if (!testFormatForPvr3TCSupport(pixelFormat))
    {
        CCLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%016llX. Re-encode it with a OpenGL pixel format variant",
              static_cast<unsigned long long>(pixelFormat));
        return false;
    }


    if (v3_pixel_formathash.find(pixelFormat) == v3_pixel_formathash.end())
    {
        CCLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%016llX. Re-encode it with a OpenGL pixel format variant",
              static_cast<unsigned long long>(pixelFormat));
        return false;
    }

    auto it = Texture2D::getPixelFormatInfoMap().find(v3_pixel_formathash.at(pixelFormat));

    if (it == Texture2D::getPixelFormatInfoMap().end())
    {
        CCLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%016llX. Re-encode it with a OpenGL pixel format variant",
              static_cast<unsigned long long>(pixelFormat));
        return false;
    }

    _renderFormat = it->first;
    
    // flags
	int flags = CC_SWAP_INT32_LITTLE_TO_HOST(header->flags);

    // PVRv3 specifies premultiply alpha in a flag -- should always respect this in PVRv3 files
    if (flags & (unsigned int)PVR3TextureFlag::PremultipliedAlpha)
    {
        _preMulti = true;
    }
    
	// sizing
	int width = CC_SWAP_INT32_LITTLE_TO_HOST(header->width);
	int height = CC_SWAP_INT32_LITTLE_TO_HOST(header->height);
	_width = width;
	_height = height;
	int dataOffset = 0, dataSize = 0;
	int blockSize = 0, widthBlocks = 0, heightBlocks = 0;
	
    _dataLen = dataLen - (sizeof(PVRv3TexHeader) + header->metadataLength);
    _data = new unsigned char[_dataLen];
    memcpy(_data, static_cast<const unsigned char*>(data) + sizeof(PVRv3TexHeader) + header->metadataLength, _dataLen);
	
	_numberOfMipmaps = header->numberOfMipmaps;
	CCAssert(_numberOfMipmaps < MIPMAP_MAX, "Image: Maximum number of mimpaps reached. Increate the CC_MIPMAP_MAX value");
    
	for (int i = 0; i < _numberOfMipmaps; i++)
    {
		switch ((PVR3TexturePixelFormat)pixelFormat)
        {
			case PVR3TexturePixelFormat::PVRTC2BPP_RGB :
			case PVR3TexturePixelFormat::PVRTC2BPP_RGBA :
				blockSize = 8 * 4; // Pixel by pixel block size for 2bpp
				widthBlocks = width / 8;
				heightBlocks = height / 4;
				break;
			case PVR3TexturePixelFormat::PVRTC4BPP_RGB :
			case PVR3TexturePixelFormat::PVRTC4BPP_RGBA :
				blockSize = 4 * 4; // Pixel by pixel block size for 4bpp
				widthBlocks = width / 4;
				heightBlocks = height / 4;
				break;
			case PVR3TexturePixelFormat::BGRA8888:
				if( ! Configuration::getInstance()->supportsBGRA8888())
                {
					CCLOG("cocos2d: Image. BGRA8888 not supported on this device");
					return false;
				}
			default:
				blockSize = 1;
				widthBlocks = width;
				heightBlocks = height;
				break;
		}
        
		// Clamp to minimum number of blocks
		if (widthBlocks < 2)
        {
			widthBlocks = 2;
        }
		if (heightBlocks < 2)
        {
			heightBlocks = 2;
        }
		
		dataSize = widthBlocks * heightBlocks * ((blockSize  * it->second.bpp) / 8);
		int packetLength = _dataLen - dataOffset;
		packetLength = packetLength > dataSize ? dataSize : packetLength;
		
		_mipmaps[i].address = _data + dataOffset;
		_mipmaps[i].len = packetLength;
		
		dataOffset += packetLength;
		CCAssert(dataOffset <= _dataLen, "CCTexurePVR: Invalid lenght");
		
		
		width = MAX(width >> 1, 1);
		height = MAX(height >> 1, 1);
	}
	
	return true;
}

bool Image::initWithETCData(const void *data, int dataLen)
{
    const etc1_byte* header = static_cast<const etc1_byte*>(data);
    
    //check the data
    if(!etc1_pkm_is_valid(header))
    {
        return  false;
    }

    _width = etc1_pkm_get_width(header);
    _height = etc1_pkm_get_height(header);

    if( 0 == _width || 0 == _height )
    {
        return false;
    }

    if(Configuration::getInstance()->supportsETC())
    {
        //old opengl version has no define for GL_ETC1_RGB8_OES, add macro to make compiler happy. 
#ifdef GL_ETC1_RGB8_OES
        _renderFormat = Texture2D::PixelFormat::ETC;
        _dataLen = dataLen - ETC_PKM_HEADER_SIZE;
        _data = new unsigned char[_dataLen];
        memcpy(_data, static_cast<const unsigned char*>(data) + ETC_PKM_HEADER_SIZE, _dataLen);
        return true;
#endif
    }
    else
    {
         //if it is not gles or device do not support ETC, decode texture by software
        int bytePerPixel = 3;
        unsigned int stride = _width * bytePerPixel;
        _renderFormat = Texture2D::PixelFormat::RGB888;
        
        _dataLen =  _width * _height * bytePerPixel;
        _data = new unsigned char[_dataLen];
        
        if (etc1_decode_image(static_cast<const unsigned char*>(data) + ETC_PKM_HEADER_SIZE, static_cast<etc1_byte*>(_data), _width, _height, bytePerPixel, stride) != 0)
        {
            _dataLen = 0;
            CC_SAFE_DELETE_ARRAY(_data);
            return false;
        }
        
        return true;
    }
    return false;
}

bool Image::initWithPVRData(const void *data, int dataLen)
{
    return initWithPVRv2Data(data, dataLen) || initWithPVRv3Data(data, dataLen);
}

bool Image::initWithWebpData(const void *data, int dataLen, bool halfsize)
{
	bool bRet = false;
	do
	{
        WebPDecoderConfig config;
        if (WebPInitDecoderConfig(&config) == 0) break;
        if (WebPGetFeatures(static_cast<const uint8_t*>(data), dataLen, &config.input) != VP8_STATUS_OK) break;
        if (config.input.width == 0 || config.input.height == 0) break;
        
        config.output.colorspace = MODE_RGBA;
        _renderFormat = Texture2D::PixelFormat::RGBA8888;
        _width    = config.input.width;
        _height   = config.input.height;
        
        int bufferSize = _width * _height * 4;
        _data = new unsigned char[bufferSize];
        
        config.output.u.RGBA.rgba = static_cast<uint8_t*>(_data);
        config.output.u.RGBA.stride = _width * 4;
        config.output.u.RGBA.size = bufferSize;
        config.output.is_external_memory = 1;
        
        if (WebPDecode(static_cast<const uint8_t*>(data), dataLen, &config) != VP8_STATUS_OK)
        {
            delete []_data;
            _data = NULL;
            break;
        }
        
        bRet = true;
	} while (0);

        if (bRet && halfsize) {
            int _nwidth = _width * 2;
            int _nheight = _height * 2;
            unsigned char* _ndata = new unsigned char[_nwidth * _nheight * 4];
            uint32_t* rawOutput = reinterpret_cast<uint32_t*>(_ndata);
            const uint32_t* rawInput = reinterpret_cast<const uint32_t*>(_data);

            // YD compensates for the x loop by subtracting the width back out
            int YD = (_height / _nheight) * _width - _width;
            int YR = _height % _nheight;
            int XD = _width / _nwidth;
            int XR = _width % _nwidth;
            int outOffset = 0;
            int inOffset = 0;

            for (int y = _nheight, YE = 0; y > 0; y--) {
                for (int x = _nwidth, XE = 0; x > 0; x--) {
                    rawOutput[outOffset++]= rawInput[inOffset];
                    inOffset+=XD;
                    XE+=XR;
                    if (XE >= _nwidth) {
                        XE-= _nwidth;
                        inOffset++;
                    }
                }
                inOffset+= YD;
                YE+= YR;
                if (YE >= _nheight) {
                    YE -= _nheight;
                    inOffset+=_width;
                }
            }

            delete []_data;
            _data = NULL;

            _width = _nwidth;
            _height = _nheight;
            _data = _ndata;
        }

	return bRet;
}

bool Image::initWithRawData(const void * data, int dataLen, int width, int height, int bitsPerComponent, bool preMulti)
{
    bool bRet = false;
    do 
    {
        CC_BREAK_IF(0 == width || 0 == height);

        _height   = height;
        _width    = width;
        _preMulti = preMulti;
        _renderFormat = Texture2D::PixelFormat::RGBA8888;

        // only RGBA8888 supported
        int bytesPerComponent = 4;
        _dataLen = height * width * bytesPerComponent;
        _data = new unsigned char[_dataLen];
        CC_BREAK_IF(! _data);
        memcpy(_data, data, _dataLen);

        bRet = true;
    } while (0);

    return bRet;
}

#if (CC_TARGET_PLATFORM != CC_PLATFORM_IOS)
bool Image::saveToFile(const char *pszFilePath, bool bIsToRGB)
{
    //only support for Texture2D::PixelFormat::RGB888 or Texture2D::PixelFormat::RGBA8888 uncompressed data
    if (isCompressed() || (_renderFormat != Texture2D::PixelFormat::RGB888 && _renderFormat != Texture2D::PixelFormat::RGBA8888))
    {
        CCLOG("cocos2d: Image: saveToFile is only support for Texture2D::PixelFormat::RGB888 or Texture2D::PixelFormat::RGBA8888 uncompressed data for now");
        return false;
    }

#if (CC_TARGET_PLATFORM == CC_PLATFORM_MAC)
    assert(false);
    return false;
#else
    bool bRet = false;

    do 
    {
        CC_BREAK_IF(NULL == pszFilePath);

        std::string strFilePath(pszFilePath);
        CC_BREAK_IF(strFilePath.size() <= 4);

        std::string strLowerCasePath(strFilePath);
        for (unsigned int i = 0; i < strLowerCasePath.length(); ++i)
        {
            strLowerCasePath[i] = tolower(strFilePath[i]);
        }

        if (std::string::npos != strLowerCasePath.find(".png"))
        {
            CC_BREAK_IF(!saveImageToPNG(pszFilePath, bIsToRGB));
        }
        else if (std::string::npos != strLowerCasePath.find(".jpg"))
        {
            CC_BREAK_IF(!saveImageToJPG(pszFilePath));
        }
        else
        {
            break;
        }

        bRet = true;
    } while (0);

    return bRet;
#endif
}
#endif

bool Image::saveToFile(const char *filePath, Image::Format format, bool bIsToRGB) {
    if (format == Image::Format::PNG) {
        return saveImageToPNG(filePath, bIsToRGB);
    } else if (format == Image::Format::JPG) {
        return saveImageToJPG(filePath);
    } else {
        assert(false);
    }

    return false;
}

bool Image::saveImageToPNG(const char * filePath, bool isToRGB)
{
    bool bRet = false;
    do 
    {
        CC_BREAK_IF(NULL == filePath);

        FILE *fp;
        png_structp png_ptr;
        png_infop info_ptr;
        png_colorp palette;
        png_bytep *row_pointers;

        fp = fopen(filePath, "wb");
        CC_BREAK_IF(NULL == fp);

        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

        if (NULL == png_ptr)
        {
            fclose(fp);
            break;
        }

        info_ptr = png_create_info_struct(png_ptr);
        if (NULL == info_ptr)
        {
            fclose(fp);
            png_destroy_write_struct(&png_ptr, NULL);
            break;
        }
#if (CC_TARGET_PLATFORM != CC_PLATFORM_BADA && CC_TARGET_PLATFORM != CC_PLATFORM_NACL)
        if (setjmp(png_jmpbuf(png_ptr)))
        {
            fclose(fp);
            png_destroy_write_struct(&png_ptr, &info_ptr);
            break;
        }
#endif
        png_init_io(png_ptr, fp);

        if (!isToRGB && hasAlpha())
        {
            png_set_IHDR(png_ptr, info_ptr, _width, _height, 8, PNG_COLOR_TYPE_RGB_ALPHA,
                PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
        } 
        else
        {
            png_set_IHDR(png_ptr, info_ptr, _width, _height, 8, PNG_COLOR_TYPE_RGB,
                PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
        }

        palette = (png_colorp)png_malloc(png_ptr, PNG_MAX_PALETTE_LENGTH * sizeof (png_color));
        png_set_PLTE(png_ptr, info_ptr, palette, PNG_MAX_PALETTE_LENGTH);

        png_write_info(png_ptr, info_ptr);

        png_set_packing(png_ptr);

        row_pointers = (png_bytep *)malloc(_height * sizeof(png_bytep));
        if(row_pointers == NULL)
        {
            fclose(fp);
            png_destroy_write_struct(&png_ptr, &info_ptr);
            break;
        }

        if (hasAlpha())
        {
            for (int i = 0; i < (int)_height; i++)
            {
                row_pointers[i] = (png_bytep)_data + i * _width * 3;
            }

            png_write_image(png_ptr, row_pointers);

            free(row_pointers);
            row_pointers = NULL;
        }
        else
        {
            if (isToRGB)
            {
                unsigned char *pTempData = new unsigned char[_width * _height * 3];
                if (NULL == pTempData)
                {
                    fclose(fp);
                    png_destroy_write_struct(&png_ptr, &info_ptr);
                    break;
                }

                for (int i = 0; i < _height; ++i)
                {
                    for (int j = 0; j < _width; ++j)
                    {
                        pTempData[(i * _width + j) * 3] = _data[(i * _width + j) * 4];
                        pTempData[(i * _width + j) * 3 + 1] = _data[(i * _width + j) * 4 + 1];
                        pTempData[(i * _width + j) * 3 + 2] = _data[(i * _width + j) * 4 + 2];
                    }
                }

                for (int i = 0; i < (int)_height; i++)
                {
                    row_pointers[i] = (png_bytep)pTempData + i * _width * 3;
                }

                png_write_image(png_ptr, row_pointers);

                free(row_pointers);
                row_pointers = NULL;

                CC_SAFE_DELETE_ARRAY(pTempData);
            } 
            else
            {
                for (int i = 0; i < (int)_height; i++)
                {
                    row_pointers[i] = (png_bytep)_data + i * _width * 4;
                }

                png_write_image(png_ptr, row_pointers);

                free(row_pointers);
                row_pointers = NULL;
            }
        }

        png_write_end(png_ptr, info_ptr);

        png_free(png_ptr, palette);
        palette = NULL;

        png_destroy_write_struct(&png_ptr, &info_ptr);

        fclose(fp);

        bRet = true;
    } while (0);
    return bRet;
}
bool Image::saveImageToJPG(const char * filePath)
{
    bool bRet = false;
    do 
    {
        CC_BREAK_IF(NULL == filePath);

        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;
        FILE * outfile;                 /* target file */
        JSAMPROW row_pointer[1];        /* pointer to JSAMPLE row[s] */
        int     row_stride;          /* physical row width in image buffer */

        cinfo.err = jpeg_std_error(&jerr);
        /* Now we can initialize the JPEG compression object. */
        jpeg_create_compress(&cinfo);

        CC_BREAK_IF((outfile = fopen(filePath, "wb")) == NULL);
        
        jpeg_stdio_dest(&cinfo, outfile);

        cinfo.image_width = _width;    /* image width and height, in pixels */
        cinfo.image_height = _height;
        cinfo.input_components = 3;       /* # of color components per pixel */
        cinfo.in_color_space = JCS_RGB;       /* colorspace of input image */

        jpeg_set_defaults(&cinfo);

        jpeg_start_compress(&cinfo, TRUE);

        row_stride = _width * 3; /* JSAMPLEs per row in image_buffer */

        if (hasAlpha())
        {
            unsigned char *pTempData = new unsigned char[_width * _height * 3];
            if (NULL == pTempData)
            {
                jpeg_finish_compress(&cinfo);
                jpeg_destroy_compress(&cinfo);
                fclose(outfile);
                break;
            }

            for (int i = 0; i < _height; ++i)
            {
                for (int j = 0; j < _width; ++j)

                {
                    pTempData[(i * _width + j) * 3] = _data[(i * _width + j) * 4];
                    pTempData[(i * _width + j) * 3 + 1] = _data[(i * _width + j) * 4 + 1];
                    pTempData[(i * _width + j) * 3 + 2] = _data[(i * _width + j) * 4 + 2];
                }
            }

            while (cinfo.next_scanline < cinfo.image_height) {
                row_pointer[0] = & pTempData[cinfo.next_scanline * row_stride];
                (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
            }

            CC_SAFE_DELETE_ARRAY(pTempData);
        } 
        else
        {
            while (cinfo.next_scanline < cinfo.image_height) {
                row_pointer[0] = & _data[cinfo.next_scanline * row_stride];
                (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
            }
        }

        jpeg_finish_compress(&cinfo);
        fclose(outfile);
        jpeg_destroy_compress(&cinfo);
        
        bRet = true;
    } while (0);
    return bRet;
}

NS_CC_END

