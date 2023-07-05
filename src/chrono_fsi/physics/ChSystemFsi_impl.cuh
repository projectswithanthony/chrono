// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Author: Milad Rakhsha, Arman Pazouki, Wei Hu, Radu Serban
// =============================================================================
//
// Implementation of FSI system that includes all subclasses for proximity and
// force calculation, and time integration.
//
// =============================================================================

#ifndef CH_SYSTEMFSI_IMPL_H_
#define CH_SYSTEMFSI_IMPL_H_

#include "chrono/ChConfig.h"

#include <thrust/device_vector.h>
#include <thrust/host_vector.h>
#include <thrust/iterator/detail/normal_iterator.h>
#include <thrust/iterator/transform_iterator.h>
#include <thrust/iterator/zip_iterator.h>
#include <thrust/tuple.h>

#include "chrono_fsi/physics/ChFsiBase.h"
#include "chrono_fsi/math/custom_math.h"
#include "chrono_fsi/utils/ChUtilsDevice.cuh"

namespace chrono {
namespace fsi {

/// @addtogroup fsi_physics
/// @{

/// typedef device iterators for shorthand SPH operation of thrust vectors of Real3
typedef thrust::device_vector<Real3>::iterator r3IterD;
/// typedef device iterators for shorthand SPH operation of thrust vectors of Real4
typedef thrust::device_vector<Real4>::iterator r4IterD;
/// typedef device tuple for holding SPH data pos,vel,[rho,pressure,mu,type]
typedef thrust::tuple<r4IterD, r3IterD, r4IterD, r3IterD, r3IterD> iterTupleSphD;
typedef thrust::zip_iterator<iterTupleSphD> zipIterSphD;

/// typedef host iterators for shorthand SPH operation of thrust vectors of Real3
typedef thrust::host_vector<Real3>::iterator r3IterH;
/// typedef host iterators for shorthand SPH operation of thrust vectors of Real4
typedef thrust::host_vector<Real4>::iterator r4IterH;
/// typedef host tuple for holding SPH data pos,vel,[rho,pressure,mu,type]
typedef thrust::tuple<r4IterH, r3IterH, r4IterH, r3IterH, r3IterH> iterTupleH;
typedef thrust::zip_iterator<iterTupleH> zipIterSphH;

/// typedef device iterators for shorthand rigid body states:
/// pos,orientation in position, velocity and acceleration level
typedef thrust::tuple<r3IterD, r4IterD, r3IterD, r4IterD, r3IterD, r3IterD> iterTupleRigidD;
typedef thrust::zip_iterator<iterTupleRigidD> zipIterRigidD;

/// typedef host iterators for shorthand rigid body states:
/// pos,orientation in position, velocity and acceleration level
typedef thrust::tuple<r3IterH, r4IterH, r3IterH, r4IterH, r3IterH, r3IterH> iterTupleRigidH;
typedef thrust::zip_iterator<iterTupleRigidH> zipIterRigidH;

/// typedef device iterators for shorthand chrono bodies operations
typedef thrust::tuple<r3IterH, r3IterH, r3IterH, r4IterH, r3IterH, r3IterH> iterTupleChronoBodiesH;
typedef thrust::zip_iterator<iterTupleChronoBodiesH> zipIterChronoBodiesH;

/// Struct to store the information of SPH particles on the device
struct SphMarkerDataD {
    thrust::device_vector<Real4> posRadD;     ///< Vector of the positions of particles + characteristic radius
    thrust::device_vector<Real3> velMasD;     ///< Vector of the velocities of particles
    thrust::device_vector<Real4> rhoPresMuD;  ///< Vector of the rho+pressure+mu+type of particles
    thrust::device_vector<Real3> tauXxYyZzD;  ///< Vector of the total stress (diagonal) of particles
    thrust::device_vector<Real3> tauXyXzYzD;  ///< Vector of the total stress (off-diagonal) of particles

    zipIterSphD iterator();
    void resize(size_t s);
};

/// Struct to store the information of SPH particles on the host.
struct SphMarkerDataH {
    thrust::host_vector<Real4> posRadH;     ///< Vector of the positions of particles
    thrust::host_vector<Real3> velMasH;     ///< Vector of the velocities of particles
    thrust::host_vector<Real4> rhoPresMuH;  ///< Vector of the rho+pressure+mu+type of particles
    thrust::host_vector<Real3> tauXxYyZzH;  ///< Vector of the total stress (diagonal) of particles
    thrust::host_vector<Real3> tauXyXzYzH;  ///< Vector of the total stress (off-diagonal) of particles

    zipIterSphH iterator();
    void resize(size_t s);
};

/// Struct to store the information of rigid bodies on the host.
struct FsiBodiesDataH {
    thrust::host_vector<Real3> posRigid_fsiBodies_H;      ///< body linear positions
    thrust::host_vector<Real4> velMassRigid_fsiBodies_H;  ///< body linear velocities
    thrust::host_vector<Real3> accRigid_fsiBodies_H;      ///< body linear accelerations
    thrust::host_vector<Real4> q_fsiBodies_H;             ///< body orientations (quaternions)
    thrust::host_vector<Real3> omegaVelLRF_fsiBodies_H;   ///< body angular velocities
    thrust::host_vector<Real3> omegaAccLRF_fsiBodies_H;   ///< body angular accelerations

    zipIterRigidH iterator();
    void resize(size_t s);
};

/// Struct to store the information of rigid bodies on the device.
struct FsiBodiesDataD {
    thrust::device_vector<Real3> posRigid_fsiBodies_D;      ///< body linear positions
    thrust::device_vector<Real4> velMassRigid_fsiBodies_D;  ///< body linear velocities
    thrust::device_vector<Real3> accRigid_fsiBodies_D;      ///< body linear accelerations
    thrust::device_vector<Real4> q_fsiBodies_D;             ///< body orientations (quaternions)
    thrust::device_vector<Real3> omegaVelLRF_fsiBodies_D;   ///< body angular velocities
    thrust::device_vector<Real3> omegaAccLRF_fsiBodies_D;   ///< body angular accelerations

    zipIterRigidD iterator();
    void CopyFromH(const FsiBodiesDataH& other);
    FsiBodiesDataD& operator=(const FsiBodiesDataD& other);
    void resize(size_t s);
};

/// FEA mesh state on host.
struct FsiMeshStateH {
    thrust::host_vector<Real3> pos_fsi_fea_H;  ///< Vector of the mesh position
    thrust::host_vector<Real3> vel_fsi_fea_H;  ///< Vector of the mesh velocity
    thrust::host_vector<Real3> acc_fsi_fea_H;  ///< Vector of the mesh acceleration
    thrust::host_vector<Real3> dir_fsi_fea_H;  ///< Vector of the mesh direction

    // zipIterFlexH iterator();
    void resize(size_t s);
    size_t size() { return pos_fsi_fea_H.size(); };
};

/// FEA mesh state on device.
struct FsiMeshStateD {
    thrust::device_vector<Real3> pos_fsi_fea_D;  ///< Vector of the mesh position
    thrust::device_vector<Real3> vel_fsi_fea_D;  ///< Vector of the mesh velocity
    thrust::device_vector<Real3> acc_fsi_fea_D;  ///< Vector of the mesh acceleration
    thrust::device_vector<Real3> dir_fsi_fea_D;  ///< Vector of the mesh direction

    // zipIterFlexD iterator();
    void CopyFromH(const FsiMeshStateH& other);
    FsiMeshStateD& operator=(const FsiMeshStateD& other);
    void resize(size_t s);
};

/// Struct to store neighbor search information on the device.
struct ProximityDataD {
    thrust::device_vector<uint> gridMarkerHashD;   ///< gridMarkerHash=s(i,j,k)= k*n_x*n_y + j*n_x + i (numAllMarkers);
    thrust::device_vector<uint> gridMarkerIndexD;  ///< Marker's index, can be original or sorted (numAllMarkers);
    thrust::device_vector<uint> cellStartD;  ///< Index of the particle starts a cell in sorted list (m_numGridCells)
    thrust::device_vector<uint> cellEndD;    ///< Index of the particle ends a cell in sorted list (m_numGridCells)
    thrust::device_vector<uint>
        mapOriginalToSorted;  ///< Index mapping from the original to the sorted (numAllMarkers);

    void resize(size_t s);
};

/// FDI system information information exchanged with the Chrono system.
struct FsiData {
    // fluidfsiBodiesIndex (host)
    thrust::host_vector<int4> referenceArray;      ///< phases in the array of SPH particles
    thrust::host_vector<int4> referenceArray_FEA;  ///< phases in the array of SPH particles for flexible elements

    // Fluid data (device)
    thrust::device_vector<Real4> derivVelRhoD;      ///< dv/dt and d(rho)/dt for particles
    thrust::device_vector<Real4> derivVelRhoD_old;  ///< dv/dt and d(rho)/dt for particles
    thrust::device_vector<Real3> derivTauXxYyZzD;   ///< d(tau)/dt for particles
    thrust::device_vector<Real3> derivTauXyXzYzD;   ///< d(tau)/dt for particles
    thrust::device_vector<Real3> vel_XSPH_D;        ///< XSPH velocity for particles
    thrust::device_vector<Real3> vis_vel_SPH_D;     ///< IISPH velocity for particles
    thrust::device_vector<Real4> sr_tau_I_mu_i;     ///< I2SPH strain-rate, stress, inertia number, friction

    thrust::device_vector<uint> activityIdentifierD;  ///< Identifies if a particle is an active particle or not
    thrust::device_vector<uint> extendedActivityIdD;  ///< Identifies if a particle is in an extended active domain

    thrust::device_vector<uint> freeSurfaceIdD;  ///< Identifies if a particle is close to free surface

    // BCE
    thrust::device_vector<Real3> rigidSPH_MeshPos_LRF_D;  ///< Rigid body BCE position (local reference frame)
    thrust::device_vector<Real3> FlexSPH_MeshPos_LRF_D;   ///< Flex body BCE position (local coordinates) - device
    thrust::host_vector<Real3> FlexSPH_MeshPos_LRF_H;     ///< Flex body BCE position (local coordinates) - host

    thrust::device_vector<uint> rigidIdentifierD;  ///< Identifies which rigid body a particle belongs to
    thrust::device_vector<uint> FlexIdentifierD;   ///< Identifies which flexible body a particle belongs to

    // FSI bodies
    thrust::device_vector<Real3> rigid_FSI_ForcesD;   ///< Vector of the surface-integrated forces to rigid bodies
    thrust::device_vector<Real3> rigid_FSI_TorquesD;  ///< Vector of the surface-integrated torques to rigid bodies
    thrust::device_vector<Real3> Flex_FSI_ForcesD;    ///< Vector of the surface-integrated force on FEA nodes

    thrust::host_vector<int2> CableElementsNodesH;    ///< Vector of the cable elements nodes on host
    thrust::device_vector<int2> CableElementsNodesD;  ///< Vector of the cable elements nodes on device

    thrust::host_vector<int4> ShellElementsNodesH;    ///< Vector of the shell elements nodes on host
    thrust::device_vector<int4> ShellElementsNodesD;  ///< Vector of the shell elements nodes on device
};

/// Underlying implementation of an FSI system.
class ChSystemFsi_impl : public ChFsiBase {
  public:
    ChSystemFsi_impl(std::shared_ptr<SimParams> params);
    virtual ~ChSystemFsi_impl();

    /// Add an SPH particle given its position, physical properties, velocity, and stress.
    void AddSPHParticle(Real4 pos,
                        Real4 rhoPresMu,
                        Real3 vel = mR3(0.0),
                        Real3 tauXxYyZz = mR3(0.0),
                        Real3 tauXyXzYz = mR3(0.0));

    /// Resize the simulation data based on the FSI system constructed.
    void ResizeData(size_t numRigidBodies, size_t numFlexBodies1D, size_t numFlexBodies2D, size_t numFlexNodes);

    /// Extract forces applied on all SPH particles.
    thrust::device_vector<Real4> GetParticleForces();

    /// Extract accelerations of all SPH particles.
    thrust::device_vector<Real4> GetParticleAccelerations();

    /// Find indices of all SPH particles inside the specified OBB.
    thrust::device_vector<int> FindParticlesInBox(const Real3& hsize,
                                                  const Real3& pos,
                                                  const Real3& ax,
                                                  const Real3& ay,
                                                  const Real3& az);

    /// Extract positions of all SPH particles with indices in the provided array.
    /// The return value is a device thrust vector.
    thrust::device_vector<Real4> GetParticlePositions(const thrust::device_vector<int>& indices);

    /// Extract velocities of all SPH particles with indices in the provided array.
    /// The return value is a device thrust vector.
    thrust::device_vector<Real3> GetParticleVelocities(const thrust::device_vector<int>& indices);

    /// Extract forces applied to all SPH particles with indices in the provided array.
    /// The return value is a device thrust vector.
    thrust::device_vector<Real4> GetParticleForces(const thrust::device_vector<int>& indices);

    /// Extract accelerations of all SPH particles with indices in the provided array.
    /// The return value is a device thrust vector.
    thrust::device_vector<Real4> GetParticleAccelerations(const thrust::device_vector<int>& indices);

    std::shared_ptr<SimParams> paramsH;  ///< Parameters of the simulation

    std::shared_ptr<SphMarkerDataD> sphMarkersD1;       ///< Information of SPH particles at state 1 on device
    std::shared_ptr<SphMarkerDataD> sphMarkersD2;       ///< Information of SPH particles at state 2 on device
    std::shared_ptr<SphMarkerDataD> sortedSphMarkersD;  ///< Sorted information of SPH particles at state 1 on device
    std::shared_ptr<SphMarkerDataH> sphMarkersH;        ///< Information of SPH particles on host

    std::shared_ptr<FsiBodiesDataD> fsiBodiesD1;   ///< Information of rigid bodies at state 1 on device
    std::shared_ptr<FsiBodiesDataD> fsiBodiesD2;   ///< Information of rigid bodies at state 2 on device
    std::shared_ptr<FsiBodiesDataH> fsiBodiesH;    ///< Information of rigid bodies at state 1 on host
    std::shared_ptr<FsiMeshStateD> fsiMeshStateD;  ///< FEA mesh state (device)
    std::shared_ptr<FsiMeshStateH> fsiMeshStateH;  ///< FEA mesh state (host)

    std::shared_ptr<FsiData> fsiData;  ///< General FSI data needed in the simulation

    std::shared_ptr<ProximityDataD> markersProximityD;  ///< Information of neighbor search on the device

  private:
    void ArrangeDataManager();
    void ConstructReferenceArray();
    void InitNumObjects();
    void CalcNumObjects();

    friend class ChSystemFsi;
};

/// @} fsi_physics

}  // end namespace fsi
}  // end namespace chrono

#endif
