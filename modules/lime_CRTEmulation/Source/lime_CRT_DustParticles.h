#pragma once

#include <vector>

namespace lime
{

class CRT_DustParticles
{
public:
	struct DustParticle
	{
		float	px;
		float	py;
		float	pz;

		float	vx;
		float	vy;
		float	vz;
	};

	CRT_DustParticles ( int numParticles );

	const std::vector<DustParticle>& getParticles () const { return particles; }

private:
	void resetParticle ( DustParticle& p );

	std::vector<DustParticle>		particles;
	juce::Random					random;
};

} // namespace lime
