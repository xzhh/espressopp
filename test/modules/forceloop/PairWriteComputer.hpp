#include "types.hpp"

#include "particlestorage/ParticleStorage.hpp"
#include "pairs/ParticlePairComputer.hpp"

namespace espresso {

   namespace pairs {

     typedef particlestorage::ParticleStorage::PropertyTraits<size_t>::ConstReference ConstSizeRef;
     typedef particlestorage::ParticleStorage::PropertyTraits<real>::ConstArrayReference ConstRealArrayRef;

     /** This class is used to print all particle pairs.

        \code
         PairWriteComputer pairWriteComputer(particlestorage);
         ParticlePairs pairs ...
         pairs->foreach(pairWriteComputer)
        \endcode

        \sa espresso::pairs::ParticlePairComputer
     */

     class PairWriteComputer: public ConstParticlePairComputer {

     public:

       ConstRealArrayRef pos;  //<! const property position
       ConstSizeRef id;        //<! const property id(entification)

       /** Constructor of the pair writer class.
         
           \param particleStorage is needed to access the properties that will be printed

       */

       PairWriteComputer(const espresso::particlestorage::ParticleStorage* particleStorage,
	                 size_t position) :

	  pos(particleStorage->getArrayProperty<real>(position)),
          id(particleStorage->getIDProperty())

       {
       }

       /** Implementation of the pure routine that is applied to each particle pair.

          \sa espresso::ParticlePairComputer::operator()
       */

       virtual void operator()(const Real3D dist,
                               const espresso::particleset::ParticleSet::const_reference p1,
                               const espresso::particleset::ParticleSet::const_reference p2)

       {
          printf("Pair: id = (%d,%d) , pos1 = (%f,%f,%f), pos2 = (%f,%f,%f), dist = (%f, %f, %f)\n",

               id[p1], id[p2], 
               pos[p1][0], pos[p1][1], pos[p1][2],
               pos[p2][0], pos[p2][1], pos[p2][2],
               dist.getX(), dist.getY(), dist.getZ());
       }
     };
  }
}
