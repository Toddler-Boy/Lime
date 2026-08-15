/*
	PNG and JPEG decoding straight into upload-ready openGL_Image buffers
	(BGRA8, premultiplied alpha), using the library copies compiled into
	juce_graphics. Own compile unit so the C library headers stay out of the
	module's unity build.
*/

#include "lime_ShaderToyComponent.h"

#include <csetjmp>
#include <cstring>

#if ! defined (JUCE_INCLUDE_PNGLIB_CODE) || JUCE_INCLUDE_PNGLIB_CODE
#include "juce_graphics/image_formats/pnglib/png.h"
#else
extern "C"
{
#include JUCE_PNGLIB_INCLUDE_PATH
}
#endif

namespace jpglibNamespace
{
#if ! defined (JUCE_INCLUDE_JPEGLIB_CODE) || JUCE_INCLUDE_JPEGLIB_CODE
#include "juce_graphics/image_formats/jpglib/jpeglib.h"
#else
extern "C"
{
#include JUCE_JPEGLIB_INCLUDE_PATH
}
#endif
}

//-----------------------------------------------------------------------------

namespace
{

//
// PNG
//

struct pngReadState
{
	const uint8_t*	data;
	size_t			size;
	size_t			pos;
};

void pngRead ( png_structp png, png_bytep dst, png_size_t length )
{
	auto&	state = *static_cast<pngReadState*> ( png_get_io_ptr ( png ) );

	if ( length > state.size - state.pos )
		png_error ( png, "" );	// Does not return

	std::memcpy ( dst, state.data + state.pos, length );
	state.pos += length;
}

void pngError ( png_structp png, png_const_charp )
{
	longjmp ( png_jmpbuf ( png ), 1 );
}

void pngWarning ( png_structp, png_const_charp ) {}

lime::openGL_Image decodePNG ( const void* data, const size_t size )
{
	auto	png = png_create_read_struct ( PNG_LIBPNG_VER_STRING, nullptr, pngError, pngWarning );
	if ( png == nullptr )
		return {};

	auto	info = png_create_info_struct ( png );
	if ( info == nullptr )
	{
		png_destroy_read_struct ( &png, nullptr, nullptr );
		return {};
	}

	pngReadState	state { (const uint8_t*)data, size, 0 };

	lime::openGL_Image		out;
	std::vector<png_bytep>	rows;

	volatile auto	ok = false;

	// libpng reports errors via longjmp, landing back here with a nonzero code
	if ( setjmp ( png_jmpbuf ( png ) ) == 0 )
	{
		png_set_read_fn ( png, &state, pngRead );

		// Keeps a corrupt header from requesting a giant allocation
		png_set_user_limits ( png, 16384, 16384 );

		png_read_info ( png, info );

		png_uint_32	w = 0;
		png_uint_32	h = 0;
		auto		bitDepth = 0;
		auto		colorType = 0;
		png_get_IHDR ( png, info, &w, &h, &bitDepth, &colorType, nullptr, nullptr, nullptr );

		// Everything becomes 8-bit BGRA
		if ( bitDepth == 16 )
			png_set_strip_16 ( png );

		if ( bitDepth < 8 || colorType == PNG_COLOR_TYPE_PALETTE || png_get_valid ( png, info, PNG_INFO_tRNS ) )
			png_set_expand ( png );

		if ( colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA )
			png_set_gray_to_rgb ( png );

		png_set_bgr ( png );
		png_set_add_alpha ( png, 0xFF, PNG_FILLER_AFTER );

		png_set_interlace_handling ( png );
		png_read_update_info ( png, info );

		out = lime::openGL_Image ( 4, int ( w ), int ( h ) );

		rows.resize ( h );
		for ( auto y = 0u; y < h; ++y )
			rows[ y ] = out.getLinePointer ( int ( y ) );

		png_read_image ( png, rows.data () );

		ok = true;
	}

	png_destroy_read_struct ( &png, &info, nullptr );

	if ( ! ok )
		return {};

	// The pipeline works in premultiplied alpha, like juce::Image does
	{
		auto	pix = out.getData ();

		for ( auto i = 0; i < out.width * out.height; ++i, pix += 4 )
		{
			if ( const auto a = pix[ 3 ]; a < 255 )
			{
				pix[ 0 ] = uint8_t ( ( pix[ 0 ] * a + 127 ) / 255 );
				pix[ 1 ] = uint8_t ( ( pix[ 1 ] * a + 127 ) / 255 );
				pix[ 2 ] = uint8_t ( ( pix[ 2 ] * a + 127 ) / 255 );
			}
		}
	}

	return out;
}

//
// JPEG. Errors set a flag and the decoder aborts, matching the juce loader
//

void jpgSilent1 ( jpglibNamespace::j_common_ptr ) {}
void jpgSilent2 ( jpglibNamespace::j_common_ptr, int ) {}
void jpgSilent3 ( jpglibNamespace::j_common_ptr, char* ) {}
void jpgFatal ( jpglibNamespace::j_common_ptr p )	{	*(bool*)p->client_data = true;	}

void jpgInitSource ( jpglibNamespace::j_decompress_ptr ) {}

jpglibNamespace::boolean jpgFill ( jpglibNamespace::j_decompress_ptr )
{
	return 0;
}

void jpgSkip ( jpglibNamespace::j_decompress_ptr cinfo, long num )
{
	cinfo->src->next_input_byte += num;
	cinfo->src->bytes_in_buffer -= size_t ( std::min ( num, long ( cinfo->src->bytes_in_buffer ) ) );
}

lime::openGL_Image decodeJPEG ( const void* data, const size_t size )
{
	using namespace jpglibNamespace;

	jpeg_decompress_struct	cinfo;
	jpeg_error_mgr			jerr;

	std::memset ( &jerr, 0, sizeof ( jerr ) );
	jerr.error_exit = jpgFatal;
	jerr.emit_message = jpgSilent2;
	jerr.output_message = jpgSilent1;
	jerr.format_message = jpgSilent3;
	jerr.reset_error_mgr = jpgSilent1;

	cinfo.err = &jerr;

	jpeg_create_decompress ( &cinfo );

	auto	failed = false;
	cinfo.client_data = &failed;

	jpeg_source_mgr	src {};
	src.init_source = jpgInitSource;
	src.fill_input_buffer = jpgFill;
	src.skip_input_data = jpgSkip;
	src.resync_to_restart = jpeg_resync_to_restart;
	src.term_source = jpgInitSource;
	src.next_input_byte = (const unsigned char*)data;
	src.bytes_in_buffer = size;
	cinfo.src = &src;

	lime::openGL_Image	out;

	jpeg_read_header ( &cinfo, TRUE );

	if ( ! failed )
	{
		jpeg_calc_output_dimensions ( &cinfo );
		cinfo.out_color_space = JCS_RGB;

		const auto	w = int ( cinfo.output_width );
		const auto	h = int ( cinfo.output_height );

		std::vector<uint8_t>	row ( size_t ( w ) * 3 );
		auto*	rowPtr = row.data ();

		if ( ! failed && w > 0 && h > 0 && jpeg_start_decompress ( &cinfo ) && ! failed )
		{
			out = lime::openGL_Image ( 4, w, h );

			for ( auto y = 0; y < h; ++y )
			{
				jpeg_read_scanlines ( &cinfo, &rowPtr, 1 );
				if ( failed )
					break;

				const auto*	src3 = row.data ();
				auto*		dst = out.getLinePointer ( y );

				// RGB triplets into opaque BGRA
				for ( auto x = 0; x < w; ++x, src3 += 3, dst += 4 )
				{
					dst[ 0 ] = src3[ 2 ];
					dst[ 1 ] = src3[ 1 ];
					dst[ 2 ] = src3[ 0 ];
					dst[ 3 ] = 255;
				}
			}

			if ( ! failed )
				jpeg_finish_decompress ( &cinfo );
		}
	}

	jpeg_destroy_decompress ( &cinfo );

	if ( failed )
		return {};

	return out;
}

}
//-----------------------------------------------------------------------------

lime::openGL_Image lime::content::decodeTexture ( const void* data, const size_t size )
{
	if ( data == nullptr || size < 8 )
		return {};

	const auto*	bytes = (const uint8_t*)data;

	if ( png_sig_cmp ( (png_const_bytep)bytes, 0, 8 ) == 0 )
		return decodePNG ( data, size );

	// JPEG starts with the SOI marker
	if ( bytes[ 0 ] == 0xFF && bytes[ 1 ] == 0xD8 && bytes[ 2 ] == 0xFF )
		return decodeJPEG ( data, size );

	return {};
}
//-----------------------------------------------------------------------------
