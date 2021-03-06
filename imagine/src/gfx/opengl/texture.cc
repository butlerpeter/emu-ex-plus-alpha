/*  This file is part of Imagine.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Imagine.  If not, see <http://www.gnu.org/licenses/> */

#include <algorithm>
#include <imagine/gfx/Gfx.hh>
#include <imagine/gfx/GfxBufferImage.hh>
#include "private.hh"
#ifdef __ANDROID__
#include "../../base/android/android.hh"
#include "android/DirectTextureBufferImage.hh"
#include "android/SurfaceTextureBufferImage.hh"
#endif

#ifdef CONFIG_GFX_OPENGL_ES
#define GL_MIRRORED_REPEAT 0x8370
#endif

namespace Gfx
{

TextureHandle newTexRef()
{
	GLuint ref;
	glGenTextures(1, &ref);
	//logMsg("created texture %u", ref);
	return ref;
}

void freeTexRef(TextureHandle texRef)
{
	//logMsg("deleting texture %u", texRef);
	glcDeleteTextures(1, &texRef);
}

static uint unpackAlignForPitch(uint pitch)
{
	uint alignment = 1;
	if(pitch % 8 == 0) alignment = 8;
	else if(pitch % 4 == 0) alignment = 4;
	else if(pitch % 2 == 0) alignment = 2;
	return alignment;
}

static uint unpackAlignForAddr(void *srcAddr)
{
	auto addr = (ptrsize)srcAddr;
	uint alignment = 1;
	if(addr % 8 == 0) alignment = 8;
	else if(addr % 4 == 0) alignment = 4;
	else if(addr % 2 == 0) alignment = 2;
	return alignment;
}

static uint unpackAlignForAddrAndPitch(void *srcAddr, uint pitch)
{
	uint alignmentForAddr = unpackAlignForAddr(srcAddr);
	uint alignmentForPitch = unpackAlignForPitch(pitch);
	if(alignmentForAddr < alignmentForPitch)
	{
		logMsg("using lowest alignment of address %p (%d) and pitch %d (%d)",
				srcAddr, alignmentForAddr, pitch, alignmentForPitch);
	}
	uint alignment = std::min(alignmentForPitch, alignmentForAddr);
	return alignment;
}

uint BufferImage::bestAlignment(const IG::Pixmap &p)
{
	return unpackAlignForAddrAndPitch(p.data, p.pitch);
}

static int bestClampMode(bool textured)
{
	if(textured)
	{
		logMsg("repeating image");
		return GL_REPEAT;
	}
	return GL_CLAMP_TO_EDGE;
	//return GL_MIRRORED_REPEAT;
}

static GLenum pixelFormatToOGLDataType(const PixelFormatDesc &format)
{
	switch(format.id)
	{
		case PIXEL_RGBA8888:
		case PIXEL_BGRA8888:
		#if !defined CONFIG_GFX_OPENGL_ES
			return GL_UNSIGNED_INT_8_8_8_8_REV;
		#endif
		case PIXEL_ARGB8888:
		case PIXEL_ABGR8888:
		#if !defined CONFIG_GFX_OPENGL_ES
			return GL_UNSIGNED_INT_8_8_8_8;
		#endif
		case PIXEL_RGB888:
		case PIXEL_BGR888:
		case PIXEL_I8:
		case PIXEL_IA88:
		case PIXEL_A8:
			return GL_UNSIGNED_BYTE;
		case PIXEL_RGB565:
			return GL_UNSIGNED_SHORT_5_6_5;
		case PIXEL_ARGB1555:
			return GL_UNSIGNED_SHORT_5_5_5_1;
		case PIXEL_ARGB4444:
			return GL_UNSIGNED_SHORT_4_4_4_4;
		#if !defined CONFIG_GFX_OPENGL_ES
		case PIXEL_BGRA4444:
			return GL_UNSIGNED_SHORT_4_4_4_4_REV;
		case PIXEL_ABGR1555:
			return GL_UNSIGNED_SHORT_1_5_5_5_REV;
		#endif
		default: bug_branch("%d", format.id); return 0;
	}
}

static GLenum pixelFormatToOGLFormat(const PixelFormatDesc &format)
{
//	#if defined(CONFIG_BASE_PS3)
//		if(format.id == PIXEL_ARGB8888)
//			return GL_BGRA;
//	#endif
	if(format.isGrayscale())
	{
		if(format.hasColorComponent())
		{
			if(format.aBits)
			{
				#if !defined CONFIG_GFX_OPENGL_ES
				if(!useFixedFunctionPipeline)
					return GL_RG;
				#endif
				#if defined CONFIG_GFX_OPENGL_ES || defined CONFIG_GFX_OPENGL_FIXED_FUNCTION_PIPELINE
				return GL_LUMINANCE_ALPHA;
				#endif
			}
			#if defined CONFIG_GFX_OPENGL_ES || defined CONFIG_GFX_OPENGL_FIXED_FUNCTION_PIPELINE
			else return GL_LUMINANCE;
			#endif
			bug_exit("no valid return values");
			return 0;
		}
		else
		{
			#if !defined CONFIG_GFX_OPENGL_ES
			if(!useFixedFunctionPipeline)
				return GL_RED;
			#endif
			#if defined CONFIG_GFX_OPENGL_ES || defined CONFIG_GFX_OPENGL_FIXED_FUNCTION_PIPELINE
			return GL_ALPHA;
			#endif
			bug_exit("no valid return values");
			return 0;
		}
	}
	#if !defined CONFIG_GFX_OPENGL_ES
	else if(format.isBGROrder())
	{
		assert(supportBGRPixels);
		if(format.aBits)
		{
			return GL_BGRA;
		}
		else return GL_BGR;
	}
	#else
	else if(format.isBGROrder() && format.aBits)
	{
		assert(supportBGRPixels);
		return GL_BGRA;
	}
	#endif
	else if(format.aBits)
	{
		return GL_RGBA;
	}
	else return GL_RGB;
}

static int pixelToOGLInternalFormat(const PixelFormatDesc &format)
{
	#if defined CONFIG_GFX_OPENGL_ES
	if(format.id == PIXEL_BGRA8888) // Apple's BGRA extension loosens the internalformat match requirement
		return bgrInternalFormat;
	else return pixelFormatToOGLFormat(format); // OpenGL ES manual states internalformat always equals format
	#else
	if(useCompressedTextures)
	{
		switch(format.id)
		{
			case PIXEL_RGBA8888:
			case PIXEL_BGRA8888:
				return GL_COMPRESSED_RGBA;
			case PIXEL_RGB888:
			case PIXEL_BGR888:
			case PIXEL_RGB565:
			case PIXEL_ARGB1555:
			case PIXEL_ARGB4444:
			case PIXEL_BGRA4444:
				return GL_COMPRESSED_RGB;
			case PIXEL_I8:
				if(!useFixedFunctionPipeline)
					return GL_COMPRESSED_RED;
				#if defined CONFIG_GFX_OPENGL_FIXED_FUNCTION_PIPELINE
				return GL_COMPRESSED_LUMINANCE;
				#endif
			case PIXEL_IA88:
				if(!useFixedFunctionPipeline)
					return GL_COMPRESSED_RG;
				#if defined CONFIG_GFX_OPENGL_FIXED_FUNCTION_PIPELINE
				return GL_COMPRESSED_LUMINANCE_ALPHA;
				#endif
			case PIXEL_A8:
				if(!useFixedFunctionPipeline)
					return GL_COMPRESSED_RED;
				#if defined CONFIG_GFX_OPENGL_FIXED_FUNCTION_PIPELINE
				return GL_COMPRESSED_ALPHA;
				#endif
			default: bug_branch("%d", format.id); return 0;
		}
	}
	else
	{
		switch(format.id)
		{
			case PIXEL_BGRA8888:
//			#if defined(CONFIG_BASE_PS3)
//				return GL_BGRA;
//			#endif
			case PIXEL_ARGB8888:
			case PIXEL_ABGR8888:
//			#if defined(CONFIG_BASE_PS3)
//				return GL_ARGB_SCE;
//			#endif
			case PIXEL_RGBA8888:
				return GL_RGBA8;
			case PIXEL_RGB888:
			case PIXEL_BGR888:
				return GL_RGB8;
			case PIXEL_RGB565:
				return GL_RGB5;
			case PIXEL_ABGR1555:
			case PIXEL_ARGB1555:
				return GL_RGB5_A1;
			case PIXEL_ARGB4444:
			case PIXEL_BGRA4444:
				return GL_RGBA4;
			case PIXEL_I8:
			{
				if(!useFixedFunctionPipeline)
					return GL_RG8;
				#if defined CONFIG_GFX_OPENGL_FIXED_FUNCTION_PIPELINE
				return GL_LUMINANCE8;
				#endif
			}
			case PIXEL_IA88:
			{
				if(!useFixedFunctionPipeline)
					return GL_RG8;
				#if defined CONFIG_GFX_OPENGL_FIXED_FUNCTION_PIPELINE
				return GL_LUMINANCE8_ALPHA8;
				#endif
			}
			case PIXEL_A8:
			{
				if(!useFixedFunctionPipeline)
					return GL_R8;
				#if defined CONFIG_GFX_OPENGL_FIXED_FUNCTION_PIPELINE
				return GL_ALPHA8;
				#endif
			}
			default: bug_branch("%d", format.id); return 0;
		}
	}
	#endif
	bug_exit("no valid format for %s", format.name);
	return 0;
}

enum { MIPMAP_NONE, MIPMAP_LINEAR, MIPMAP_NEAREST };
static GLint minFilterType(uint imgFilter, uchar mipmapType)
{
	if(imgFilter == BufferImage::NEAREST)
	{
		return mipmapType == MIPMAP_NEAREST ? GL_NEAREST_MIPMAP_NEAREST :
			mipmapType == MIPMAP_LINEAR ? GL_NEAREST_MIPMAP_LINEAR :
			GL_NEAREST;
	}
	else
	{
		return mipmapType == MIPMAP_NEAREST ? GL_LINEAR_MIPMAP_NEAREST :
			mipmapType == MIPMAP_LINEAR ? GL_LINEAR_MIPMAP_LINEAR :
			GL_LINEAR;
	}
}

static GLint magFilterType(uint imgFilter)
{
	return imgFilter == BufferImage::NEAREST ? GL_NEAREST : GL_LINEAR;
}

static void setDefaultImageTextureParams(uint imgFilter, uchar mipmapType, int xWrapType, int yWrapType, uint usedX, uint usedY, GLenum target)
{
	//mipmapType = MIPMAP_NONE;
	glTexParameteri(target, GL_TEXTURE_WRAP_S, xWrapType);
	glTexParameteri(target, GL_TEXTURE_WRAP_T, yWrapType);
	auto magFilter = magFilterType(imgFilter);
	if(magFilter != GL_LINEAR) // GL_LINEAR is the default
		glTexParameteri(target, GL_TEXTURE_MAG_FILTER, magFilter);
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilterType(imgFilter, mipmapType));
	#ifndef CONFIG_ENV_WEBOS
	if(useAnisotropicFiltering)
		glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropy);
	#endif
}

static uint writeGLTexture(IG::Pixmap &pix, bool includePadding, GLenum target, uint srcAlign)
{
	//logMsg("writeGLTexture");
	//logMsg("setting source pixel row alignment: %d", srcAlign);
	glcPixelStorei(GL_UNPACK_ALIGNMENT, srcAlign);
	if((ptrsize)pix.data % (ptrsize)srcAlign != 0)
	{
		bug_exit("expected data from address %p to be aligned to %u bytes", pix.data, srcAlign);
	}
	GLenum format = pixelFormatToOGLFormat(pix.format);
	GLenum dataType = pixelFormatToOGLDataType(pix.format);
	uint xSize = includePadding ? pix.pitchPixels() : pix.x;
	#ifndef CONFIG_GFX_OPENGL_ES
	glcPixelStorei(GL_UNPACK_ROW_LENGTH, (!includePadding && pix.isPadded()) ? pix.pitchPixels() : 0);
	//logMsg("writing %s %dx%d to %dx%d, xline %d", glImageFormatToString(format), 0, 0, pix->x, pix->y, pix->pitch / pix->format->bytesPerPixel);
	handleGLErrors();
	glTexSubImage2D(target, 0, 0, 0,
			xSize, pix.y, format, dataType, pix.data);
	if(handleGLErrors([](GLenum, const char *err) { logErr("%s in glTexSubImage2D", err); }))
	{
		return 0;
	}
	#else
	handleGLErrors();
	if(includePadding || pix.pitch == pix.x * pix.format.bytesPerPixel)
	{
		//logMsg("pitch equals x size optimized case");
		glTexSubImage2D(target, 0, 0, 0,
				xSize, pix.y, format, dataType, pix.data);
		if(handleGLErrors([](GLenum, const char *err) { logErr("%s in glTexSubImage2D", err); }))
		{
			return 0;
		}
	}
	else
	{
		logWarn("OGL ES slow glTexSubImage2D case");
		char *row = pix.data;
		for(int y = 0; y < (int)pix.y; y++)
		{
			glTexSubImage2D(target, 0, 0, y,
					pix.x, 1, format, dataType, row);
			if(handleGLErrors([](GLenum, const char *err) { logErr("%s in glTexSubImage2D", err); }))
			{
				logErr("error in line %d", y);
				return 0;
			}
			row += pix.pitch;
		}
	}
	#endif

	return 1;
}

static uint replaceGLTexture(IG::Pixmap &pix, bool upload, uint internalFormat, bool includePadding, GLenum target, uint srcAlign)
{
	glcPixelStorei(GL_UNPACK_ALIGNMENT, srcAlign);
	if((ptrsize)pix.data % (ptrsize)srcAlign != 0)
	{
		bug_exit("expected data from address %p to be aligned to %u bytes", pix.data, srcAlign);
	}
	#ifndef CONFIG_GFX_OPENGL_ES
	glcPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	#endif
	GLenum format = pixelFormatToOGLFormat(pix.format);
	GLenum dataType = pixelFormatToOGLDataType(pix.format);
	uint xSize = includePadding ? pix.pitchPixels() : pix.x;
	if(includePadding && pix.pitchPixels() != pix.x)
		logMsg("including padding in texture size, %d", pix.pitchPixels());
	handleGLErrors();
	glTexImage2D(target, 0, internalFormat, xSize, pix.y,
				0, format, dataType, upload ? pix.data : 0);
	if(handleGLErrors([](GLenum, const char *err) { logErr("%s in glTexImage2D", err); }))
	{
		return 0;
	}
	return 1;
}

static uint replaceGLTexture(IG::Pixmap &pix, bool upload, uint internalFormat, bool includePadding, GLenum target)
{
	uint alignment = unpackAlignForAddrAndPitch(pix.data, pix.pitch);
	return replaceGLTexture(pix, upload, internalFormat, includePadding, target, alignment);
}

static const PixelFormatDesc *swapRGBToPreferedOrder(const PixelFormatDesc *fmt)
{
	if(Gfx::preferBGR && fmt->id == PIXEL_RGB888)
		return &PixelFormatBGR888;
	else if(Gfx::preferBGRA && fmt->id == PIXEL_RGBA8888)
		return &PixelFormatBGRA8888;
	else
		return fmt;
}

bool BufferImage::hasMipmaps()
{
	#ifdef CONFIG_MACHINE_PANDORA
	// mipmap auto-generation appears to be broken in the driver
	return false;
	#endif
	return hasMipmaps_;
}

void BufferImage::setFilter(uint filter)
{
	logMsg("setting texture filter %s", filter == BufferImage::NEAREST ? "nearest" : "linear");
	#if !defined(CONFIG_GFX_OPENGL_TEXTURE_EXTERNAL_OES)
	GLenum target = GL_TEXTURE_2D;
	#else
	GLenum target = textureDesc().target;
	#endif
	Gfx::setActiveTexture(textureDesc().tid, target);
	auto magFilter = magFilterType(filter);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, magFilter);
	if(handleGLErrorsVerbose([](GLenum, const char *err) { logErr("%s in glTexParameteri with GL_TEXTURE_MAG_FILTER", err); }))
		logWarn("error with mag filter %d", magFilter);
	auto minFilter = minFilterType(filter, hasMipmaps() ? MIPMAP_NEAREST : MIPMAP_NONE);
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter);
	if(handleGLErrorsVerbose([](GLenum, const char *err) { logErr("%s in glTexParameteri with GL_TEXTURE_MIN_FILTER", err); }))
		logWarn("error with min filter %d", minFilter);
}

void BufferImage::setRepeatMode(uint xMode, uint yMode)
{
	#if !defined(CONFIG_GFX_OPENGL_TEXTURE_EXTERNAL_OES)
	GLenum target = GL_TEXTURE_2D;
	#else
	GLenum target = textureDesc().target;
	#endif
	Gfx::setActiveTexture(textureDesc().tid, target);
	auto wrapS = xMode ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	glTexParameteri(target, GL_TEXTURE_WRAP_S, wrapS);
	if(handleGLErrorsVerbose([](GLenum, const char *err) { logErr("%s in glTexParameteri with GL_TEXTURE_WRAP_S", err); }))
		logWarn("error with wrap s %d", wrapS);
	auto wrapT = yMode ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	glTexParameteri(target, GL_TEXTURE_WRAP_T, wrapT);
	if(handleGLErrorsVerbose([](GLenum, const char *err) { logErr("%s in glTexParameteri with GL_TEXTURE_WRAP_T", err); }))
		logWarn("error with wrap t %d", wrapT);
}

void TextureBufferImage::write(IG::Pixmap &p, uint hints, uint alignment)
{
	glcBindTexture(GL_TEXTURE_2D, desc.tid);
	writeGLTexture(p, hints, GL_TEXTURE_2D, alignment);
}

void TextureBufferImage::write(IG::Pixmap &p, uint hints)
{
	uint alignment = unpackAlignForAddrAndPitch(p.data, p.pitch);
	write(p, hints, alignment);
}

void TextureBufferImage::replace(IG::Pixmap &p, uint hints)
{
	glcBindTexture(GL_TEXTURE_2D, desc.tid);
	replaceGLTexture(p, 1, pixelToOGLInternalFormat(p.format), hints, GL_TEXTURE_2D);
	setSwizzleForFormat(p.format);
}

void TextureBufferImage::setSwizzleForFormat(const PixelFormatDesc &format)
{
	#if !defined CONFIG_GFX_OPENGL_ES && defined CONFIG_GFX_OPENGL_SHADER_PIPELINE
	if(useFixedFunctionPipeline)
		return;
	if(useTextureSwizzle)
	{
		const GLint swizzleMaskRGBA[] {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
		const GLint swizzleMaskIA88[] {GL_RED, GL_RED, GL_RED, GL_GREEN};
		const GLint swizzleMaskA8[] {GL_ONE, GL_ONE, GL_ONE, GL_RED};
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, (format.id == PIXEL_IA88) ? swizzleMaskIA88
				: (format.id == PIXEL_A8) ? swizzleMaskA8
				: swizzleMaskRGBA);
	}
	#endif
}

static uint typeForPixelFormat(const PixelFormatDesc &format)
{
	return (format.id == PIXEL_A8) ? TEX_2D_1 :
		(format.id == PIXEL_IA88) ? TEX_2D_2 :
		TEX_2D_4;
}

bool BufferImage::compileDefaultProgram(uint mode)
{
	switch(mode)
	{
		bcase IMG_MODE_REPLACE:
			switch(type_)
			{
				case TEX_2D_1 : return texAlphaReplaceProgram.compile();
				case TEX_2D_2 : return texIntensityAlphaReplaceProgram.compile();
				case TEX_2D_4 : return texReplaceProgram.compile();
				case TEX_2D_EXTERNAL : return texExternalReplaceProgram.compile();
				default: bug_branch("%d", type_); return false;
			}
		bcase IMG_MODE_MODULATE:
			switch(type_)
			{
				case TEX_2D_1 : return texAlphaProgram.compile();
				case TEX_2D_2 : return texIntensityAlphaProgram.compile();
				case TEX_2D_4 : return texProgram.compile();
				case TEX_2D_EXTERNAL : return texExternalProgram.compile();
				default: bug_branch("%d", type_); return false;
			}
		bdefault: bug_branch("%d", type_); return false;
	}
}

void BufferImage::useDefaultProgram(uint mode, const Mat4 *modelMat)
{
	#ifndef CONFIG_GFX_OPENGL_SHADER_PIPELINE
	const uint type_ = TEX_2D_4;
	#endif
	switch(mode)
	{
		bcase IMG_MODE_REPLACE:
			switch(type_)
			{
				bcase TEX_2D_1 : texAlphaReplaceProgram.use(modelMat);
				bcase TEX_2D_2 : texIntensityAlphaReplaceProgram.use(modelMat);
				bcase TEX_2D_4 : texReplaceProgram.use(modelMat);
				bcase TEX_2D_EXTERNAL : texExternalReplaceProgram.use(modelMat);
			}
		bcase IMG_MODE_MODULATE:
			switch(type_)
			{
				bcase TEX_2D_1 : texAlphaProgram.use(modelMat);
				bcase TEX_2D_2 : texIntensityAlphaProgram.use(modelMat);
				bcase TEX_2D_4 : texProgram.use(modelMat);
				bcase TEX_2D_EXTERNAL : texExternalProgram.use(modelMat);
			}
	}
}

IG::Pixmap *TextureBufferImage::lock(uint x, uint y, uint xlen, uint ylen, IG::Pixmap *fallback) { return fallback; }

void TextureBufferImage::unlock(IG::Pixmap *pix, uint hints) { write(*pix, hints); }

void TextureBufferImage::deinit()
{
	logMsg("freeing texture 0x%X", desc.tid);
	freeTexRef(desc.tid);
	desc.tid = 0;
}

void BufferImage::generateMipmaps()
{
	assert(hasMipmaps());
	#if defined CONFIG_GFX_OPENGL_ES && CONFIG_GFX_OPENGL_ES_MAJOR_VERSION > 1
	logMsg("generating mipmaps");
	glGenerateMipmap(GL_TEXTURE_2D);
	#elif defined CONFIG_GFX_OPENGL_ES
	return; // TODO: OES_framebuffer_object extension
	#else
	if(useFBOFuncs)
	{
		logMsg("generating mipmaps");
		#ifdef CONFIG_GFX_OPENGL_FIXED_FUNCTION_PIPELINE
		if(useFBOFuncsEXT)
			glGenerateMipmapEXT(GL_TEXTURE_2D);
		else
		#endif
			glGenerateMipmap(GL_TEXTURE_2D);
	}
	#endif
}

bool BufferImage::setupTexture(IG::Pixmap &pix, bool upload, uint internalFormat, int xWrapType, int yWrapType,
	uint usedX, uint usedY, uint hints, uint filter)
{
	#if defined __ANDROID__ && defined CONFIG_GFX_OPENGL_USE_DRAW_TEXTURE
	xSize = usedX;
	ySize = usedY;
	#endif
	//logMsg("createGLTexture");
	GLenum texTarget = GL_TEXTURE_2D;
	#if defined(CONFIG_GFX_OPENGL_TEXTURE_EXTERNAL_OES)
	if(surfaceTextureConf.use && (hints & BufferImage::HINT_STREAM))
	{
		texTarget = GL_TEXTURE_EXTERNAL_OES;
	}
	#endif

	auto texRef = newTexRef();
	if(texRef == 0)
	{
		logMsg("error getting new texture reference");
		return 0;
	}

	//logMsg("binding texture %u to target %d after creation", texRef, texTarget);
	glcBindTexture(texTarget, texRef);
	setDefaultImageTextureParams(filter, hasMipmaps() ? MIPMAP_NEAREST : MIPMAP_NONE, xWrapType, yWrapType, usedX, usedY, texTarget);

	bool includePadding = 0; //include extra bytes when x != pitch ?
	if(hints & BufferImage::HINT_STREAM)
	{
//		#if defined(CONFIG_BASE_PS3)
//		logMsg("optimizing texture for frequent updates");
//		glTexParameteri(texTarget, GL_TEXTURE_ALLOCATION_HINT_SCE, GL_TEXTURE_LINEAR_SYSTEM_SCE);
//		#endif
		#if defined(CONFIG_GFX_OPENGL_TEXTURE_EXTERNAL_OES)
		if(surfaceTextureConf.use)
		{
			logMsg("using SurfaceTexture, %dx%d %s", usedX, usedY, pix.format.name);
			pix.x = usedX;
			pix.y = usedY;
			auto *surfaceTex = new SurfaceTextureBufferImage;
			surfaceTex->init(texRef, pix);
			impl = surfaceTex;
			textureDesc().target = GL_TEXTURE_EXTERNAL_OES;
			textureDesc().tid = texRef;
			#ifdef CONFIG_GFX_OPENGL_SHADER_PIPELINE
			type_ = TEX_2D_EXTERNAL;
			#endif
			return 1;
		}
		#endif
		#ifdef __ANDROID__
		if(directTextureConf.useEGLImageKHR)
		{
			logMsg("using EGL image for texture, %dx%d %s", usedX, usedY, pix.format.name);
			auto *directTex = new DirectTextureBufferImage;
			if(directTex->init(pix, texRef, usedX, usedY))
			{
				pix.x = usedX;
				pix.y = usedY;
				impl = directTex;
				textureDesc().tid = texRef;
				#ifdef CONFIG_GFX_OPENGL_SHADER_PIPELINE
				type_ = TEX_2D_4;
				#endif
				return 1;
			}
			else
			{
				logWarn("failed to create EGL image, falling back to normal texture");
				delete directTex;
			}
		}
		#endif
		#ifdef CONFIG_GFX_OPENGL_ES
		includePadding = 1; // avoid slow OpenGL ES upload case
		#endif
	}

	if(hasMipmaps())
	{
		#if defined CONFIG_GFX_OPENGL_ES && CONFIG_GFX_OPENGL_ES_MAJOR_VERSION == 1
		logMsg("auto-generating mipmaps");
		glTexParameteri(texTarget, GL_GENERATE_MIPMAP, GL_TRUE);
		#endif
	}
	{
		GLenum format = pixelFormatToOGLFormat(pix.format);
		GLenum dataType = pixelFormatToOGLDataType(pix.format);
		logMsg("%s texture 0x%X with size %dx%d, internal format %s, from image %s:%s", upload ? "uploading" : "creating", texRef, pix.x, pix.y, glImageFormatToString(internalFormat), glImageFormatToString(format), glDataTypeToString(dataType));
	}
	if(replaceGLTexture(pix, upload, internalFormat, includePadding, texTarget))
	{
		//logMsg("success");
	}

	if(hasMipmaps() && upload)
	{
		generateMipmaps();
	}

	#ifdef CONFIG_GFX_OPENGL_BUFFER_IMAGE_MULTI_IMPL
	auto texBuffImg = new TextureBufferImage();
	impl = texBuffImg;
	#else
	auto texBuffImg = this;
	#endif
	texBuffImg->setSwizzleForFormat(pix.format);
	#ifdef CONFIG_GFX_OPENGL_SHADER_PIPELINE
	type_ = typeForPixelFormat(pix.format);
	#endif
	textureDesc().tid = texRef;
	return 1;
}

CallResult BufferImage::init(GfxImageSource &img, uint filter, uint hints, bool textured)
{
	deinit();

	var_selfs(hints);
	testMipmapSupport(img.width(), img.height());
	//logMsg("BufferImage::init");
	int wrapMode = bestClampMode(textured);

	uint texX, texY;
	textureSizeSupport.findBufferXYPixels(texX, texY, img.width(), img.height());

	auto pixFmt = img.pixelFormat();//swapRGBToPreferedOrder(img.pixelFormat());
	IG::Pixmap texPix(*pixFmt);
	uint uploadPixStoreSize = texX * texY * pixFmt->bytesPerPixel;
	#if __APPLE__
	//logMsg("alloc in heap"); // for low stack limits
	auto uploadPixStore = (char*)mem_calloc(uploadPixStoreSize);
	if(!uploadPixStore)
		return OUT_OF_MEMORY;
	#else
	char uploadPixStore[uploadPixStoreSize] __attribute__ ((aligned (8)));
	mem_zero(uploadPixStore, uploadPixStoreSize);
	#endif
	texPix.init(uploadPixStore, texX, texY);
	img.getImage(texPix);
	if(!setupTexture(texPix, 1, pixelToOGLInternalFormat(texPix.format), wrapMode,
			wrapMode, img.width(), img.height(), hints, filter))
	{
		#if __APPLE__
		mem_free(uploadPixStore);
		#endif
		return INVALID_PARAMETER;
	}
	#if __APPLE__
	mem_free(uploadPixStore);
	#endif

	textureDesc().xStart = pixelToTexC((uint)0, texPix.x);
	textureDesc().yStart = pixelToTexC((uint)0, texPix.y);
	textureDesc().xEnd = pixelToTexC(img.width(), texPix.x);
	textureDesc().yEnd = pixelToTexC(img.height(), texPix.y);

	return OK;
}

void BufferImage::testMipmapSupport(uint x, uint y)
{
	hasMipmaps_ = usingAutoMipmaping() &&
			!(hints & HINT_STREAM) && !(hints & HINT_NO_MINIFY)
			&& Gfx::textureSizeSupport.supportsMipmaps(x, y);
}

CallResult BufferImage::init(IG::Pixmap &pix, bool upload, uint filter, uint hints, bool textured)
{
	deinit();

	var_selfs(hints);
	testMipmapSupport(pix.x, pix.y);

	int wrapMode = bestClampMode(textured);

	uint xSize = (hints & HINT_STREAM) ? pix.pitchPixels() : pix.x;
	uint texX, texY;
	textureSizeSupport.findBufferXYPixels(texX, texY, xSize, pix.y,
		(hints & HINT_STREAM) ? TextureSizeSupport::streamHint : 0);

	IG::Pixmap texPix(pix.format);
	texPix.init(nullptr, texX, texY);

	/*uchar uploadPixStore[texX * texY * pix.format->bytesPerPixel];

	if(upload && pix.pitch != uploadPix.pitch)
	{
		mem_zero(uploadPixStore);
		pix.copy(0, 0, 0, 0, &uploadPix, 0, 0);
	}*/
	if(!setupTexture(texPix, false, pixelToOGLInternalFormat(pix.format),
			wrapMode, wrapMode, pix.x, pix.y, hints, filter))
	{
		return INVALID_PARAMETER;
	}

	textureDesc().xStart = pixelToTexC((uint)0, texPix.x);
	textureDesc().yStart = pixelToTexC((uint)0, texPix.y);
	textureDesc().xEnd = pixelToTexC(pix.x, texPix.x);
	textureDesc().yEnd = pixelToTexC(pix.y, texPix.y);

	if(upload)
	{
		write(pix);
		if(hasMipmaps())
		{
			generateMipmaps();
		}
	}

	return OK;
}

void BufferImage::write(IG::Pixmap &p) { BufferImageImpl::write(p, hints); }
void BufferImage::write(IG::Pixmap &p, uint assumeAlign) { BufferImageImpl::write(p, hints, assumeAlign); }
void BufferImage::replace(IG::Pixmap &p)
{
	BufferImageImpl::replace(p, hints);
}
void BufferImage::unlock(IG::Pixmap *p) { BufferImageImpl::unlock(p, hints); }

void BufferImage::deinit()
{
	if(!isInit())
		return;
	BufferImageImpl::deinit();
}

}
