/*

  Copyright (C) 2017
      Gregor Deichmann (TU Darmstadt)
  Copyright (C) 2012,2013
      Max Planck Institute for Polymer Research
  Copyright (C) 2008,2009,2010,2011
      Max-Planck-Institute for Polymer Research & Fraunhofer SCAI
  Copyright (C) 2022
      Data Center, Johannes Gutenberg University Mainz

  This file is part of ESPResSo++.

  ESPResSo++ is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  ESPResSo++ is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "python.hpp"
#include "DPDThermostat.hpp"

#include "types.hpp"
#include "System.hpp"
#include "storage/Storage.hpp"
#include "iterator/CellListIterator.hpp"
#include "esutil/RNG.hpp"
#include "bc/BC.hpp"
#include <cmath>
#include <utility>

#ifdef RANDOM123_EXIST
#include "mpi.hpp"
#include <boost/signals2.hpp>
/*UNCOMMENT TO ENABLE GAUSSIAN DISTRIBUTION
#ifndef M_PIl
#define M_PIl 3.1415926535897932384626433832795029L
#endif
#define M_2PI (2 * M_PIl)
*/
#endif

namespace espressopp
{
namespace integrator
{
using namespace espressopp::iterator;
// using namespace r123;

DPDThermostat::DPDThermostat(std::shared_ptr<System> system,
                             std::shared_ptr<VerletList> _verletList,
                             int _ntotal)
    : Extension(system), verletList(_verletList), ntotal(_ntotal)
{
    type = Extension::Thermostat;

    gamma = 0.0;
    temperature = 0.0;

    mdStep = 0;
#ifdef RANDOM123_EXIST
    ncounter_per_pair = 1;
    if (tgamma > 0.0) ncounter_per_pair++;
    if (ntotal <= 0)
        throw std::runtime_error("DPD/random123 needs a read to the total number of particles");
#endif
    current_cutoff = verletList->getVerletCutoff() - system->getSkin();
    current_cutoff_sqr = current_cutoff * current_cutoff;

    if (!system->rng)
    {
        throw std::runtime_error("system has no RNG");
    }

    rng = system->rng;

    LOG4ESPP_INFO(theLogger, "DPD constructed");
}

void DPDThermostat::setGamma(real _gamma) { gamma = _gamma; }

real DPDThermostat::getGamma() { return gamma; }

void DPDThermostat::setTGamma(real _tgamma) { tgamma = _tgamma; }

real DPDThermostat::getTGamma() { return tgamma; }

void DPDThermostat::setTemperature(real _temperature) { temperature = _temperature; }

real DPDThermostat::getTemperature() { return temperature; }

DPDThermostat::~DPDThermostat() { disconnect(); }

void DPDThermostat::disconnect()
{
    _initialize.disconnect();
    _heatUp.disconnect();
    _coolDown.disconnect();
    _thermalize.disconnect();
}

void DPDThermostat::connect()
{
    // connect to initialization inside run()
    _initialize = integrator->runInit.connect(std::bind(&DPDThermostat::initialize, this));

    _heatUp = integrator->recalc1.connect(std::bind(&DPDThermostat::heatUp, this));

    _coolDown = integrator->recalc2.connect(std::bind(&DPDThermostat::coolDown, this));

    _thermalize = integrator->aftInitF.connect(std::bind(&DPDThermostat::thermalize, this));
}

void DPDThermostat::thermalize()
{
    LOG4ESPP_DEBUG(theLogger, "thermalize DPD");

    System& system = getSystemRef();
    system.storage->updateGhostsV();

#ifdef RANDOM123_EXIST
    uint64_t internal_seed = system.seed64;
    if (mdStep == 0)
    {
        if (internal_seed == 0)
        {
            if (system.comm->rank() == 0)
            {
                int rng1, rng2, rng3;
                rng1 = (*rng)(2);
                rng2 = (*rng)(INT_MAX);
                rng3 = (*rng)(INT_MAX);
                internal_seed =
                    (uint64_t)rng1 * (uint64_t)rng2 * (uint64_t)UINT_MAX + (uint64_t)rng3;
            }

            mpi::broadcast(*system.comm, internal_seed, 0);
            system.seed64 = internal_seed;
        }

        counter = {{0}};
        ukey = {{internal_seed}};
        key = ukey;
        // crng = threefry2x64(counter, key);
        // counter.v[0]=ULONG_MAX-1;std::cout<<"CTR> "<<counter.v[0]<<"\n";
        // std::cout<<"RNG-"<<system->comm->rank()<<" ("<<counter
        //<<","<<key<<") /"<<uneg11<double>(crng.v[0])<<std::endl;
    }
#endif

    // loop over VL pairs
    intStep = integrator->getStep();
    for (PairList::Iterator it(verletList->getPairs()); it.isValid(); ++it)
    {
        Particle& p1 = *it->first;
        Particle& p2 = *it->second;

        if (gamma > 0.0) frictionThermoDPD(p1, p2);
        if (tgamma > 0.0) frictionThermoTDPD(p1, p2);
    }

#ifdef RANDOM123_EXIST
    uint64_t ncounter_per_step =
        (uint64_t)ntotal * (uint64_t)(ntotal - 1) / (uint64_t)2 * (uint64_t)ncounter_per_pair;

    if (ULONG_MAX - mdStep * ncounter_per_step < ncounter_per_step)
    {
        mdStep = 0;
        internal_seed = 0;
        system.seed64 = 0;
    }
    else
        mdStep++;
#endif
}

void DPDThermostat::frictionThermoDPD(Particle& p1, Particle& p2)
{
    // Implements the standard DPD thermostat
    Real3D r = p1.position() - p2.position();
    real dist2 = r.sqr();
    System& system = getSystemRef();

    // Test code for different thermalizing modes
    // mode(0): the thermostat acts on peculiar velocities (default)
    // mode(1): on full velocities (incl. shear contribution);
    // mode(2): on y-dir of velocities ONLY (vorticity)
    /* UNCOMMENT TO ACTIVATE MODE1/2
    int modeThermal = system.lebcMode;
    */

    if (dist2 < current_cutoff_sqr)
    {
        real dist = sqrt(dist2);
        real omega = 1 - dist / current_cutoff;
        real omega2 = omega * omega;
        real veldiff = .0;

#ifdef RANDOM123_EXIST
        uint64_t i = p1.id();
        uint64_t j = p2.id();
        if (i > j) std::swap(i, j);

        counter.v[0] =
            (intStep * ntotal * (ntotal - 1) / 2 + (ntotal * (i - 1) - i * (i + 1) / 2 + j)) *
            ncounter_per_pair;
        crng = threefry2x64(counter, key);  // call rng generator

        real zrng = u01<double>(crng.v[0]);

        /*UNCOMMENT TO ENABLE GAUSSIAN DISTRIBUTION
        real u2 = u01<double>(crng.v[1]);
        zrng=sqrt(-2.0*log(zrng))*cos(M_2PI*u2); // get a rng with normal distribution
        */
#endif
        r /= dist;

        /*  UNCOMMENT TO ACTIVATE MODE1/2
        if (system.ifShear)
            if (modeThermal == 1)
            {
                Real3D vsdiff = {system.shearRate * (p1.position()[2] - p2.position()[2]), .0, .0};
                veldiff = (p1.velocity() + vsdiff - p2.velocity()) * r;
            }
            else if (modeThermal == 2)
                veldiff = (p1.velocity()[1] - p2.velocity()[1]) * r[1];
            else
                veldiff = (p1.velocity() - p2.velocity()) * r;
        else
        */
        veldiff = (p1.velocity() - p2.velocity()) * r;

        real friction = pref1 * omega2 * veldiff;
#ifdef RANDOM123_EXIST
        real r0 = zrng - 0.5;
        /*UNCOMMENT TO ENABLE GAUSSIAN DISTRIBUTION
        r0 = zrng;
        */
#else
        real r0 = ((*rng)() - 0.5);
#endif
        real noise = pref2 * omega * r0;  //(*rng)() - 0.5);

        Real3D f = (noise - friction) * r;

        /*  UNCOMMENT TO ACTIVATE MODE1/2
        if (system.ifShear && modeThermal == 2)
        {
            f[0]=.0;
            f[2]=.0;
        }
        */

        p1.force() += f;
        p2.force() -= f;

        // Analysis to get stress tensors
        if (system.ifShear && system.ifViscosity)
        {
            system.dyadicP_xz += r[0] * f[2];
            system.dyadicP_zx += r[2] * f[0];
        }
    }
}

void DPDThermostat::frictionThermoTDPD(Particle& p1, Particle& p2)
{
    // Implements a transverse DPD thermostat with the canonical functional form of omega
    Real3D r = p1.position() - p2.position();
    real dist2 = r.sqr();
    System& system = getSystemRef();

    // Test code for different thermalizing modes
    // mode(0): the thermostat acts on peculiar velocities (default)
    // mode(1): on full velocities (incl. shear contribution);
    // mode(2): on y-dir of velocities ONLY (vorticity)
    /* UNCOMMENT TO ACTIVATE MODE1/2
    int modeThermal = system.lebcMode;
    */

    if (dist2 < current_cutoff_sqr)
    {
        real dist = sqrt(dist2);
        real omega = 1 - dist / current_cutoff;
        real omega2 = omega * omega;
        Real3D veldiff = .0;

        r /= dist;

        Real3D noisevec(0.0);
#ifdef RANDOM123_EXIST
        int i = p1.id();
        int j = p2.id();
        if (i > j) std::swap(i, j);

        counter.v[0] =
            (intStep * ntotal * (ntotal - 1) / 2 + (ntotal * (i - 1) - i * (i + 1) / 2 + j)) *
            ncounter_per_pair;
        crng = threefry2x64(counter, key);  // call rng generator
        real zrng = u01<double>(crng.v[1]);
        noisevec[0] = zrng - 0.5;
        counter.v[0]--;
        zrng = u01<double>(crng.v[0]);
        noisevec[1] = zrng - 0.5;
        zrng = u01<double>(crng.v[1]);
        noisevec[2] = zrng - 0.5;
#else
        noisevec[0] = (*rng)() - 0.5;
        noisevec[1] = (*rng)() - 0.5;
        noisevec[2] = (*rng)() - 0.5;
#endif
        /* UNCOMMENT TO ACTIVATE MODE1/2
        if (system.ifShear)
            if (modeThermal == 1)
            {
                Real3D vsdiff = {system.shearRate * (p1.position()[2] - p2.position()[2]), .0, .0};
                veldiff = p1.velocity() - p2.velocity() + vsdiff;
            }
            else if (modeThermal == 2)
                veldiff[1] = p1.velocity()[1] - p2.velocity()[1];
            else*/
        veldiff = p1.velocity() - p2.velocity();

        Real3D f_damp, f_rand;

        // Calculate matrix product of projector and veldiff vector:
        // P dv = (I - r r_T) dv
        f_damp[0] =
            (1.0 - r[0] * r[0]) * veldiff[0] - r[0] * r[1] * veldiff[1] - r[0] * r[2] * veldiff[2];
        f_damp[1] =
            (1.0 - r[1] * r[1]) * veldiff[1] - r[1] * r[0] * veldiff[0] - r[1] * r[2] * veldiff[2];
        f_damp[2] =
            (1.0 - r[2] * r[2]) * veldiff[2] - r[2] * r[0] * veldiff[0] - r[2] * r[1] * veldiff[1];

        // Same with random vector
        f_rand[0] = (1.0 - r[0] * r[0]) * noisevec[0] - r[0] * r[1] * noisevec[1] -
                    r[0] * r[2] * noisevec[2];
        f_rand[1] = (1.0 - r[1] * r[1]) * noisevec[1] - r[1] * r[0] * noisevec[0] -
                    r[1] * r[2] * noisevec[2];
        f_rand[2] = (1.0 - r[2] * r[2]) * noisevec[2] - r[2] * r[0] * noisevec[0] -
                    r[2] * r[1] * noisevec[1];

        f_damp *= pref3 * omega2;
        f_rand *= pref4 * omega;

        Real3D f_tmp = f_rand - f_damp;
        p1.force() += f_tmp;
        p2.force() -= f_tmp;
        // Analysis to get stress tensors
        if (system.ifShear && system.ifViscosity)
        {
            system.dyadicP_xz += r[0] * f_tmp[2];
            system.dyadicP_zx += r[2] * f_tmp[0];
        }
    }
}

void DPDThermostat::initialize()
{
    // calculate the prefactors
    System& system = getSystemRef();
    current_cutoff = verletList->getVerletCutoff() - system.getSkin();
    current_cutoff_sqr = current_cutoff * current_cutoff;

    real timestep = integrator->getTimeStep();

    LOG4ESPP_INFO(theLogger, "init, timestep = " << timestep << ", gamma = " << gamma
                                                 << ", tgamma = " << tgamma
                                                 << ", temperature = " << temperature);

    pref1 = gamma;
    pref2 = sqrt(24.0 * temperature * gamma / timestep);
    pref3 = tgamma;
    pref4 = sqrt(24.0 * temperature * tgamma / timestep);
}

/** very nasty: if we recalculate force when leaving/reentering the integrator,
    a(t) and a((t-dt)+dt) are NOT equal in the vv algorithm. The random
    numbers are drawn twice, resulting in a different variance of the random force.
    This is corrected by additional heat when restarting the integrator here.
    Currently only works for the Langevin thermostat, although probably also others
    are affected.
*/

void DPDThermostat::heatUp()
{
    LOG4ESPP_INFO(theLogger, "heatUp");

    pref2buffer = pref2;
    pref2 *= sqrt(3.0);
    pref4buffer = pref4;
    pref4 *= sqrt(3.0);
}

/** Opposite to heatUp */

void DPDThermostat::coolDown()
{
    LOG4ESPP_INFO(theLogger, "coolDown");

    pref2 = pref2buffer;
    pref4 = pref4buffer;
}

/****************************************************
** REGISTRATION WITH PYTHON
****************************************************/

void DPDThermostat::registerPython()
{
    using namespace espressopp::python;
    class_<DPDThermostat, std::shared_ptr<DPDThermostat>, bases<Extension> >(
        "integrator_DPDThermostat",
        init<std::shared_ptr<System>, std::shared_ptr<VerletList>, int>())
        .def("connect", &DPDThermostat::connect)
        .def("disconnect", &DPDThermostat::disconnect)
        .add_property("gamma", &DPDThermostat::getGamma, &DPDThermostat::setGamma)
        .add_property("tgamma", &DPDThermostat::getTGamma, &DPDThermostat::setTGamma)
        .add_property("temperature", &DPDThermostat::getTemperature,
                      &DPDThermostat::setTemperature);
}
}  // namespace integrator
}  // namespace espressopp
