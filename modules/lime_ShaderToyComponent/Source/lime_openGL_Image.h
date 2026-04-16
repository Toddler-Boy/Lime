#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <algorithm>
#include <ranges>
#include <utility>

namespace lime
{
//-----------------------------------------------------------------------------

struct openGL_Image
{
	openGL_Image () = default;

	// Creates a new buffer and marks this instance as the owner
	openGL_Image ( int _pixLen, int _width, int _height );

	// Shallow copy constructor
	openGL_Image ( const openGL_Image& other );

	// Move Constructor
	openGL_Image ( openGL_Image&& other ) noexcept;

	// Unifying Assignment Operator
	openGL_Image& operator=( openGL_Image other ) noexcept;

	// Swap Friend Function
	friend void swap ( openGL_Image& first, openGL_Image& second ) noexcept;

	// Creates a brand-new owner with its own unique data buffer
	[[nodiscard]] openGL_Image clone () const;

	// Destroys the buffer if this instance is the owner
	void destroy ();

	// Fills the buffer with a specific value if this instance is the owner
	void fill ( const uint8_t val );

	// Clears the buffer if this instance is the owner
	void clear () { fill ( 0 ); }

	// Checks if the buffer is allocated and valid
	[[nodiscard]] bool isValid () const { return data && !data->empty (); }

	// Returns a pointer to the start of a specific pixel row
	[[nodiscard]] uint8_t* getLinePointer ( const int y ) const { return data->data () + y * width * pixLen; }

	// Returns a pointer to the start of the data
	[[nodiscard]] uint8_t* getData () const { return data->data (); }

	int	pixLen = 1;
	int	width = 0;
	int	height = 0;

private:
	std::shared_ptr<std::vector<uint8_t>> data;
	bool	isOwner = false;
};

//-----------------------------------------------------------------------------
}
