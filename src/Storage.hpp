#ifndef STORAGE_HPP
#define STORAGE_HPP
#include <vector>
#include <mpi.hpp>
#include <boost/unordered_map.hpp>
#include "log4espp.hpp"

#include "Particle.hpp"
#include "System.hpp"

typedef std::vector< std::pair<int,int> > PairList;

/**
   Iterates all Particles in a list of cells. This is a Python-like,
   self-contained iterator: isValid() tells whether there are more
   particles to come.
*/
class ParticleIterator {
public:
  ParticleIterator(std::vector<Cell *> &lst)
    : cCell(lst.begin()), endCell(lst.end()), part(0)
  {
    if (!isValid()) {
      end = 0;
      return;
    }
    end = (*cCell)->size();
    if (part >= end) {
      findNonemptyCell();
    }
  }
  
  ParticleIterator &operator++()
  {
    if (++part >= end) {
      findNonemptyCell();
    }
    return *this;
  }

  bool isValid() const { return cCell != endCell; }

  Particle &operator*() const { return (*(*cCell))[part]; }
  Particle *operator->() const { return &((*(*cCell))[part]); }

private:
  void findNonemptyCell();

  std::vector<Cell *>::iterator cCell, endCell;
  integer part, end;
};

/** represents the particle storage of one system. */
class Storage {
public:
  Storage(System *,
          const boost::mpi::communicator &,
          bool useVList);
  virtual ~Storage();

  void addParticle(integer id, const real pos[3]);

  integer getNActiveParticles() const;
  void fetchParticles(Storage &);

  std::vector<Cell> &getAllCells()      { return cells; }
  std::vector<Cell *> &getActiveCells() { return localCells; }
  std::vector<Cell *> &getGhostCells()  { return ghostCells; }

  virtual Cell *mapPositionToCellClipping(const real pos[3]) = 0;
  virtual Cell *mapPositionToCellChecked(const real pos[3]) = 0;

protected:
  // update the id->local particle map for the given cell
  void updateLocalParticles(Cell *);
  // append a particle to a list, without updating localParticles
  Particle *appendUnindexedParticle(Cell *, Particle *);
  // append a particle to a list, updating localParticles
  Particle *appendIndexedParticle(Cell *, Particle *);
  // move a particle from one list to another, without updating localParticles
  Particle *moveUnindexedParticle(Cell *dst, Cell *src, integer srcpos);
  // move a particle from one list to another, updating localParticles
  Particle *moveIndexedParticle(Cell *dst, Cell *src, integer srcpos);

  /// map particle id to Particle *
  boost::unordered_map<integer, Particle * > localParticles;
  boost::mpi::communicator comm;
  System *system;
  
  /** Structure containing information about local interactions
      with particles in a neighbor cell. */
  struct IANeighbor {
    /// Pointer to cells of neighboring cells
    Cell *pList1, *pList2;
    /// Verlet list for non bonded interactions of a cell with a neighbor cell
    PairList vList;
  };

  /** List of neighbor cells and their interactions.

  A word about the interacting neighbor cells:

  In a 3D lattice each cell has 27 neighbors (including
  itself!). Since we deal with pair forces, it is sufficient to
  calculate only half of the interactions (Newtons law: actio =
  reactio). For each cell 13+1=14 neighbors. This has only to be
  done for the inner cells. 

  Caution: This implementation needs double sided ghost
  communication! For single sided ghost communication one would
  need some ghost-ghost cell interaction as well, which we do not
  need! 

  It follows: inner cells: #neighbors = 14
  ghost cells:             #neighbors = 0
  */
  typedef std::vector<IANeighbor> IANeighborList;

  /** flag for using Verlet List. */
  integer useVList;

  /** Array containing information about the interactions between the cells. */
  std::vector<IANeighborList> cellInter;

  /** here the particles are actually stored */
  std::vector<Cell> cells;

  /** list of local (active) cells */
  std::vector<Cell *> localCells;
  /** list of ghost cells */
  std::vector<Cell *> ghostCells;

  static LOG4ESPP_DECL_LOGGER(logger);
};

#endif