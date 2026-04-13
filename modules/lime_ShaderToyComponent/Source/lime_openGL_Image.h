#pragma once

#include <cstdint>
#include <vector>

namespace lime
{
//-----------------------------------------------------------------------------

struct openGL_Image
{
	openGL_Image () = default;
	openGL_Image ( int _pixLen, int _width, int _height );

	void destroy ();
	void fill ( const uint8_t val );
	void clear ()	{	fill ( 0 );	}
	[[ nodiscard ]] bool isValid () const	{	return ! data.empty ();	}
	[[ nodiscard ]] uint8_t* getLinePointer ( const int y )	{	return data.data () + y * width * pixLen;	}

	int	pixLen = 1;
	int	width = 0;
	int height = 0;

	std::vector<uint8_t>	data;
};
//-----------------------------------------------------------------------------
}