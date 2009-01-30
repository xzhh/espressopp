
// Example C++ program doing the same as the force loop Python script

#include "types.hpp"
#include "bc/PBC.hpp"
#include "interaction/LennardJones.hpp"
#include "pairs/All.hpp"
#include "pairs/PairForceComputer.hpp"
#include "particleset/All.hpp"
#include "particlestorage/ParticleStorage.hpp"

#include "ParticleWriter.hpp"
#include "PairWriteComputer.hpp"

#include <cstdio>
#include <vector>

using namespace std;
using namespace espresso;
using namespace espresso::bc;
using namespace espresso::pairs;
using namespace espresso::particleset;
using namespace espresso::particlestorage;
using namespace espresso::interaction;

/** N stands for number particles in each dimensions.
*/

#define N 3
#define SIZE 5.0

typedef espresso::pairs::All AllPairs;
typedef espresso::particleset::All AllParticles;

/** Main routine of a test program:

    - generate N * N * N particle in a box of size SIZE * SIZE * SIZE 
    - print out particle data
    - define periodic boundary conditions
    - define Lennard Jones interaction and apply it to all pairs
    - print out particle data
*/

int main() 

{
  // Create a new particle storage

  ParticleStorage particleStorage;
  size_t position = particleStorage.addProperty<real>(3);
  size_t force = particleStorage.addProperty<real>(3);

  // generate particles in the particle storage

  for (int i = 0; i < N; i++) 
  for (int j = 0; j < N; j++) 
  for (int k = 0; k < N; k++) {
      
       real r;
       r = 0.4 + 0.2 * rand() / RAND_MAX;
       real x = (i + r) / N * SIZE;
       real y = (j + r) / N * SIZE; 
       real z = (k + r) / N * SIZE;

       ParticleStorage::reference ref =
	   particleStorage.getParticleByID(particleStorage.addParticle());
       ParticleStorage::PropertyTraits<real>::ArrayReference positionRef=
	   particleStorage.getArrayProperty<real>(position);
       ParticleStorage::PropertyTraits<real>::ArrayReference forceRef=
	   particleStorage.getArrayProperty<real>(force);

       positionRef[ref][0] = x;
       positionRef[ref][1] = y;
       positionRef[ref][2] = z;

       forceRef[ref][0] = 0.0;
       forceRef[ref][1] = 0.0;
       forceRef[ref][2] = 0.0;
  }

  // For test only: ParticleWriter prints each particle

  ParticleWriter pWriter(particleStorage, position, force);

  // call pWriter(ref) for each particle reference ref of particle storage

  particleStorage.foreach(pWriter);

  // define periodic boundary conditions

  PBC pbc(SIZE);

  // define a set of all particles

  AllParticles allset(&particleStorage);

  // define allpairs with (x, y) for all x, y in allset

  AllPairs allpairs(pbc, allset, position);

  // For test only: PairWriter prints each particle pair 

  PairWriteComputer pairWriter(&particleStorage, position);

  // call pairWriter(ref1, ref2) for each particle ref pair of allset

  allpairs.foreach(pairWriter);

  // define LennardJones interaction

  LennardJones ljint;

  ljint.setCutoff(2.5);
  ljint.setEpsilon(1.0);
  ljint.setSigma(1.0);

  // force will be the vector of all forces in the particle storage
  // and force[ref] returns the force (as RealArrayRef) of particle reference ref

  PairForceComputer::RealArrayRef forceRef = particleStorage.getArrayProperty<real>(force);

  // Define a pair computer that computes the forces for particle pairs
  // ljint provides the routine computeForce for a particle pair
  // force (pointer to all forces of particles) tells us where the computed forces are added

  PairForceComputer forcecompute(forceRef, ljint);

  // call forcecompute(ref1, ref2) for each particle ref pair of allset

  allpairs.foreach(forcecompute);

  // print out all particle data to see that it calculates some forces

  particleStorage.foreach(pWriter);

}
