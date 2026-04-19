#pragma once

#include <vector>

namespace lime
{

/**
 * A 3D Dust Particle System.
 * Simulates coherent but erratic motion via a wind field (Perlin Noise).
 * Particles descend and recycle to the top of the "volume".
 */
class CRT_DustParticles
{
public:
	struct DustParticle
	{
		juce::Vector3D<float> position;
		juce::Vector3D<float> velocity;
		float	size;				// Base pixel size
		float	alpha;				// Calculated based on depth
		float	targetAlpha;
	};

	CRT_DustParticles ( int numParticles );

	void update ( float deltaTime );
	juce::Rectangle<float> getQuad ( const DustParticle& p, const float width, const float height, const float scale ) const;

	const std::vector<DustParticle>& getParticles () const { return particles; }
	int getNumParticles () const { return int ( particles.size () ); }

private:
	void resetParticle ( DustParticle& p );

	std::vector<DustParticle>	particles;
	juce::Random				random;
};

} // namespace lime
