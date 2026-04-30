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

void CRT_DustParticles::resetParticle ( DustParticle& p )
{
	// Distribute in a normalized 2D cube (-1 to 1)
	p.px = random.nextFloat () * 2.0f - 1.0f;
	p.py = random.nextFloat () * 2.0f - 1.0f;
	p.pz = random.nextFloat () * 2.0f - 1.0f;

	p.vx = 0.0f;
	p.vy = 0.0f;
	p.vz = 0.0f;
}
//-----------------------------------------------------------------------------

} // namespace lime