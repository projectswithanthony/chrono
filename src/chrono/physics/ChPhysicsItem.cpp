// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All right reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Alessandro Tasora, Radu Serban
// =============================================================================

#include "chrono/physics/ChPhysicsItem.h"

namespace chrono {

// Register into the object factory, to enable run-time dynamic creation and persistence
ChClassRegisterABSTRACT<ChPhysicsItem> a_registration_ChPhysicsItem;

ChPhysicsItem::ChPhysicsItem(const ChPhysicsItem& other) : ChObj(other) {
    assets = other.assets;

    // Do not copy the system; this is initialized at insertion time
    system = NULL;
    offset_x = other.offset_x;
    offset_w = other.offset_w;
    offset_L = other.offset_L;
}

ChPhysicsItem::~ChPhysicsItem() {
    SetSystem(NULL);  // note that this might remove collision model from system
}

void ChPhysicsItem::Copy(ChPhysicsItem* source) {
    // first copy the parent class data...
    ChObj::Copy(source);

    // copy other class data
    system = 0;  // do not copy - must be initialized with insertion in system.

    this->offset_x = source->offset_x;
    this->offset_w = source->offset_w;
    this->offset_L = source->offset_L;

    this->assets = source->assets;  // copy the list of shared pointers to assets
}

void ChPhysicsItem::SetSystem(ChSystem* m_system) {
    if (system == m_system)  // shortcut if no change
        return;
    if (system) {
        if (this->GetCollide())
            this->RemoveCollisionModelsFromSystem();
    }
    system = m_system;  // set here
    if (system) {
        if (this->GetCollide())
            this->AddCollisionModelsToSystem();
    }
}

std::shared_ptr<ChAsset> ChPhysicsItem::GetAssetN(unsigned int num) {
    if (num < assets.size())
        return assets[num];
    return std::shared_ptr<ChAsset>();
}

void ChPhysicsItem::GetTotalAABB(ChVector<>& bbmin, ChVector<>& bbmax) {
    bbmin.Set(-1e200, -1e200, -1e200);
    bbmax.Set(1e200, 1e200, 1e200);
}

void ChPhysicsItem::GetCenter(ChVector<>& mcenter) {
    ChVector<> mmin, mmax;
    this->GetTotalAABB(mmin, mmax);
    mcenter = (mmin + mmax) * 0.5;
}

void ChPhysicsItem::Update(double mytime, bool update_assets) {
    this->ChTime = mytime;

    if (update_assets) {
        for (unsigned int ia = 0; ia < this->assets.size(); ++ia)
            assets[ia]->Update(this, this->GetAssetsFrame().GetCoord());
    }
}

void ChPhysicsItem::ArchiveOUT(ChArchiveOut& marchive) {
    // version number
    marchive.VersionWrite(1);

    // serialize parent class
    ChObj::ArchiveOUT(marchive);

    // serialize all member data:
    // marchive << CHNVP(system); ***TODO***
    marchive << CHNVP(assets);
    // marchive << CHNVP(offset_x);
    // marchive << CHNVP(offset_w);
    // marchive << CHNVP(offset_L);
}

/// Method to allow de serialization of transient data from archives.
void ChPhysicsItem::ArchiveIN(ChArchiveIn& marchive) {
    // version number
    int version = marchive.VersionRead();

    // deserialize parent class
    ChObj::ArchiveIN(marchive);

    // stream in all member data:
    // marchive >> CHNVP(system); ***TODO***
    marchive >> CHNVP(assets);
    // marchive >> CHNVP(offset_x);
    // marchive >> CHNVP(offset_w);
    // marchive >> CHNVP(offset_L);
}

}  // end namespace chrono
