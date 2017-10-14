/****************************************************************************
Copyright (c) 2013      Zynga Inc.

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

#include "CCTextureCacheEmscripten.h"
#include "platform/CCImage.h"
#include "platform/CCFileUtils.h"
#include <emscripten/emscripten.h>
#include <sstream>
#include <emscripten.h>

using namespace cocos2d;
using namespace std;

#define MULTILINE(...) #__VA_ARGS__

void cocos2dx_initAsyncImageLoaderLibrary() {
    static bool isInited = false;
    if (isInited) {
        return;
    }
    EM_ASM((
/****************************************************************************
Copyright (c) 2013      Zynga Inc.

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


// Non-namespaced facade over underlying objects. See above methods for
// documentation. These methods serve two purposes:
//
// 1. convert types to/from JS equivalents.
// 2. expose a "C-like" interface, such that the symbols are easy to find
//    for calling code (Emscripten provides no easy way to access nested
//    dictionaries).
//
// Note that the leading '_' is because Emscripten prefixes all function
// calls like this. Since this code is being injected at runtime,
// Emscripten's compiler doesn't have a chance to add it for us.


var LibraryCocosHelper = {
    $cocos2dx__deps: [ '_CCTextureCacheEmscripten_preMultiplyImageRegion' ],
    $cocos2dx: {
        objects: {},
        classes: {
            // AsyncOperationQueue -- simple worker queue. Note that all functions
            // passed in should effectively be "static", and have all requisite
            // information contained in their args object.
            AsyncOperationQueue: function()
            {
                this.ops = [];
                this.timeoutId = null;

                // Target using 2/3 of available CPU for texture loading.
                this.sliceBudget = 8;
                this.sliceInterval = 4;

                /**
                 * If there is a valid scheduled queue consumer, cancel it. Won't cancel
                 * currently executing operations (as this is not multi-threaded), but will
                 * prevent future execution from happening until it is re-scheduled.
                 */
                this.unschedule = function(interval)
                {
                    if(typeof this.timeoutId == "number")
                    {
                        clearTimeout(this.timeoutId);
                        this.timeoutId = null;
                    }
                };

                /**
                 * Schedule the queue-processor to run @interval ms in the future.
                 */
                this.schedule = function(interval)
                {
                    this.unschedule();

                    var that = this;
                    var o = function() {
                        that.run();
                    };

                    this.timeoutId = setTimeout(o, interval);
                };

                /**
                 * Enqueue a funtcion @fn with arguments @args to run asynchronously at
                 * some point in the future, then return control to the caller. Operations
                 * are guaranteed to be run in the order in which they are enqueued, but no
                 * guarantee is given about when or in what context they will be executed.
                 */
                this.enqueue = function(fn, args)
                {
                    var op = {
                        fn: fn,
                        args: args
                    };
                    this.ops.push(op);
                    this.schedule(this.sliceInterval);
                };

                /**
                 * Start running the consumer "thread". Will consume operations from the
                 * queue until such time as either the queue is empty, or it has exceeded
                 * its time budget. If the latter case (budget exceeded), it will
                 * reschedule itself to run again later and finish draining the queue.
                 *
                 * Rescheduling logic still in flux, but the objective is to try to
                 * schedule it to run at some point after the next frame has been
                 * requested, whilst still maintaining maximum bandwidth.
                 */
                this.run = function()
                {
                    if(this.ops.length === 0)
                    {
                        return;
                    }

                    var start = +new Date();
                    var end = +new Date();
                    while(this.ops.length && ((end - start) < this.sliceBudget))
                    {
                        var op = this.ops.shift();
                        op.fn(op.args);
                        end = +new Date();
                    }

                    if(this.ops.length)
                    {
                        this.schedule(this.sliceInterval);
                    }
                };

                /**
                 * Unschedule any remaining operations in the queue.
                 */
                this.shutdown = function()
                {
                    this.unschedule();
                    this.ops = [];
                };
            },

            /**
             * Construct a new AsyncImageLoader object. @cxxTextureCache should be a
             * pointer to a valid CCTextureCacheEmscripten object in C++.
             */
            AsyncImageLoader: function(cxxTextureCache)
            {
                this.cxxTextureCache = cxxTextureCache;
                this.operationQueue = new cocos2dx.classes.AsyncOperationQueue();

                // Since there will only ever be one image being processed at a time,
                // we pre-initialize a canvas object and use it for all operations so
                // as to save doing this for every image, incurring (probably) both
                // compute work and memory fragmentation.
                this.canvas = document.createElement('canvas');
                this.ctx = this.canvas.getContext('2d');

                this.regionSize = 128;

                /**
                 * "Private" and "static" method to copy an input image to a given
                 * rectangle in the output image. Used by @loadImage.
                 */
                this._blitAndPremultipyRegion = function(args)
                {
                    // Calculate width and height for this region such that we
                    // do not run off the edge of the image.
                    var rw = Math.min(args.i + this.regionSize, args.w) - args.i;
                    var rh = Math.min(args.j + this.regionSize, args.h) - args.j;

                    this.ctx.drawImage(args.img, args.i, args.j, rw, rh, args.i, args.j, rw, rh);

                    var imgData = this.ctx.getImageData(args.i, args.j, rw, rh);
                    var inImgData = Module['_malloc'](rw * rh * 4);
                    Module.HEAP8.set(imgData.data, inImgData);

                    // Call into C++ code in CCTextureCacheEmscripten.cpp to do the actual
                    // copy and pre-multiply.
                    Module['_TextureCacheEmscripten_preMultiplyImageRegion'](inImgData, rw, rh, args.outImgData, args.w, args.h, args.i, args.j);
                    Module['_free'](inImgData);
                };

                /**
                 * Enqueue the image at @path (interpreted as a URL) to be asynchronously
                 * loaded, then call the cocos2dx callback specified in @asyncData. Image
                 * fetch happens asynchronously using browser asset management code, and
                 * the work of copying pixel data to a buffer (for use by cocos2dx) is
                 * spread out over several frames such that disruptions to frame rate are
                 * minimized.
                 */
                this.loadImage = function(path, asyncData)
                {
                    var img = new Image();
                    var that = this;

                    img.onload = function()
                    {
                        var w = img.width;
                        var h = img.height;

                        // Setup the canvas in the queue also so that it happens in order
                        // with the operations that depend on the canvas having been
                        // configured to this resolution. We can get away with this since
                        // the queue guarantees that operations are executed in the order
                        // in which they are enqueued.
                        var setupCanvas = function(args)
                        {
                            that.canvas.width = args.w;
                            that.canvas.height = args.h;
                        };
                        that.operationQueue.enqueue(setupCanvas, { w: w, h: h });

                        var outImgData = Module['_malloc'](w * h * 4);

                        for(var i = 0; i < w; i += that.regionSize)
                        {
                            for(var j = 0; j < h; j += that.regionSize)
                            {
                                var args = {
                                    i: i, j: j,
                                    w: w, h: h,
                                    img: img,
                                    outImgData: outImgData
                                };
                                that.operationQueue.enqueue(function(a) { that._blitAndPremultipyRegion(a); }, args);
                            }
                        }

                        var fireCallback = function(args) {
                            Module['_TextureCacheEmscripten_addImageAsyncCallBack'](that.cxxTextureCache, args.asyncData, args.imgData, args.w, args.h);
                        };
                        var opArgs = {
                            asyncData: asyncData,
                            imgData: outImgData,
                            w: w, h: h
                        };
                        that.operationQueue.enqueue(fireCallback, opArgs);
                    };

                    img.onerror = function()
                    {
                        console.log("Error loading '" + path + "'");
                    };

                    img.src = path;
                };

                /**
                 * Shutdown this image loader object. Used by destructor in cocos2dx.
                 */
                this.shutdown = function()
                {
                    this.operationQueue.shutdown();
                };
            }
        }
    },

    /*
     * Construct a new AsyncImageLoader object. Held as a singleton, referred
     * to in other wrapper methods here.
     * @deps__ignored is ignored, but is accepted so that we can communicate to
     * the compiler what functions we need to ensure are ready by the time this
     * class gets instantiated.
     */
    cocos2dx_newAsyncImageLoader: function(cxxTextureCache, deps__ignored)
    {
        cocos2dx.objects.asyncImageLoader = new cocos2dx.classes.AsyncImageLoader(cxxTextureCache);
    },

    /**
     * Shutdown the current image loader object. Used by the cocos2dx
     * destructor method.
     */
    cocos2dx_shutdownAsyncImageLoader: function()
    {
        cocos2dx.objects.asyncImageLoader.shutdown();
        cocos2dx.objects.asyncImageLoader = null;
    },

    /**
     * Load a new image asynchronously.
     */
    cocos2dx_asyncImageLoader_LoadImage: function(path, asyncData)
    {
        var opArgs = {
            path: Pointer_stringify(path),
            data: asyncData
        };
        var op = function(args)
        {
            cocos2dx.objects.asyncImageLoader.loadImage(args.path, args.data);
        };
        cocos2dx.objects.asyncImageLoader.operationQueue.enqueue(op, opArgs);
    }
};

window['LibraryCocosHelper'] = LibraryCocosHelper;
window['cocos2dx'] = LibraryCocosHelper.$cocos2dx;

));
}

extern "C" {
// This C interface is exposed so that the JavaScript in
// TextureCacheEmscripten.js is able to call into these functions.
void EMSCRIPTEN_KEEPALIVE TextureCacheEmscripten_addImageAsyncCallBack(TextureCacheEmscripten *tc, TextureCache::AsyncStruct *data, unsigned char *imgData, int width, int height);
void EMSCRIPTEN_KEEPALIVE TextureCacheEmscripten_preMultiplyImageRegion( unsigned char *in, int win, int hin, unsigned char *out, int wout, int hout, int xout, int yout);
};

void TextureCacheEmscripten_addImageAsyncCallBack(TextureCacheEmscripten *tc, TextureCache::AsyncStruct *data, unsigned char *imgData, int width, int height)
{
    tc->addImageAsyncCallBack_emscripten(data, imgData, width, height);
}

/**
 * Construct a new TextureCacheEmscripten object. Note that the code in
 * TextureCacheEmscripten.js will be injected into the JavaScript runtime as
 * a side-affect.
 */
TextureCacheEmscripten::TextureCacheEmscripten()
{
    // Add dummy references to these functions so that the compiler will emit
    // code for them prior to this point (which is before when we will call
    // them.
    int deps[] = {
        (int)&TextureCacheEmscripten_addImageAsyncCallBack,
        (int)&TextureCacheEmscripten_preMultiplyImageRegion
    };

    cocos2dx_initAsyncImageLoaderLibrary();
    // cocos2dx_newAsyncImageLoader((int)this, (int)&deps);
    EM_ASM_ARGS({
        LibraryCocosHelper.cocos2dx_newAsyncImageLoader($0, $1);
    }, (size_t)this, (size_t)&deps);
}

/**
 * Destroy this object. Note that this will shutdown the image processing queue
 * (and hence stall anything waiting for those images to finish loading), and
 * unload the classes and objects instantiated in JavaScript to make this class
 * work.
 */
TextureCacheEmscripten::~TextureCacheEmscripten()
{
    // cocos2dx_shutdownAsyncImageLoader();
    EM_ASM({
        LibraryCocosHelper.cocos2dx_shutdownAsyncImageLoader();
    });
}

/**
 * "Blit" the input image to a particular location on the target image. Output
 * image will be alpha pre-multiplied in the process.
 *
 * @in:     Pointer to input image raw data bytes
 * @win:    Width of input image in pixels
 * @hin:    Height of input image in pixels
 * @out:    Pointer to memory where output image raw pixels should be written
 * @wout:   Width of output image in pixels
 * @hout:   Height of output image in pixels
 * @xout:   x-offset into target image where blitted image should be placed
 * @yout:   y-offset into target image where blitted image should be placed
 */
void TextureCacheEmscripten_preMultiplyImageRegion(
    unsigned char *in, int win, int hin, // Input image, its width and height
    unsigned char *out, int wout, int hout, // Output image, its width and height
    int xout, int yout) // x and y offsets into the output image
{
    int iter = 0;
    for(int j = 0; j < hin; j++)
    {
        for(int i = 0; i < win; i++)
        {
            int inOffset = 4 * (j * win + i);
            int outOffset = 4 * ((j + yout) * wout + (i + xout));

            unsigned char *pin = in + inOffset;
            unsigned int *pout = (unsigned int *) (out + outOffset);

            *pout = CC_RGB_PREMULTIPLY_ALPHA( pin[0], pin[1], pin[2], pin[3] );
        }
    }
}

/**
 * "Private" callback method to load a texture based on the image data
 * generated in TextureCacheEmscripten.js. Exposed here as public so that it
 * can be called from the C wrapper method
 * @TextureCacheEmscripten_addImageAsyncCallBack, above.
 */
void TextureCacheEmscripten::addImageAsyncCallBack_emscripten(AsyncStruct *data, unsigned char *imgData, int width, int height)
{
    const char *filename = data->filename.c_str();

    Image *pImage = new Image();
    pImage->initWithRawData((unsigned char*) imgData, 4 * width * height, width, height, 8, true);

    free(imgData);

    Texture2D *texture = new Texture2D();
    texture->initWithImage(pImage); 

#if CC_ENABLE_CACHE_TEXTURE_DATA
    // cache the texture file name
    VolatileTexture::addImageTexture(texture, filename, Image::kFmtRawData);
#endif

    // cache the texture
    _textures->setObject(texture, filename);
    texture->autorelease();

    Object *target = data->target;
    SEL_CallFuncO selector = data->selector;

    if (target && selector)
    {
        (target->*selector)(texture);
        target->release();
    }

    pImage->release();
    delete data;
}

void TextureCacheEmscripten::addImageAsync(const char *path, Object *target, SEL_CallFuncO selector)
{
    CCAssert(path != NULL, "TextureCache: fileimage MUST not be NULL");    

    Texture2D *texture = NULL;

    // optimization
    std::string pathKey = FileUtils::getInstance()->fullPathForFilename(path);
    texture = (Texture2D*)_textures->objectForKey(pathKey.c_str());

    std::string fullpath = pathKey;
    if (texture != NULL)
    {
        if (target && selector)
        {
            (target->*selector)(texture);
        }
        
        return;
    }

    if (target)
    {
        target->retain();
    }

    // generate async struct
    AsyncStruct *data = new AsyncStruct(fullpath, target, selector);

    // Call into JavaScript code in TextureCacheEmscripten.js to do the rest.
    // cocos2dx_asyncImageLoader_LoadImage(data->filename.c_str(), data);
    EM_ASM_ARGS({
        LibraryCocosHelper.cocos2dx_asyncImageLoader_LoadImage($0, $1);
    }, (size_t) data->filename.c_str(), (size_t) data);

    return;
}
