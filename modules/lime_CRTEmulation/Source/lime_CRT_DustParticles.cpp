#include "lime_CRT_DustParticles.h"

namespace lime
{

//-----------------------------------------------------------------------------

CRT_DustParticles::CRT_DustParticles ( int numParticles )
{
	particles.resize ( numParticles );
	renderData.resize ( numParticles );

	for ( auto& p : particles )
		resetParticle ( p );
}
//-----------------------------------------------------------------------------

namespace
{

float getZ ( const float z )
{
	return ( z + 1.0001f ) / 2.0f;
}

}

void CRT_DustParticles::update ( float deltaTime )
{
	constexpr auto	jitterStrength = 1.0f;
	constexpr auto	drag = 0.98f; // Dampens movement so it "hovers"

	for ( auto& p : particles )
	{
		// Add chaotic Brownian jitter
		p.velocity.x += ( random.nextFloat () * 2.0f - 1.0f ) * jitterStrength * deltaTime;
		p.velocity.y += ( random.nextFloat () * 2.0f - 0.95f ) * jitterStrength * deltaTime;
		p.velocity.z += ( random.nextFloat () * 2.0f - 1.0f ) * jitterStrength * deltaTime;

		p.velocity *= drag;

		const auto	zPos = getZ ( std::clamp ( p.position.z + p.velocity.z, -1.0f, 1.0f ) );
		const auto	speedMultiplier = std::lerp ( 0.00001f, 0.005f, zPos * zPos );

		p.position.x += p.velocity.x * speedMultiplier;
		p.position.y += p.velocity.y * speedMultiplier;
		p.position.z += p.velocity.z * speedMultiplier;

		// Wrap around boundaries (keep in view)
		if ( p.position.x > 1.01f )			p.position.x = -1.01f;
		else if ( p.position.x < -1.01f )	p.position.x = 1.01f;

		if ( p.position.y > 1.01f )			p.position.y = -1.01f;
		else if ( p.position.y < -1.01f )	p.position.y = 1.01f;

		if ( p.position.z > 1.0f )			p.position.z = 1.0f;
		else if ( p.position.z < -1.0f )	p.position.z = -1.0f;
	}
}
//-----------------------------------------------------------------------------

std::vector<CRT_DustParticles::renderParticles>& CRT_DustParticles::getParticles ()
{
	for ( auto i = 0; i < particles.size (); ++i )
	{
		const auto&	p = particles[ i ];
		auto&		d = renderData[ i ];

		d.x = p.position.x;
		d.y = p.position.y;
		d.alpha = getZ ( p.position.z );
	}

	return renderData;
}
//-----------------------------------------------------------------------------

void CRT_DustParticles::resetParticle ( DustParticle& p )
{
	auto pow2 = [] ( const float x ) { return x * x; };

	// Distribute in a normalized 2D cube (-1 to 1)
	p.position = { random.nextFloat () * 2.0f - 1.0f,
				   random.nextFloat () * 2.0f - 1.0f,
				  pow2 ( random.nextFloat () ) * 2.0f - 1.0f };
	p.velocity = { 0, 0, 0 };
}
//-----------------------------------------------------------------------------

} // namespace lime