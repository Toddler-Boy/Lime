#include "lime_openGL_Image.h"

#include <algorithm>
#include <ranges>

namespace lime
{

//-----------------------------------------------------------------------------

openGL_Image::openGL_Image ( int _pixLen, int _width, int _height )
	: pixLen ( _pixLen )
	, width ( _width )
	, height ( _height )
	, data ( _pixLen * _width * _height )
{
}
//-----------------------------------------------------------------------------

void openGL_Image::destroy ()
{
	if ( ! isValid () )
		return;

	data = {};

	pixLen = 1;
	width = 0;
	height = 0;
}
//-----------------------------------------------------------------------------

void openGL_Image::fill ( const uint8_t val )
{
	std::ranges::fill ( data, val );
}
//-----------------------------------------------------------------------------
}