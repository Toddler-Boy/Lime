#include "lime_CRT_DustParticles.h"

namespace lime
{

//-----------------------------------------------------------------------------

CRT_DustParticles::CRT_DustParticles ( int numParticles )
{
	particles.resize ( numParticles );

	for ( auto& p : particles )
		resetParticle ( p );
}
//-----------------------------------------------------------------------------

namespace
{

float getZ ( const float z )
{
	return ( z + 1.1f ) / 2.0f;
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
		const auto	speedMultiplier = std::lerp ( 0.00001f, 0.01f, zPos * 0.1f );

		p.position.x += p.velocity.x * speedMultiplier;
		p.position.y += p.velocity.y * speedMultiplier;
		p.position.z += p.velocity.z * speedMultiplier;

		// Wrap around boundaries (keep in view)
		if ( p.position.x > 1.01f )			p.position.x = -0.01f;
		else if ( p.position.x < -0.01f )	p.position.x = 1.01f;

		if ( p.position.y > 1.01f )			p.position.y = -0.01f;
		else if ( p.position.y < -0.01f )	p.position.y = 1.01f;

		if ( p.position.z > 1.0f )			p.position.z = 1.0f;
		else if ( p.position.z < -1.0f )	p.position.z = -1.0f;

		p.alpha = zPos;// *0.5f + 0.3f;
	}
}
//-----------------------------------------------------------------------------

juce::Rectangle<float> CRT_DustParticles::getQuad ( const DustParticle& p, const float width, const float height, const float scale ) const
{
	const auto	visualSize = scale * 1.25f;

	return juce::Rectangle<float> ( p.position.x * width - visualSize * 0.5f,
									p.position.y * height - visualSize * 0.5f,
									visualSize,
									visualSize );
}
//-----------------------------------------------------------------------------

void CRT_DustParticles::resetParticle ( DustParticle& p )
{
	// Distribute in a normalized 2D cube (0 to 1)
	p.position = { random.nextFloat (),
				   random.nextFloat (),
				   random.nextFloat () * 2.0f - 1.0f };
	p.velocity = { 0, 0, 0 };
}
//-----------------------------------------------------------------------------

} // namespace lime