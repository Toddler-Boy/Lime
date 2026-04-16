#include "lime_openGL_Image.h"

namespace lime
{

//-----------------------------------------------------------------------------

openGL_Image::openGL_Image ( int _pixLen, int _width, int _height )
	: pixLen ( _pixLen )
	, width ( _width )
	, height ( _height )
	, data ( std::make_shared<std::vector<uint8_t>> ( _pixLen* _width* _height ) )
	, isOwner ( true )
{
}
//-----------------------------------------------------------------------------

openGL_Image::openGL_Image ( const openGL_Image& other )
	: pixLen ( other.pixLen )
	, width ( other.width )
	, height ( other.height )
	, data ( other.data )
	, isOwner ( false )
{
}
//-----------------------------------------------------------------------------

openGL_Image::openGL_Image ( openGL_Image&& other ) noexcept
	: openGL_Image ()
{
	swap ( *this, other );
}
//-----------------------------------------------------------------------------

openGL_Image& openGL_Image::operator=( openGL_Image other ) noexcept
{
	swap ( *this, other );

	return *this;
}
//-----------------------------------------------------------------------------

void swap ( openGL_Image& first, openGL_Image& second ) noexcept
{
	std::swap ( first.pixLen, second.pixLen );
	std::swap ( first.width, second.width );
	std::swap ( first.height, second.height );
	std::swap ( first.data, second.data );
	std::swap ( first.isOwner, second.isOwner );
}
//-----------------------------------------------------------------------------

openGL_Image openGL_Image::clone () const
{
	if ( ! isValid () )
		return {};

	openGL_Image	newMaster ( pixLen, width, height );
	if ( data && newMaster.data )
		*( newMaster.data ) = *data;

	return newMaster;
}
//-----------------------------------------------------------------------------

void openGL_Image::destroy ()
{
	if ( ! isOwner )
		return;

	data.reset ();
	width = 0;
	height = 0;
}
//-----------------------------------------------------------------------------

void openGL_Image::fill ( const uint8_t val )
{
	if ( isOwner && data )
		std::ranges::fill ( *data, val );
}
//-----------------------------------------------------------------------------

} // namespace lime
