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
		float	alpha;				// Calculated based on depth
	};

	struct renderParticles
	{
		float	x;
		float	y;
		float	alpha;
	};

	CRT_DustParticles ( int numParticles );

	void update ( float deltaTime );

	std::vector<renderParticles>& getParticles ();

private:
	void resetParticle ( DustParticle& p );

	std::vector<DustParticle>		particles;
	std::vector<renderParticles>	renderData;
	juce::Random					random;
};

} // namespace lime
